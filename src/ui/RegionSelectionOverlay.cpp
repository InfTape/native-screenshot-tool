#include "ui/RegionSelectionOverlay.h"

#include <Windows.h>
#include <WindowsX.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "common/RectUtils.h"
#include "common/Win32Error.h"
#include "editing/ImageMarkupService.h"

namespace {

constexpr BYTE kOverlayAlpha = 120;
constexpr int kInstructionHeight = 54;
constexpr int kInstructionMargin = 16;
constexpr COLORREF kPanelColor = RGB(24, 24, 24);
constexpr COLORREF kPanelBorderColor = RGB(255, 255, 255);
constexpr COLORREF kPanelTextColor = RGB(255, 255, 255);
constexpr COLORREF kPreviewAccentColor = RGB(242, 154, 28);
constexpr COLORREF kArrowColor = RGB(255, 99, 71);
constexpr int kArrowThickness = 4;
constexpr int kMosaicBlockSize = 14;
constexpr std::size_t kMaxUndoSteps = 20;
constexpr int kSelectionHandleSize = 8;
constexpr int kSelectionHandleHitPadding = 6;
constexpr int kMinimumSelectionSize = 8;
constexpr int kPreviewInvalidationPadding = 4;

void FillSolidRect(HDC hdc, const RECT& rect, COLORREF color) {
    if (!common::HasArea(rect)) {
        return;
    }

    SetDCBrushColor(hdc, color);
    FillRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

bool ApplyAlphaDim(HDC hdc, const RECT& rect, BYTE alpha) {
    if (!common::HasArea(rect)) {
        return true;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = 1;
    bitmap_info.bmiHeader.biHeight = -1;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(hdc, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        return false;
    }

    *static_cast<DWORD*>(bits) = 0x00000000;

    HDC memory_dc = CreateCompatibleDC(hdc);
    if (memory_dc == nullptr) {
        DeleteObject(bitmap);
        return false;
    }

    const HGDIOBJ previous_bitmap = SelectObject(memory_dc, bitmap);
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = alpha;

    const BOOL blended = AlphaBlend(hdc,
                                    rect.left,
                                    rect.top,
                                    common::RectWidth(rect),
                                    common::RectHeight(rect),
                                    memory_dc,
                                    0,
                                    0,
                                    1,
                                    1,
                                    blend);

    SelectObject(memory_dc, previous_bitmap);
    DeleteDC(memory_dc);
    DeleteObject(bitmap);
    return blended != FALSE;
}

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

bool IsPointInside(const RECT& rect, const POINT& point) {
    return common::HasArea(rect) && PtInRect(&rect, point) != FALSE;
}

void DrawArrowShape(HDC hdc, const POINT& start, const POINT& end, COLORREF color, int thickness) {
    const double dx = static_cast<double>(end.x - start.x);
    const double dy = static_cast<double>(end.y - start.y);
    const double length = std::hypot(dx, dy);
    if (length < 1.0) {
        return;
    }

    HPEN pen = CreatePen(PS_SOLID, thickness, color);
    const HGDIOBJ previous_pen = SelectObject(hdc, pen);
    const HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));

    MoveToEx(hdc, start.x, start.y, nullptr);
    LineTo(hdc, end.x, end.y);

    const double unit_x = dx / length;
    const double unit_y = dy / length;
    const double head_length = std::max(14.0, static_cast<double>(thickness) * 4.0);
    const double cos_angle = std::cos(0.5235987755982988);
    const double sin_angle = std::sin(0.5235987755982988);
    const double back_x = -unit_x * head_length;
    const double back_y = -unit_y * head_length;

    const POINT left{
        static_cast<LONG>(std::lround(end.x + (back_x * cos_angle) - (back_y * sin_angle))),
        static_cast<LONG>(std::lround(end.y + (back_x * sin_angle) + (back_y * cos_angle))),
    };
    const POINT right{
        static_cast<LONG>(std::lround(end.x + (back_x * cos_angle) + (back_y * sin_angle))),
        static_cast<LONG>(std::lround(end.y - (back_x * sin_angle) + (back_y * cos_angle))),
    };

    MoveToEx(hdc, end.x, end.y, nullptr);
    LineTo(hdc, left.x, left.y);
    MoveToEx(hdc, end.x, end.y, nullptr);
    LineTo(hdc, right.x, right.y);

    SelectObject(hdc, previous_brush);
    SelectObject(hdc, previous_pen);
    DeleteObject(pen);
}

}  // namespace

namespace ui {

RegionSelectionOverlay::RegionSelectionOverlay(HINSTANCE instance) : instance_(instance) {}

std::optional<RegionSelectionResult> RegionSelectionOverlay::SelectRegion(
    const capture::DesktopSnapshot& snapshot,
    std::wstring& error_message) {
    return RunSession(snapshot, false, error_message);
}

std::optional<RegionSelectionResult> RegionSelectionOverlay::EditImage(
    const capture::DesktopSnapshot& snapshot,
    std::wstring& error_message) {
    return RunSession(snapshot, true, error_message);
}

std::optional<RegionSelectionResult> RegionSelectionOverlay::RunSession(
    const capture::DesktopSnapshot& snapshot,
    bool start_with_full_selection,
    std::wstring& error_message) {
    snapshot_ = &snapshot;
    working_image_ = snapshot.image;
    session_starts_with_full_selection_ = start_with_full_selection;
    InitializeSessionState();

    if (!RegisterWindowClass()) {
        error_message = L"注册选区窗口失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
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
        error_message = L"创建选区窗口失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        snapshot_ = nullptr;
        working_image_ = capture::CapturedImage{};
        return std::nullopt;
    }

    if (!CreateBackBuffer(error_message)) {
        DestroyWindow(window_);
        window_ = nullptr;
        snapshot_ = nullptr;
        working_image_ = capture::CapturedImage{};
        return std::nullopt;
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

    MSG message{};
    while (!finished_) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            error_message = L"选区消息循环失败。\n\n" + common::GetLastErrorMessage(GetLastError());
            break;
        }

        if (result == 0) {
            finished_ = true;
            break;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    std::optional<RegionSelectionResult> result;
    if (accepted_ && common::HasArea(selection_)) {
        auto cropped_image = working_image_.Crop(selection_, error_message);
        if (cropped_image.has_value()) {
            result = RegionSelectionResult{selection_, std::move(*cropped_image)};
        }
    }

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    DestroyBackBuffer();
    snapshot_ = nullptr;
    working_image_ = capture::CapturedImage{};
    edit_history_.clear();
    session_starts_with_full_selection_ = false;
    toolbar_.Hide();
    return result;
}

void RegionSelectionOverlay::InitializeSessionState() {
    edit_history_.clear();
    finished_ = false;
    accepted_ = false;
    anchor_ = POINT{};
    current_ = POINT{};
    selection_ = RECT{};
    active_tool_ = EditTool::Select;
    interaction_mode_ = InteractionMode::None;
    adjust_handle_ = SelectionAdjustHandle::None;
    drag_start_ = POINT{};
    drag_origin_selection_ = RECT{};
    preview_start_ = POINT{};
    preview_current_ = POINT{};
    toolbar_.Hide();
}

bool RegionSelectionOverlay::RegisterWindowClass() const {
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
            SetLastError(error_code);
            return false;
        }
    }

