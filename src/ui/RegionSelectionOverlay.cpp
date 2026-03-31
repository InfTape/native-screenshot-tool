#include "ui/RegionSelectionOverlay.h"

#include <Windows.h>
#include <WindowsX.h>

#include <algorithm>
#include <string>
#include <vector>

#include "common/RectUtils.h"
#include "common/Win32Error.h"
#include "editing/ImageMarkupService.h"

namespace {

RECT UnionIfNeeded(const RECT& left, bool has_left, const RECT& right, bool has_right) {
    if (!has_left) {
        return has_right ? right : RECT{};
    }
    if (!has_right) {
        return left;
    }

    RECT result{};
    UnionRect(&result, &left, &right);
    return result;
}

POINT ClampPointToRect(const POINT& point, const RECT& rect) {
    POINT clamped{};
    clamped.x = std::clamp(point.x, rect.left, rect.right - 1);
    clamped.y = std::clamp(point.y, rect.top, rect.bottom - 1);
    return clamped;
}

}  // namespace

namespace ui {

RegionSelectionOverlay::RegionSelectionOverlay(HINSTANCE instance) : instance_(instance) {}

common::Result<std::optional<RegionSelectionResult>> RegionSelectionOverlay::SelectRegion(
    const capture::DesktopSnapshot& snapshot) {
    return RunSession(snapshot, false);
}

common::Result<std::optional<RegionSelectionResult>> RegionSelectionOverlay::EditImage(
    const capture::DesktopSnapshot& snapshot) {
    return RunSession(snapshot, true);
}

common::Result<std::optional<RegionSelectionResult>> RegionSelectionOverlay::RunSession(
    const capture::DesktopSnapshot& snapshot,
    bool start_with_full_selection) {
    snapshot_ = &snapshot;
    working_image_ = snapshot.image;
    session_.start_with_full_selection = start_with_full_selection;
    InitializeSessionState();

    const auto cleanup = [this]() {
        if (window_ != nullptr) {
            DestroyWindow(window_);
            window_ = nullptr;
        }

        DestroyBackBuffer();
        snapshot_ = nullptr;
        working_image_ = capture::CapturedImage{};
        session_.ClearHistory();
        session_.start_with_full_selection = false;
        toolbar_.Hide();
    };

    auto register_result = RegisterWindowClass();
    if (!register_result) {
        cleanup();
        return common::Result<std::optional<RegionSelectionResult>>::Failure(
            register_result.Error());
    }

    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                              kClassName,
                              L"选区截图",
                              WS_POPUP,
                              snapshot.origin_x,
                              snapshot.origin_y,
                              snapshot.image.Width(),
                              snapshot.image.Height(),
                              nullptr,
                              nullptr,
                              instance_,
                              this);
    if (window_ == nullptr) {
        const std::wstring error_message =
            L"创建选区窗口失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        cleanup();
        return common::Result<std::optional<RegionSelectionResult>>::Failure(error_message);
    }

    auto create_back_buffer_result = CreateBackBuffer();
    if (!create_back_buffer_result) {
        cleanup();
        return common::Result<std::optional<RegionSelectionResult>>::Failure(
            create_back_buffer_result.Error());
    }

    if (start_with_full_selection) {
        const RECT full_image_rect{0, 0, snapshot.image.Width(), snapshot.image.Height()};
        if (common::HasArea(full_image_rect)) {
            CommitSelection(full_image_rect);
        }
    }

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    SetFocus(window_);