    return true;
}

bool RegionSelectionOverlay::CreateBackBuffer(std::wstring& error_message) {
    DestroyBackBuffer();

    if (window_ == nullptr || snapshot_ == nullptr) {
        error_message = L"初始化选区缓冲失败。";
        return false;
    }

    HDC window_dc = GetDC(window_);
    if (window_dc == nullptr) {
        error_message = L"获取选区窗口 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return false;
    }

    if (!CreateBaseBuffer(window_dc, error_message)) {
        ReleaseDC(window_, window_dc);
        return false;
    }

    if (!CreateDimmedBuffer(window_dc, error_message)) {
        ReleaseDC(window_, window_dc);
        DestroyBackBuffer();
        return false;
    }

    back_buffer_dc_ = CreateCompatibleDC(window_dc);
    if (back_buffer_dc_ == nullptr) {
        error_message = L"创建选区后台缓冲 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        ReleaseDC(window_, window_dc);
        DestroyBackBuffer();
        return false;
    }

    back_buffer_bitmap_ =
        CreateCompatibleBitmap(window_dc, snapshot_->image.Width(), snapshot_->image.Height());
    ReleaseDC(window_, window_dc);

    if (back_buffer_bitmap_ == nullptr) {
        error_message = L"创建选区后台缓冲位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyBackBuffer();
        return false;
    }

    back_buffer_previous_bitmap_ = SelectObject(back_buffer_dc_, back_buffer_bitmap_);
    if (back_buffer_previous_bitmap_ == nullptr || back_buffer_previous_bitmap_ == HGDI_ERROR) {
        error_message = L"绑定选区后台缓冲位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyBackBuffer();
        return false;
    }

    back_buffer_size_.cx = snapshot_->image.Width();
    back_buffer_size_.cy = snapshot_->image.Height();

    const RECT full_rect{0, 0, back_buffer_size_.cx, back_buffer_size_.cy};
    UpdateBackBuffer(full_rect);
    return true;
}

void RegionSelectionOverlay::DestroyBackBuffer() {
    if (pending_dirty_region_ != nullptr) {
        DeleteObject(pending_dirty_region_);
        pending_dirty_region_ = nullptr;
    }

    if (back_buffer_dc_ != nullptr && back_buffer_previous_bitmap_ != nullptr &&
        back_buffer_previous_bitmap_ != HGDI_ERROR) {
        SelectObject(back_buffer_dc_, back_buffer_previous_bitmap_);
    }

    back_buffer_previous_bitmap_ = nullptr;

    if (back_buffer_bitmap_ != nullptr) {
        DeleteObject(back_buffer_bitmap_);
        back_buffer_bitmap_ = nullptr;
    }

    if (back_buffer_dc_ != nullptr) {
        DeleteDC(back_buffer_dc_);
        back_buffer_dc_ = nullptr;
    }

    DestroyDimmedBuffer();
    DestroyBaseBuffer();
    back_buffer_size_ = SIZE{};
}

bool RegionSelectionOverlay::CreateBaseBuffer(HDC reference_dc, std::wstring& error_message) {
    base_buffer_dc_ = CreateCompatibleDC(reference_dc);
    if (base_buffer_dc_ == nullptr) {
        error_message = L"创建基础底图 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return false;
    }

    base_buffer_bitmap_ =
        CreateCompatibleBitmap(reference_dc, snapshot_->image.Width(), snapshot_->image.Height());
    if (base_buffer_bitmap_ == nullptr) {
        error_message = L"创建基础底图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyBaseBuffer();
        return false;
    }

    base_buffer_previous_bitmap_ = SelectObject(base_buffer_dc_, base_buffer_bitmap_);
    if (base_buffer_previous_bitmap_ == nullptr || base_buffer_previous_bitmap_ == HGDI_ERROR) {
        error_message = L"绑定基础底图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyBaseBuffer();
        return false;
    }

    RenderBaseBuffer();
    return true;
}

void RegionSelectionOverlay::DestroyBaseBuffer() {
    if (base_buffer_dc_ != nullptr && base_buffer_previous_bitmap_ != nullptr &&
        base_buffer_previous_bitmap_ != HGDI_ERROR) {
        SelectObject(base_buffer_dc_, base_buffer_previous_bitmap_);
    }

    base_buffer_previous_bitmap_ = nullptr;

    if (base_buffer_bitmap_ != nullptr) {
        DeleteObject(base_buffer_bitmap_);
        base_buffer_bitmap_ = nullptr;
    }

    if (base_buffer_dc_ != nullptr) {
        DeleteDC(base_buffer_dc_);
        base_buffer_dc_ = nullptr;
    }
}

bool RegionSelectionOverlay::CreateDimmedBuffer(HDC reference_dc, std::wstring& error_message) {
    dimmed_buffer_dc_ = CreateCompatibleDC(reference_dc);
    if (dimmed_buffer_dc_ == nullptr) {
        error_message = L"创建暗化底图 DC 失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        return false;
    }

    dimmed_buffer_bitmap_ =
        CreateCompatibleBitmap(reference_dc, snapshot_->image.Width(), snapshot_->image.Height());
    if (dimmed_buffer_bitmap_ == nullptr) {
        error_message = L"创建暗化底图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyDimmedBuffer();
        return false;
    }

    dimmed_buffer_previous_bitmap_ = SelectObject(dimmed_buffer_dc_, dimmed_buffer_bitmap_);
    if (dimmed_buffer_previous_bitmap_ == nullptr || dimmed_buffer_previous_bitmap_ == HGDI_ERROR) {
        error_message = L"绑定暗化底图位图失败。\n\n" + common::GetLastErrorMessage(GetLastError());
        DestroyDimmedBuffer();
        return false;
    }

    RenderDimmedBuffer();
    return true;
}

void RegionSelectionOverlay::DestroyDimmedBuffer() {
    if (dimmed_buffer_dc_ != nullptr && dimmed_buffer_previous_bitmap_ != nullptr &&
        dimmed_buffer_previous_bitmap_ != HGDI_ERROR) {
        SelectObject(dimmed_buffer_dc_, dimmed_buffer_previous_bitmap_);
    }

    dimmed_buffer_previous_bitmap_ = nullptr;

    if (dimmed_buffer_bitmap_ != nullptr) {
        DeleteObject(dimmed_buffer_bitmap_);
        dimmed_buffer_bitmap_ = nullptr;
    }

    if (dimmed_buffer_dc_ != nullptr) {
        DeleteDC(dimmed_buffer_dc_);
        dimmed_buffer_dc_ = nullptr;
    }
}

void RegionSelectionOverlay::RenderBaseBuffer() {
    if (base_buffer_dc_ != nullptr) {
        PaintBaseImage(base_buffer_dc_);
    }
}

void RegionSelectionOverlay::RenderDimmedBuffer() {
    if (dimmed_buffer_dc_ == nullptr || base_buffer_dc_ == nullptr || snapshot_ == nullptr) {
        return;
    }

    BitBlt(dimmed_buffer_dc_,
           0,
           0,
           snapshot_->image.Width(),
           snapshot_->image.Height(),
           base_buffer_dc_,
           0,
           0,
           SRCCOPY);

    const RECT full_rect{0, 0, snapshot_->image.Width(), snapshot_->image.Height()};
    ApplyAlphaDim(dimmed_buffer_dc_, full_rect, kOverlayAlpha);
    DrawInstructions(dimmed_buffer_dc_, full_rect);
}

void RegionSelectionOverlay::RebuildSourceBuffers() {
    RenderBaseBuffer();
    RenderDimmedBuffer();
}

RECT RegionSelectionOverlay::CurrentSelection() const {
    if (interaction_mode_ == InteractionMode::Selecting) {
        RECT normalized = common::NormalizeRect(anchor_, current_);
        return common::ClampRectToBounds(normalized, snapshot_->image.Width(), snapshot_->image.Height());
    }

    return selection_;
}

bool RegionSelectionOverlay::HasSelection() const {
    return common::HasArea(CurrentSelection());
}

RECT RegionSelectionOverlay::CurrentPreviewRect() const {
    if (interaction_mode_ != InteractionMode::Mosaic) {
        return RECT{};
    }

    const RECT preview = common::NormalizeRect(preview_start_, preview_current_);
    RECT clipped{};
    IntersectRect(&clipped, &preview, &selection_);
    return clipped;
}

RECT RegionSelectionOverlay::PreviewVisualRect(const RECT& preview_rect) const {
    if (!common::HasArea(preview_rect)) {
        return RECT{};
    }

    RECT visual_rect = preview_rect;
    InflateRect(&visual_rect, kPreviewInvalidationPadding, kPreviewInvalidationPadding);
    return common::ClampRectToBounds(visual_rect, back_buffer_size_.cx, back_buffer_size_.cy);
}

bool RegionSelectionOverlay::HasPreviewRect() const {
    return common::HasArea(CurrentPreviewRect());
}

RegionSelectionOverlay::SelectionAdjustHandle RegionSelectionOverlay::HitTestSelectionHandle(
    const POINT& point) const {
    if (!common::HasArea(selection_)) {
        return SelectionAdjustHandle::None;
    }

    static constexpr SelectionAdjustHandle kResizeHandles[] = {
        SelectionAdjustHandle::TopLeft,
        SelectionAdjustHandle::Top,
        SelectionAdjustHandle::TopRight,
        SelectionAdjustHandle::Right,
        SelectionAdjustHandle::BottomRight,
        SelectionAdjustHandle::Bottom,
        SelectionAdjustHandle::BottomLeft,
        SelectionAdjustHandle::Left,
    };

    for (const auto handle : kResizeHandles) {
        RECT hit_rect = SelectionHandleRect(selection_, handle);
        InflateRect(&hit_rect, kSelectionHandleHitPadding, kSelectionHandleHitPadding);
        if (PtInRect(&hit_rect, point) != FALSE) {
            return handle;
        }
    }

    if (PtInRect(&selection_, point) != FALSE) {
        return SelectionAdjustHandle::Move;
    }

    return SelectionAdjustHandle::None;
}

RECT RegionSelectionOverlay::SelectionHandleRect(const RECT& selection,
                                                 SelectionAdjustHandle handle) const {
    if (!common::HasArea(selection)) {
        return RECT{};
    }

    const LONG center_x = (selection.left + selection.right) / 2;
    const LONG center_y = (selection.top + selection.bottom) / 2;
    const LONG half = kSelectionHandleSize / 2;
    auto make_handle_rect = [half](LONG center_x_value, LONG center_y_value) {
        return RECT{center_x_value - half,
                    center_y_value - half,
                    center_x_value + half,
                    center_y_value + half};
    };

    switch (handle) {
    case SelectionAdjustHandle::Left:
        return make_handle_rect(selection.left, center_y);
    case SelectionAdjustHandle::Top:
        return make_handle_rect(center_x, selection.top);
    case SelectionAdjustHandle::Right:
        return make_handle_rect(selection.right, center_y);
    case SelectionAdjustHandle::Bottom:
        return make_handle_rect(center_x, selection.bottom);
    case SelectionAdjustHandle::TopLeft:
        return make_handle_rect(selection.left, selection.top);
    case SelectionAdjustHandle::TopRight:
        return make_handle_rect(selection.right, selection.top);
    case SelectionAdjustHandle::BottomLeft:
        return make_handle_rect(selection.left, selection.bottom);
    case SelectionAdjustHandle::BottomRight:
        return make_handle_rect(selection.right, selection.bottom);
    case SelectionAdjustHandle::Move:
        return selection;
    case SelectionAdjustHandle::None:
    default:
        return RECT{};
    }
}