    common::Result<void> loop_result = common::Result<void>::Success();
    MSG message{};
    while (!session_.finished) {
        const BOOL message_result = GetMessageW(&message, nullptr, 0, 0);
        if (message_result == -1) {
            loop_result = common::Result<void>::Failure(
                L"选区消息循环失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
            break;
        }

        if (message_result == 0) {
            session_.finished = true;
            break;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    common::Result<std::optional<RegionSelectionResult>> result =
        common::Result<std::optional<RegionSelectionResult>>::Success(
            std::optional<RegionSelectionResult>{});
    if (!loop_result) {
        result = common::Result<std::optional<RegionSelectionResult>>::Failure(loop_result.Error());
    } else if (session_.accepted && common::HasArea(session_.selection)) {
        auto cropped_image = working_image_.Crop(session_.selection);
        if (!cropped_image) {
            result = common::Result<std::optional<RegionSelectionResult>>::Failure(
                cropped_image.Error());
        } else {
            result = common::Result<std::optional<RegionSelectionResult>>::Success(
                std::optional<RegionSelectionResult>(
                    RegionSelectionResult{session_.selection, std::move(cropped_image.Value())}));
        }
    }

    cleanup();
    return result;
}

void RegionSelectionOverlay::InitializeSessionState() {
    session_.Reset(session_.start_with_full_selection);
    toolbar_.Hide();
}

common::Result<void> RegionSelectionOverlay::RegisterWindowClass() const {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = 0;
    window_class.lpfnWndProc = &RegionSelectionOverlay::WindowProc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kClassName;

    if (RegisterClassExW(&window_class) == 0) {
        const DWORD error_code = GetLastError();
        if (error_code != ERROR_CLASS_ALREADY_EXISTS) {
            return common::Result<void>::Failure(
                L"注册选区窗口失败。\n\n" + common::GetLastErrorMessage(error_code));
        }
    }

    return common::Result<void>::Success();
}

common::Result<void> RegionSelectionOverlay::CreateBackBuffer() {
    DestroyBackBuffer();

    if (window_ == nullptr || snapshot_ == nullptr) {
        return common::Result<void>::Failure(L"初始化选区缓冲失败。");
    }

    const SIZE buffer_size{snapshot_->image.Width(), snapshot_->image.Height()};
    return buffers_.Initialize(window_,
                               buffer_size,
                               DefaultOverlayTheme().overlay_alpha,
                               [this](HDC hdc) { PaintBaseImage(hdc); },
                               [this](HDC hdc, const RECT& bounds) { DrawInstructions(hdc, bounds); });
}

void RegionSelectionOverlay::DestroyBackBuffer() {
    pending_dirty_region_.Reset();
    buffers_.Destroy();
}

void RegionSelectionOverlay::RebuildSourceBuffers() {
    buffers_.RebuildSourceBuffers([this](HDC hdc) { PaintBaseImage(hdc); },
                                  [this](HDC hdc, const RECT& bounds) { DrawInstructions(hdc, bounds); });
}

void RegionSelectionOverlay::FinishSelection(bool accepted) {
    session_.Finish(accepted, CurrentSelection());
    if (GetCapture() == window_) {
        ReleaseCapture();
    }

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

void RegionSelectionOverlay::CommitSelection(const RECT& selection) {
    session_.CommitSelection(selection);
    UpdateToolbarLayout();
    RefreshFullFrame();
}

void RegionSelectionOverlay::ResetInteraction() {
    if (GetCapture() == window_) {
        ReleaseCapture();
    }

    session_.ResetInteraction();
}

void RegionSelectionOverlay::RefreshFullFrame() {
    if (window_ == nullptr) {
        return;
    }

    const SIZE buffer_size = buffers_.BufferSize();
    const RECT full_rect{0, 0, buffer_size.cx, buffer_size.cy};
    RefreshDirtyRect(full_rect);
}

void RegionSelectionOverlay::RefreshDirtyRect(const RECT& dirty_rect) {
    if (!common::HasArea(dirty_rect) || window_ == nullptr) {
        return;
    }

    const SIZE buffer_size = buffers_.BufferSize();
    const RECT clamped_dirty_rect =
        common::ClampRectToBounds(dirty_rect, buffer_size.cx, buffer_size.cy);
    if (!common::HasArea(clamped_dirty_rect)) {
        return;
    }

    if (!pending_dirty_region_) {
        pending_dirty_region_.Reset(CreateRectRgn(0, 0, 0, 0));
    }

    if (pending_dirty_region_) {
        common::UniqueRegion dirty_region(CreateRectRgnIndirect(&clamped_dirty_rect));
        if (dirty_region) {
            CombineRgn(pending_dirty_region_.Get(),
                       pending_dirty_region_.Get(),
                       dirty_region.Get(),
                       RGN_OR);
        }
    }

    InvalidateRect(window_, &clamped_dirty_rect, FALSE);
}

void RegionSelectionOverlay::FlushPendingDirtyRegion() {
    if (!pending_dirty_region_) {
        return;
    }

    RECT region_bounds{};
    if (GetRgnBox(pending_dirty_region_.Get(), &region_bounds) == NULLREGION) {
        return;
    }

    const DWORD region_size = GetRegionData(pending_dirty_region_.Get(), 0, nullptr);
    if (region_size == 0) {
        UpdateBackBuffer(region_bounds);
        SetRectRgn(pending_dirty_region_.Get(), 0, 0, 0, 0);
        return;
    }

    std::vector<BYTE> region_buffer(region_size);
    auto* region_data = reinterpret_cast<RGNDATA*>(region_buffer.data());
    if (GetRegionData(pending_dirty_region_.Get(), region_size, region_data) == 0) {
        UpdateBackBuffer(region_bounds);
        SetRectRgn(pending_dirty_region_.Get(), 0, 0, 0, 0);
        return;
    }

    const bool use_region_bounds = session_.interaction_mode == InteractionMode::Rectangle ||
                                   session_.interaction_mode == InteractionMode::Mosaic ||
                                   session_.interaction_mode == InteractionMode::Arrow;
    if (use_region_bounds) {
        UpdateBackBuffer(region_bounds);
        SetRectRgn(pending_dirty_region_.Get(), 0, 0, 0, 0);
        return;
    }

    const RECT* dirty_rects = reinterpret_cast<const RECT*>(region_data->Buffer);
    for (DWORD index = 0; index < region_data->rdh.nCount; ++index) {
        UpdateBackBuffer(dirty_rects[index]);
    }

    SetRectRgn(pending_dirty_region_.Get(), 0, 0, 0, 0);
}

void RegionSelectionOverlay::UpdateBackBuffer(const RECT& dirty_rect) {
    const RECT selection = CurrentSelection();
    const RECT* revealed_rect = HasSelection() ? &selection : nullptr;
    buffers_.UpdateBackBuffer(
        dirty_rect,
        revealed_rect,
        [this](HDC hdc, const RECT& client_rect) { PaintOverlay(hdc, client_rect); });
}

void RegionSelectionOverlay::InvalidateSelectionChange(const RECT& previous_selection,
                                                       bool previous_has_selection,
                                                       const RECT& previous_toolbar_rect,
                                                       bool previous_toolbar_visible) {
    if (window_ == nullptr) {
        return;
    }

    RECT client_rect{};
    GetClientRect(window_, &client_rect);

    RECT previous_visual_rect{};
    if (previous_has_selection) {
        previous_visual_rect = SelectionVisualRect(
            previous_selection, client_rect, previous_toolbar_rect, previous_toolbar_visible);
    }

    RECT current_visual_rect{};
    const bool current_has_selection = HasSelection();
    if (current_has_selection) {
        const RECT current_toolbar_rect = toolbar_.IsVisible() ? toolbar_.Bounds() : RECT{};
        current_visual_rect = SelectionVisualRect(
            CurrentSelection(), client_rect, current_toolbar_rect, toolbar_.IsVisible());
    }

    const RECT dirty_rect =
        UnionIfNeeded(previous_visual_rect, previous_has_selection, current_visual_rect, current_has_selection);
    RefreshDirtyRect(dirty_rect);
}

void RegionSelectionOverlay::InvalidatePreviewChange(const RECT& previous_preview,
                                                     bool previous_has_preview) {
    RECT previous_visual_preview = previous_preview;
    if (previous_has_preview) {
        previous_visual_preview = PreviewVisualRect(previous_preview);
    }

    RECT current_preview{};
    bool current_has_preview = false;

    switch (session_.interaction_mode) {
    case InteractionMode::Rectangle:
        current_preview = PreviewVisualRect(CurrentPreviewRect());
        current_has_preview = common::HasArea(current_preview);
        break;
    case InteractionMode::Mosaic:
        current_preview = PreviewVisualRect(CurrentPreviewRect());
        current_has_preview = common::HasArea(current_preview);
        break;
    case InteractionMode::Arrow:
        current_preview = PreviewVisualRect(ArrowPreviewRect());
        current_has_preview = common::HasArea(current_preview);
        break;
    default:
        break;
    }

    const RECT dirty_rect =
        UnionIfNeeded(previous_visual_preview, previous_has_preview, current_preview, current_has_preview);
    RefreshDirtyRect(dirty_rect);
}

void RegionSelectionOverlay::UpdateToolbarLayout() {
    if (!common::HasArea(session_.selection) || window_ == nullptr) {
        toolbar_.Hide();
        return;
    }

    RECT client_rect{};
    GetClientRect(window_, &client_rect);
    toolbar_.UpdateLayout(session_.selection, client_rect);
}

void RegionSelectionOverlay::SetActiveTool(EditTool tool) {
    session_.active_tool = tool;
    if (toolbar_.IsVisible()) {
        RefreshDirtyRect(toolbar_.Bounds());
    }
}

bool RegionSelectionOverlay::CanUndoLastEdit() const {
    return session_.CanUndo();
}

void RegionSelectionOverlay::PushUndoState() {
    session_.PushUndoState(working_image_, kMaxUndoSteps);
}

void RegionSelectionOverlay::UndoLastEdit() {
    if (!session_.UndoLastEdit(working_image_)) {
        return;
    }

    RebuildSourceBuffers();
    RefreshFullFrame();
}

common::Result<void> RegionSelectionOverlay::ApplyMarkupCommand(const editing::MarkupCommand& command) {
    PushUndoState();
    common::Result<void> result = editing::ImageMarkupService::ApplyCommand(working_image_, command);
    if (!result) {
        session_.DiscardLastUndo();
        return result;
    }

    RebuildSourceBuffers();
    return result;
}

common::Result<void> RegionSelectionOverlay::ApplyPendingRectangle() {
    const RECT rectangle_rect = CurrentPreviewRect();
    if (!common::HasArea(rectangle_rect)) {
        return common::Result<void>::Success();
    }

    editing::MarkupCommand command{};
    command.tool = editing::MarkupTool::Rectangle;
    command.clip_bounds = session_.selection;
    command.rect = rectangle_rect;
    command.color = renderer_.RectangleColor();
    command.thickness = renderer_.RectangleThickness();
    return ApplyMarkupCommand(command);
}

common::Result<void> RegionSelectionOverlay::ApplyPendingMosaic() {
    const RECT mosaic_rect = CurrentPreviewRect();
    if (!common::HasArea(mosaic_rect)) {
        return common::Result<void>::Success();
    }

    editing::MarkupCommand command{};
    command.tool = editing::MarkupTool::Mosaic;
    command.rect = mosaic_rect;
    command.block_size = renderer_.MosaicBlockSize();
    return ApplyMarkupCommand(command);
}

common::Result<void> RegionSelectionOverlay::ApplyPendingArrow() {
    if (session_.interaction_mode != InteractionMode::Arrow) {
        return common::Result<void>::Success();
    }

    editing::MarkupCommand command{};
    command.tool = editing::MarkupTool::Arrow;
    command.clip_bounds = session_.selection;
    command.start = session_.preview_start;
    command.end = session_.preview_current;
    command.color = renderer_.ArrowColor();
    command.thickness = renderer_.ArrowThickness();
    return ApplyMarkupCommand(command);
}

void RegionSelectionOverlay::ShowEditError(const std::wstring& error_message) const {
    if (window_ != nullptr && !error_message.empty()) {
        MessageBoxW(window_, error_message.c_str(), L"编辑失败", MB_OK | MB_ICONERROR);
    }
}

void RegionSelectionOverlay::PaintBaseImage(HDC hdc) const {
    renderer_.PaintBaseImage(hdc, working_image_);
}

void RegionSelectionOverlay::DrawInstructions(HDC hdc, const RECT& bounds) const {
    renderer_.DrawInstructions(hdc, bounds, session_.start_with_full_selection);
}

std::array<RECT, 8> RegionSelectionOverlay::BuildSelectionHandleRects(const RECT& selection) const {
    return {
        SelectionHandleRect(selection, SelectionAdjustHandle::TopLeft),
        SelectionHandleRect(selection, SelectionAdjustHandle::Top),
        SelectionHandleRect(selection, SelectionAdjustHandle::TopRight),
        SelectionHandleRect(selection, SelectionAdjustHandle::Right),
        SelectionHandleRect(selection, SelectionAdjustHandle::BottomRight),
        SelectionHandleRect(selection, SelectionAdjustHandle::Bottom),
        SelectionHandleRect(selection, SelectionAdjustHandle::BottomLeft),
        SelectionHandleRect(selection, SelectionAdjustHandle::Left),
    };
}

RegionSelectionRenderModel RegionSelectionOverlay::BuildRenderModel(const RECT& client_rect) const {
    RegionSelectionRenderModel model{};
    model.starts_with_full_selection = session_.start_with_full_selection;
    model.active_tool = session_.active_tool;
    model.can_undo = CanUndoLastEdit();

    if (HasSelection()) {
        const RECT selection = CurrentSelection();
        model.has_selection = true;
        model.selection = selection;
        model.show_selection_handles = session_.interaction_mode != InteractionMode::Selecting;
        model.selection_handle_rects = BuildSelectionHandleRects(selection);

        const RECT label_rect = SelectionLabelRect(selection, client_rect);
        if (common::HasArea(label_rect)) {
            model.has_selection_label = true;
            model.selection_label_rect = label_rect;
            model.selection_label_text =
                std::to_wstring(common::RectWidth(selection)) + L" x " +
                std::to_wstring(common::RectHeight(selection));
        }
    }

    if (session_.interaction_mode == InteractionMode::Rectangle) {
        model.rectangle_preview = CurrentPreviewRect();
        model.has_rectangle_preview = common::HasArea(model.rectangle_preview);
    } else if (session_.interaction_mode == InteractionMode::Mosaic) {
        model.mosaic_preview = CurrentPreviewRect();
        model.has_mosaic_preview = common::HasArea(model.mosaic_preview);
    } else if (session_.interaction_mode == InteractionMode::Arrow) {
        const RECT clip_rect = common::HasArea(session_.selection) ? session_.selection : client_rect;
        model.has_arrow_preview = common::HasArea(clip_rect);
        model.arrow_preview.clip_rect = clip_rect;
        model.arrow_preview.start =
            common::HasArea(clip_rect) ? ClampPointToRect(session_.preview_start, clip_rect)
                                       : session_.preview_start;
        model.arrow_preview.end =
            common::HasArea(clip_rect) ? ClampPointToRect(session_.preview_current, clip_rect)
                                       : session_.preview_current;
    }

    return model;
}

void RegionSelectionOverlay::PaintOverlay(HDC hdc, const RECT& client_rect) const {
    renderer_.PaintOverlay(hdc, client_rect, BuildRenderModel(client_rect), toolbar_);
}

LRESULT CALLBACK RegionSelectionOverlay::WindowProc(HWND hwnd,
                                                    UINT message,
                                                    WPARAM w_param,
                                                    LPARAM l_param) {
    RegionSelectionOverlay* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = static_cast<RegionSelectionOverlay*>(create_struct->lpCreateParams);
        self->window_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<RegionSelectionOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(message, w_param, l_param);
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT RegionSelectionOverlay::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        return HandleSetCursor() ? TRUE : FALSE;

    case WM_KEYDOWN:
        if (HandleKeyDown(w_param)) {
            return 0;
        }
        break;

    case WM_RBUTTONUP:
        FinishSelection(false);
        return 0;

    case WM_LBUTTONDOWN:
        HandleLeftButtonDown(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
        return 0;

    case WM_MOUSEMOVE:
        HandleMouseMove(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
        return 0;

    case WM_LBUTTONUP:
        HandleLeftButtonUp(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint_struct{};
        HDC hdc = BeginPaint(window_, &paint_struct);

        const int paint_width = common::RectWidth(paint_struct.rcPaint);
        const int paint_height = common::RectHeight(paint_struct.rcPaint);
        if (paint_width > 0 && paint_height > 0 && buffers_.HasBackBuffer()) {
            FlushPendingDirtyRegion();
            BitBlt(hdc,
                   paint_struct.rcPaint.left,
                   paint_struct.rcPaint.top,
                   paint_width,
                   paint_height,
                   buffers_.BackBufferDc(),
                   paint_struct.rcPaint.left,
                   paint_struct.rcPaint.top,
                   SRCCOPY);
        } else if (snapshot_ != nullptr) {
            RECT client_rect{};
            GetClientRect(window_, &client_rect);
            const RECT selection = CurrentSelection();
            const RECT* revealed_rect = HasSelection() ? &selection : nullptr;
            buffers_.PaintFrame(hdc,
                                client_rect,
                                revealed_rect,
                                [this](HDC target_hdc) { PaintBaseImage(target_hdc); },
                                [this](HDC target_hdc, const RECT& bounds) {
                                    DrawInstructions(target_hdc, bounds);
                                },
                                [this](HDC target_hdc, const RECT& paint_rect) {
                                    PaintOverlay(target_hdc, paint_rect);
                                });
        }

        EndPaint(window_, &paint_struct);
        return 0;
    }

    case WM_CLOSE:
        FinishSelection(false);
        return 0;

    case WM_DESTROY:
        DestroyBackBuffer();
        session_.finished = true;
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window_, message, w_param, l_param);
}

}  // namespace ui