RECT RegionSelectionOverlay::SelectionLabelRect(const RECT& selection, const RECT& client_rect) const {
    if (!common::HasArea(selection)) {
        return RECT{};
    }

    RECT label_rect{
        selection.left,
        std::max(client_rect.top, selection.top - 32),
        selection.left + 180,
        selection.top,
    };
    if (common::RectHeight(label_rect) < 24) {
        label_rect.top = selection.bottom;
        label_rect.bottom = std::min(client_rect.bottom, selection.bottom + 32);
    }

    label_rect.left = std::clamp(label_rect.left, client_rect.left, client_rect.right);
    label_rect.right = std::clamp(label_rect.right, client_rect.left, client_rect.right);
    label_rect.top = std::clamp(label_rect.top, client_rect.top, client_rect.bottom);
    label_rect.bottom = std::clamp(label_rect.bottom, client_rect.top, client_rect.bottom);
    return label_rect;
}

RECT RegionSelectionOverlay::SelectionVisualRect(const RECT& selection,
                                                 const RECT& client_rect,
                                                 const RECT& toolbar_rect,
                                                 bool include_toolbar) const {
    if (!common::HasArea(selection)) {
        return RECT{};
    }

    RECT selection_bounds = selection;
    InflateRect(&selection_bounds, kSelectionHandleSize + 3, kSelectionHandleSize + 3);
    selection_bounds.left = std::clamp(selection_bounds.left, client_rect.left, client_rect.right);
    selection_bounds.top = std::clamp(selection_bounds.top, client_rect.top, client_rect.bottom);
    selection_bounds.right =
        std::clamp(selection_bounds.right, client_rect.left, client_rect.right);
    selection_bounds.bottom =
        std::clamp(selection_bounds.bottom, client_rect.top, client_rect.bottom);

    const RECT label_rect = SelectionLabelRect(selection, client_rect);

    RECT selection_and_label{};
    UnionRect(&selection_and_label, &selection_bounds, &label_rect);
    if (!include_toolbar || !common::HasArea(toolbar_rect)) {
        return selection_and_label;
    }

    RECT visual_rect{};
    UnionRect(&visual_rect, &selection_and_label, &toolbar_rect);
    return visual_rect;
}

RECT RegionSelectionOverlay::ArrowPreviewRect() const {
    if (interaction_mode_ != InteractionMode::Arrow) {
        return RECT{};
    }

    RECT bounds = common::NormalizeRect(preview_start_, preview_current_);
    InflateRect(&bounds, 28, 28);
    return common::ClampRectToBounds(bounds, snapshot_->image.Width(), snapshot_->image.Height());
}

RECT RegionSelectionOverlay::AdjustSelectionFromDrag(const POINT& point) const {
    RECT adjusted = drag_origin_selection_;
    const LONG dx = point.x - drag_start_.x;
    const LONG dy = point.y - drag_start_.y;
    const LONG minimum_size = static_cast<LONG>(kMinimumSelectionSize);
    const LONG max_width = static_cast<LONG>(snapshot_->image.Width());
    const LONG max_height = static_cast<LONG>(snapshot_->image.Height());

    switch (adjust_handle_) {
    case SelectionAdjustHandle::Move: {
        OffsetRect(&adjusted, dx, dy);
        const LONG width = adjusted.right - adjusted.left;
        const LONG height = adjusted.bottom - adjusted.top;

        if (adjusted.left < 0) {
            adjusted.left = 0;
            adjusted.right = width;
        }
        if (adjusted.top < 0) {
            adjusted.top = 0;
            adjusted.bottom = height;
        }
        if (adjusted.right > max_width) {
            adjusted.right = max_width;
            adjusted.left = adjusted.right - width;
        }
        if (adjusted.bottom > max_height) {
            adjusted.bottom = max_height;
            adjusted.top = adjusted.bottom - height;
        }
        break;
    }
    case SelectionAdjustHandle::Left:
        adjusted.left = std::clamp(point.x, 0L, adjusted.right - minimum_size);
        break;
    case SelectionAdjustHandle::Top:
        adjusted.top = std::clamp(point.y, 0L, adjusted.bottom - minimum_size);
        break;
    case SelectionAdjustHandle::Right:
        adjusted.right = std::clamp(point.x, adjusted.left + minimum_size, max_width);
        break;
    case SelectionAdjustHandle::Bottom:
        adjusted.bottom = std::clamp(point.y, adjusted.top + minimum_size, max_height);
        break;
    case SelectionAdjustHandle::TopLeft:
        adjusted.left = std::clamp(point.x, 0L, adjusted.right - minimum_size);
        adjusted.top = std::clamp(point.y, 0L, adjusted.bottom - minimum_size);
        break;
    case SelectionAdjustHandle::TopRight:
        adjusted.right = std::clamp(point.x, adjusted.left + minimum_size, max_width);
        adjusted.top = std::clamp(point.y, 0L, adjusted.bottom - minimum_size);
        break;
    case SelectionAdjustHandle::BottomLeft:
        adjusted.left = std::clamp(point.x, 0L, adjusted.right - minimum_size);
        adjusted.bottom = std::clamp(point.y, adjusted.top + minimum_size, max_height);
        break;
    case SelectionAdjustHandle::BottomRight:
        adjusted.right = std::clamp(point.x, adjusted.left + minimum_size, max_width);
        adjusted.bottom = std::clamp(point.y, adjusted.top + minimum_size, max_height);
        break;
    case SelectionAdjustHandle::None:
    default:
        break;
    }

    return adjusted;
}

void RegionSelectionOverlay::FinishSelection(bool accepted) {
    accepted_ = accepted;
    selection_ = CurrentSelection();
    ResetInteraction();
    finished_ = true;

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

void RegionSelectionOverlay::CommitSelection(const RECT& selection) {
    selection_ = selection;
    active_tool_ = EditTool::Select;
    UpdateToolbarLayout();
    RefreshFullFrame();
}

void RegionSelectionOverlay::ResetInteraction() {
    if (GetCapture() == window_) {
        ReleaseCapture();
    }

    interaction_mode_ = InteractionMode::None;
    adjust_handle_ = SelectionAdjustHandle::None;
    drag_start_ = POINT{};
    drag_origin_selection_ = RECT{};
    preview_start_ = POINT{};
    preview_current_ = POINT{};
}

void RegionSelectionOverlay::RefreshFullFrame() {
    if (window_ == nullptr) {
        return;
    }

    const RECT full_rect{0, 0, back_buffer_size_.cx, back_buffer_size_.cy};
    RefreshDirtyRect(full_rect);
}

void RegionSelectionOverlay::RefreshDirtyRect(const RECT& dirty_rect) {
    if (!common::HasArea(dirty_rect) || window_ == nullptr) {
        return;
    }

    const RECT clamped_dirty_rect =
        common::ClampRectToBounds(dirty_rect, back_buffer_size_.cx, back_buffer_size_.cy);
    if (!common::HasArea(clamped_dirty_rect)) {
        return;
    }

    if (pending_dirty_region_ == nullptr) {
        pending_dirty_region_ = CreateRectRgn(0, 0, 0, 0);
    }

    if (pending_dirty_region_ != nullptr) {
        HRGN dirty_region = CreateRectRgnIndirect(&clamped_dirty_rect);
        if (dirty_region != nullptr) {
            CombineRgn(pending_dirty_region_, pending_dirty_region_, dirty_region, RGN_OR);
            DeleteObject(dirty_region);
        }
    }

    InvalidateRect(window_, &clamped_dirty_rect, FALSE);
}

void RegionSelectionOverlay::FlushPendingDirtyRegion() {
    if (pending_dirty_region_ == nullptr) {
        return;
    }

    RECT region_bounds{};
    if (GetRgnBox(pending_dirty_region_, &region_bounds) == NULLREGION) {
        return;
    }

    const DWORD region_size = GetRegionData(pending_dirty_region_, 0, nullptr);
    if (region_size == 0) {
        UpdateBackBuffer(region_bounds);
        SetRectRgn(pending_dirty_region_, 0, 0, 0, 0);
        return;
    }

    std::vector<BYTE> region_buffer(region_size);
    auto* region_data = reinterpret_cast<RGNDATA*>(region_buffer.data());
    if (GetRegionData(pending_dirty_region_, region_size, region_data) == 0) {
        UpdateBackBuffer(region_bounds);
        SetRectRgn(pending_dirty_region_, 0, 0, 0, 0);
        return;
    }

    const bool use_region_bounds =
        interaction_mode_ == InteractionMode::Mosaic || interaction_mode_ == InteractionMode::Arrow;
    if (use_region_bounds) {
        UpdateBackBuffer(region_bounds);
        SetRectRgn(pending_dirty_region_, 0, 0, 0, 0);
        return;
    }

    const RECT* dirty_rects = reinterpret_cast<const RECT*>(region_data->Buffer);
    for (DWORD index = 0; index < region_data->rdh.nCount; ++index) {
        UpdateBackBuffer(dirty_rects[index]);
    }

    SetRectRgn(pending_dirty_region_, 0, 0, 0, 0);
}

void RegionSelectionOverlay::UpdateBackBuffer(const RECT& dirty_rect) {
    if (back_buffer_dc_ == nullptr || base_buffer_dc_ == nullptr || dimmed_buffer_dc_ == nullptr) {
        return;
    }

    const RECT clamped_dirty_rect =
        common::ClampRectToBounds(dirty_rect, back_buffer_size_.cx, back_buffer_size_.cy);
    if (!common::HasArea(clamped_dirty_rect)) {
        return;
    }

    BitBlt(back_buffer_dc_,
           clamped_dirty_rect.left,
           clamped_dirty_rect.top,
           common::RectWidth(clamped_dirty_rect),
           common::RectHeight(clamped_dirty_rect),
           dimmed_buffer_dc_,
           clamped_dirty_rect.left,
           clamped_dirty_rect.top,
           SRCCOPY);

    if (HasSelection()) {
        const RECT selection = CurrentSelection();
        RECT visible_selection{};
        if (IntersectRect(&visible_selection, &selection, &clamped_dirty_rect)) {
            BitBlt(back_buffer_dc_,
                   visible_selection.left,
                   visible_selection.top,
                   common::RectWidth(visible_selection),
                   common::RectHeight(visible_selection),
                   base_buffer_dc_,
                   visible_selection.left,
                   visible_selection.top,
                   SRCCOPY);
        }
    }

    const RECT client_rect{0, 0, back_buffer_size_.cx, back_buffer_size_.cy};
    const int saved_dc = SaveDC(back_buffer_dc_);
    if (saved_dc != 0) {
        IntersectClipRect(back_buffer_dc_,
                          clamped_dirty_rect.left,
                          clamped_dirty_rect.top,
                          clamped_dirty_rect.right,
                          clamped_dirty_rect.bottom);
    }

    PaintOverlay(back_buffer_dc_, client_rect);

    if (saved_dc != 0) {
        RestoreDC(back_buffer_dc_, saved_dc);
    }
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
        current_visual_rect =
            SelectionVisualRect(CurrentSelection(), client_rect, current_toolbar_rect, toolbar_.IsVisible());
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

    switch (interaction_mode_) {
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
        UnionIfNeeded(previous_visual_preview,
                      previous_has_preview,
                      current_preview,
                      current_has_preview);
    RefreshDirtyRect(dirty_rect);
}

void RegionSelectionOverlay::UpdateToolbarLayout() {
    if (!common::HasArea(selection_) || window_ == nullptr) {
        toolbar_.Hide();
        return;
    }

    RECT client_rect{};
    GetClientRect(window_, &client_rect);
    toolbar_.UpdateLayout(selection_, client_rect);
}

void RegionSelectionOverlay::SetActiveTool(EditTool tool) {
    active_tool_ = tool;
    if (toolbar_.IsVisible()) {
        RefreshDirtyRect(toolbar_.Bounds());
    }
}

SelectionToolbarAction RegionSelectionOverlay::ActiveToolbarAction() const {
    switch (active_tool_) {
    case EditTool::Select:
        return SelectionToolbarAction::Select;
    case EditTool::Mosaic:
        return SelectionToolbarAction::Mosaic;
    case EditTool::Arrow:
        return SelectionToolbarAction::Arrow;
    }

    return SelectionToolbarAction::None;
}

bool RegionSelectionOverlay::CanUndoLastEdit() const {
    return !edit_history_.empty();
}

void RegionSelectionOverlay::PushUndoState() {
    if (edit_history_.size() >= kMaxUndoSteps) {
        edit_history_.erase(edit_history_.begin());
    }

    edit_history_.push_back(working_image_);
}

void RegionSelectionOverlay::UndoLastEdit() {
    if (!CanUndoLastEdit()) {
        return;
    }

    working_image_ = std::move(edit_history_.back());
    edit_history_.pop_back();
    RebuildSourceBuffers();
    RefreshFullFrame();
}

bool RegionSelectionOverlay::ApplyPendingMosaic(std::wstring& error_message) {
    const RECT mosaic_rect = CurrentPreviewRect();
    if (!common::HasArea(mosaic_rect)) {
        return true;
    }

    PushUndoState();
    if (!editing::ImageMarkupService::ApplyMosaic(working_image_,
                                                  mosaic_rect,
                                                  kMosaicBlockSize,
                                                  error_message)) {
        if (!edit_history_.empty()) {
            edit_history_.pop_back();
        }
        return false;
    }

    RebuildSourceBuffers();
    return true;
}

bool RegionSelectionOverlay::ApplyPendingArrow(std::wstring& error_message) {
    if (interaction_mode_ != InteractionMode::Arrow) {
        return true;
    }

    PushUndoState();
    if (!editing::ImageMarkupService::DrawArrow(working_image_,
                                                selection_,
                                                preview_start_,
                                                preview_current_,
                                                kArrowColor,
                                                kArrowThickness,
                                                error_message)) {
        if (!edit_history_.empty()) {
            edit_history_.pop_back();
        }
        return false;
    }

    RebuildSourceBuffers();
    return true;
}

void RegionSelectionOverlay::ShowEditError(const std::wstring& error_message) const {
    if (window_ != nullptr && !error_message.empty()) {
        MessageBoxW(window_, error_message.c_str(), L"编辑失败", MB_OK | MB_ICONERROR);
    }
}

void RegionSelectionOverlay::PaintBaseImage(HDC hdc) const {
    const BITMAPINFO bitmap_info = working_image_.CreateBitmapInfo();
    SetDIBitsToDevice(hdc,
                      0,
                      0,
                      working_image_.Width(),
                      working_image_.Height(),
                      0,
                      0,
                      0,
                      working_image_.Height(),
                      working_image_.Pixels().data(),
                      &bitmap_info,
                      DIB_RGB_COLORS);
}

void RegionSelectionOverlay::PaintOverlay(HDC hdc, const RECT& client_rect) const {
    if (HasSelection()) {
        const RECT selection = CurrentSelection();
        DrawSelectionBorder(hdc, selection);
        DrawSelectionLabel(hdc, selection, client_rect);
    }

    if (interaction_mode_ == InteractionMode::Mosaic) {
        DrawMosaicPreview(hdc);
    } else if (interaction_mode_ == InteractionMode::Arrow) {
        DrawArrowPreview(hdc);
    }

    toolbar_.Paint(hdc, ActiveToolbarAction(), CanUndoLastEdit());
}

void RegionSelectionOverlay::PaintFrame(HDC hdc, const RECT& client_rect) const {
    if (dimmed_buffer_dc_ != nullptr) {
        BitBlt(hdc,
               0,
               0,
               common::RectWidth(client_rect),
               common::RectHeight(client_rect),
               dimmed_buffer_dc_,
               0,
               0,
               SRCCOPY);
    } else {
        PaintBaseImage(hdc);
        ApplyAlphaDim(hdc, client_rect, kOverlayAlpha);
        DrawInstructions(hdc, client_rect);
    }

    if (HasSelection() && base_buffer_dc_ != nullptr) {
        const RECT selection = CurrentSelection();
        BitBlt(hdc,
               selection.left,
               selection.top,
               common::RectWidth(selection),
               common::RectHeight(selection),
               base_buffer_dc_,
               selection.left,
               selection.top,
               SRCCOPY);
    }

    PaintOverlay(hdc, client_rect);
}

void RegionSelectionOverlay::DrawInstructions(HDC hdc, const RECT& bounds) const {
    RECT instruction_rect = bounds;
    instruction_rect.left += kInstructionMargin;
    instruction_rect.top += kInstructionMargin;
    instruction_rect.right =
        std::min<LONG>(instruction_rect.left + 560, bounds.right - kInstructionMargin);
    instruction_rect.bottom = instruction_rect.top + kInstructionHeight;

    FillSolidRect(hdc, instruction_rect, kPanelColor);
    FrameRect(hdc, &instruction_rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    RECT text_rect = instruction_rect;
    InflateRect(&text_rect, -12, -8);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kPanelTextColor);
    if (session_starts_with_full_selection_) {
        DrawTextW(
            hdc,
            L"\u53ef\u76f4\u63a5\u4f7f\u7528\u9a6c\u8d5b\u514b\u6216\u7bad\u5934\u6807\u6ce8\uff0c"
            L"\u4e5f\u53ef\u62d6\u52a8\u6216\u8c03\u6574\u8303\u56f4\u3002"
            L"\u6309 Enter \u5b8c\u6210\uff0c\u6309 Esc \u6216\u53f3\u952e\u53d6\u6d88\u3002",
            -1,
            &text_rect,
            DT_LEFT | DT_VCENTER | DT_WORDBREAK);
        return;
    }
    DrawTextW(hdc,
              L"拖动鼠标框选截图区域，松开后可继续做马赛克或箭头标注。按 Enter 完成，按 Esc 或右键取消。",
              -1,
              &text_rect,
              DT_LEFT | DT_VCENTER | DT_WORDBREAK);
}

void RegionSelectionOverlay::DrawSelectionBorder(HDC hdc, const RECT& selection) const {
    HPEN border_pen = CreatePen(PS_SOLID, 2, kPanelBorderColor);
    const HGDIOBJ previous_pen = SelectObject(hdc, border_pen);
    const HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, selection.left, selection.top, selection.right, selection.bottom);
    SelectObject(hdc, previous_brush);
    SelectObject(hdc, previous_pen);
    DeleteObject(border_pen);

    if (interaction_mode_ != InteractionMode::Selecting) {
        DrawSelectionHandles(hdc, selection);
    }
}

void RegionSelectionOverlay::DrawSelectionHandles(HDC hdc, const RECT& selection) const {
    static constexpr SelectionAdjustHandle kHandles[] = {
        SelectionAdjustHandle::TopLeft,
        SelectionAdjustHandle::Top,
        SelectionAdjustHandle::TopRight,
        SelectionAdjustHandle::Right,
        SelectionAdjustHandle::BottomRight,
        SelectionAdjustHandle::Bottom,
        SelectionAdjustHandle::BottomLeft,
        SelectionAdjustHandle::Left,
    };

    HBRUSH fill_brush = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH border_brush = CreateSolidBrush(RGB(32, 32, 32));
    for (const auto handle : kHandles) {
        const RECT handle_rect = SelectionHandleRect(selection, handle);
        FillRect(hdc, &handle_rect, fill_brush);
        FrameRect(hdc, &handle_rect, border_brush);
    }
    DeleteObject(border_brush);
    DeleteObject(fill_brush);
}

void RegionSelectionOverlay::DrawSelectionLabel(HDC hdc,
                                                const RECT& selection,
                                                const RECT& client_rect) const {
    RECT label_rect = SelectionLabelRect(selection, client_rect);
    if (!common::HasArea(label_rect)) {
        return;
    }

    FillSolidRect(hdc, label_rect, kPanelColor);
    FrameRect(hdc, &label_rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    RECT text_rect = label_rect;
    InflateRect(&text_rect, -8, -6);
    const std::wstring size_text =
        std::to_wstring(common::RectWidth(selection)) + L" x " +
        std::to_wstring(common::RectHeight(selection));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kPanelTextColor);
    DrawTextW(hdc, size_text.c_str(), -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void RegionSelectionOverlay::DrawMosaicPreview(HDC hdc) const {
    const RECT preview_rect = CurrentPreviewRect();
    if (!common::HasArea(preview_rect)) {
        return;
    }

    HPEN border_pen = CreatePen(PS_SOLID, 2, kPreviewAccentColor);
    const HGDIOBJ previous_pen = SelectObject(hdc, border_pen);
    const HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, preview_rect.left, preview_rect.top, preview_rect.right, preview_rect.bottom);
    SelectObject(hdc, previous_brush);
    SelectObject(hdc, previous_pen);
    DeleteObject(border_pen);
}

void RegionSelectionOverlay::DrawArrowPreview(HDC hdc) const {
    if (interaction_mode_ != InteractionMode::Arrow) {
        return;
    }

    DrawArrowShape(hdc, preview_start_, preview_current_, kArrowColor, kArrowThickness);
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
        if (window_ != nullptr) {
            POINT cursor_point{};
            GetCursorPos(&cursor_point);
            ScreenToClient(window_, &cursor_point);

            if (toolbar_.HitTest(cursor_point) != SelectionToolbarAction::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }

            auto cursor_for_handle = [](SelectionAdjustHandle handle) {
                switch (handle) {
                case SelectionAdjustHandle::Move:
                    return IDC_SIZEALL;
                case SelectionAdjustHandle::Left:
                case SelectionAdjustHandle::Right:
                    return IDC_SIZEWE;
                case SelectionAdjustHandle::Top:
                case SelectionAdjustHandle::Bottom:
                    return IDC_SIZENS;
                case SelectionAdjustHandle::TopLeft:
                case SelectionAdjustHandle::BottomRight:
                    return IDC_SIZENWSE;
                case SelectionAdjustHandle::TopRight:
                case SelectionAdjustHandle::BottomLeft:
                    return IDC_SIZENESW;
                case SelectionAdjustHandle::None:
                default:
                    return IDC_CROSS;
                }
            };

            if (active_tool_ == EditTool::Select && HasSelection()) {
                const SelectionAdjustHandle handle = interaction_mode_ == InteractionMode::Adjusting
                                                         ? adjust_handle_
                                                         : HitTestSelectionHandle(cursor_point);
                SetCursor(LoadCursorW(nullptr, cursor_for_handle(handle)));
                return TRUE;
            }
        }

        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return TRUE;

    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE) {
            FinishSelection(false);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) < 0) && (w_param == 'Z') && CanUndoLastEdit() &&
            interaction_mode_ != InteractionMode::Selecting) {
            ResetInteraction();
            UndoLastEdit();
            return 0;
        }
        if (w_param == VK_RETURN && common::HasArea(selection_)) {
            FinishSelection(true);
            return 0;
        }
        break;

    case WM_RBUTTONUP:
        FinishSelection(false);
        return 0;

    case WM_LBUTTONDOWN: {
        const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        const SelectionToolbarAction toolbar_action = toolbar_.HitTest(point);
        if (toolbar_action != SelectionToolbarAction::None) {
            switch (toolbar_action) {
            case SelectionToolbarAction::Select:
                SetActiveTool(EditTool::Select);
                return 0;
            case SelectionToolbarAction::Mosaic:
                SetActiveTool(EditTool::Mosaic);
                return 0;
            case SelectionToolbarAction::Arrow:
                SetActiveTool(EditTool::Arrow);
                return 0;
            case SelectionToolbarAction::Undo:
                if (CanUndoLastEdit()) {
                    ResetInteraction();
                    UndoLastEdit();
                }
                return 0;
            case SelectionToolbarAction::Confirm:
                FinishSelection(true);
                return 0;
            case SelectionToolbarAction::Cancel:
                FinishSelection(false);
                return 0;
            case SelectionToolbarAction::None:
                break;
            }
        }

        if (active_tool_ == EditTool::Select) {
            if (common::HasArea(selection_)) {
                const SelectionAdjustHandle hit_handle = HitTestSelectionHandle(point);
                if (hit_handle != SelectionAdjustHandle::None) {
                    adjust_handle_ = hit_handle;
                    drag_start_ = point;
                    drag_origin_selection_ = selection_;
                    interaction_mode_ = InteractionMode::Adjusting;
                    SetCapture(window_);
                    return 0;
                }
            }

            const RECT previous_selection = CurrentSelection();
            const bool previous_has_selection = HasSelection();
            const RECT previous_toolbar_rect = toolbar_.IsVisible() ? toolbar_.Bounds() : RECT{};
            const bool previous_toolbar_visible = toolbar_.IsVisible();
            toolbar_.Hide();
            anchor_ = point;
            current_ = point;
            interaction_mode_ = InteractionMode::Selecting;
            SetCapture(window_);
            InvalidateSelectionChange(previous_selection,
                                      previous_has_selection,
                                      previous_toolbar_rect,
                                      previous_toolbar_visible);
            return 0;
        }

        if (!IsPointInside(selection_, point)) {
            return 0;
        }

        preview_start_ = point;
        preview_current_ = point;
        interaction_mode_ =
            active_tool_ == EditTool::Mosaic ? InteractionMode::Mosaic : InteractionMode::Arrow;
        SetCapture(window_);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (interaction_mode_ == InteractionMode::Selecting) {
            const RECT previous_selection = CurrentSelection();
            const bool previous_has_selection = HasSelection();
            const RECT previous_toolbar_rect = RECT{};
            const bool previous_toolbar_visible = false;
            current_.x = GET_X_LPARAM(l_param);
            current_.y = GET_Y_LPARAM(l_param);
            InvalidateSelectionChange(previous_selection,
                                      previous_has_selection,
                                      previous_toolbar_rect,
                                      previous_toolbar_visible);
        } else if (interaction_mode_ == InteractionMode::Adjusting) {
            const RECT previous_selection = selection_;
            const RECT previous_toolbar_rect = toolbar_.IsVisible() ? toolbar_.Bounds() : RECT{};
            const bool previous_toolbar_visible = toolbar_.IsVisible();
            const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            selection_ = AdjustSelectionFromDrag(point);
            UpdateToolbarLayout();
            InvalidateSelectionChange(previous_selection,
                                      true,
                                      previous_toolbar_rect,
                                      previous_toolbar_visible);
        } else if (interaction_mode_ == InteractionMode::Mosaic ||
                   interaction_mode_ == InteractionMode::Arrow) {
            const RECT previous_preview = interaction_mode_ == InteractionMode::Mosaic
                                              ? CurrentPreviewRect()
                                              : ArrowPreviewRect();
            const bool previous_has_preview = common::HasArea(previous_preview);
            preview_current_.x = GET_X_LPARAM(l_param);
            preview_current_.y = GET_Y_LPARAM(l_param);
            InvalidatePreviewChange(previous_preview, previous_has_preview);
        }
        return 0;

    case WM_LBUTTONUP:
        if (interaction_mode_ == InteractionMode::Selecting) {
            current_.x = GET_X_LPARAM(l_param);
            current_.y = GET_Y_LPARAM(l_param);
            const RECT final_selection = CurrentSelection();
            ResetInteraction();
            if (common::HasArea(final_selection)) {
                CommitSelection(final_selection);
            } else {
                selection_ = RECT{};
                toolbar_.Hide();
                RefreshFullFrame();
            }
            return 0;
        }

        if (interaction_mode_ == InteractionMode::Adjusting) {
            const RECT previous_selection = selection_;
            const RECT previous_toolbar_rect = toolbar_.IsVisible() ? toolbar_.Bounds() : RECT{};
            const bool previous_toolbar_visible = toolbar_.IsVisible();
            const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            selection_ = AdjustSelectionFromDrag(point);
            UpdateToolbarLayout();
            InvalidateSelectionChange(previous_selection,
                                      true,
                                      previous_toolbar_rect,
                                      previous_toolbar_visible);
            ResetInteraction();
            return 0;
        }

        if (interaction_mode_ == InteractionMode::Mosaic || interaction_mode_ == InteractionMode::Arrow) {
            preview_current_.x = GET_X_LPARAM(l_param);
            preview_current_.y = GET_Y_LPARAM(l_param);
            const RECT previous_preview =
                interaction_mode_ == InteractionMode::Mosaic ? CurrentPreviewRect() : ArrowPreviewRect();
            const bool previous_has_preview = common::HasArea(previous_preview);

            std::wstring error_message;
            bool applied = true;
            if (interaction_mode_ == InteractionMode::Mosaic) {
                applied = ApplyPendingMosaic(error_message);
            } else {
                applied = ApplyPendingArrow(error_message);
            }

            ResetInteraction();
            if (!applied) {
                ShowEditError(error_message);
            }
            InvalidatePreviewChange(previous_preview, previous_has_preview);
            RefreshFullFrame();
            return 0;
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint_struct{};
        HDC hdc = BeginPaint(window_, &paint_struct);

        const int paint_width = common::RectWidth(paint_struct.rcPaint);
        const int paint_height = common::RectHeight(paint_struct.rcPaint);
        if (paint_width > 0 && paint_height > 0 && back_buffer_dc_ != nullptr) {
            FlushPendingDirtyRegion();
            BitBlt(hdc,
                   paint_struct.rcPaint.left,
                   paint_struct.rcPaint.top,
                   paint_width,
                   paint_height,
                   back_buffer_dc_,
                   paint_struct.rcPaint.left,
                   paint_struct.rcPaint.top,
                   SRCCOPY);
        } else if (snapshot_ != nullptr) {
            RECT client_rect{};
            GetClientRect(window_, &client_rect);
            PaintFrame(hdc, client_rect);
        }

        EndPaint(window_, &paint_struct);
        return 0;
    }

    case WM_CLOSE:
        FinishSelection(false);
        return 0;

    case WM_DESTROY:
        DestroyBackBuffer();
        finished_ = true;
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window_, message, w_param, l_param);
}

}  // namespace ui
