#include "ui/RegionSelectionOverlay.h"

#include <Windows.h>
#include <WindowsX.h>

#include <algorithm>
#include <string>
#include <vector>

#include "common/RectUtils.h"
#include "common/Win32Error.h"

namespace {

constexpr BYTE kOverlayAlpha = 120;
constexpr int kInstructionHeight = 54;
constexpr int kInstructionMargin = 16;
constexpr COLORREF kPanelColor = RGB(24, 24, 24);
constexpr COLORREF kPanelBorderColor = RGB(255, 255, 255);
constexpr COLORREF kPanelTextColor = RGB(255, 255, 255);

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

}  // namespace

namespace ui {

RegionSelectionOverlay::RegionSelectionOverlay(HINSTANCE instance) : instance_(instance) {}

std::optional<RECT> RegionSelectionOverlay::SelectRegion(const capture::DesktopSnapshot& snapshot,
                                                         std::wstring& error_message) {
    snapshot_ = &snapshot;
    finished_ = false;
    accepted_ = false;
    dragging_ = false;
    anchor_ = POINT{};
    current_ = POINT{};
    selection_ = RECT{};

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
        return std::nullopt;
    }

    if (!CreateBackBuffer(error_message)) {
        DestroyWindow(window_);
        window_ = nullptr;
        snapshot_ = nullptr;
        return std::nullopt;
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

    std::optional<RECT> result;
    if (accepted_ && common::HasArea(selection_)) {
        result = selection_;
    }

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    DestroyBackBuffer();
    snapshot_ = nullptr;
    return result;
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

RECT RegionSelectionOverlay::CurrentSelection() const {
    RECT normalized = common::NormalizeRect(anchor_, current_);
    return common::ClampRectToBounds(normalized, snapshot_->image.Width(), snapshot_->image.Height());
}

bool RegionSelectionOverlay::HasSelection() const {
    return common::HasArea(CurrentSelection());
}

void RegionSelectionOverlay::FinishSelection(bool accepted) {
    accepted_ = accepted;
    selection_ = CurrentSelection();
    finished_ = true;
    if (dragging_) {
        ReleaseCapture();
        dragging_ = false;
    }

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

void RegionSelectionOverlay::DrawInstructions(HDC hdc, const RECT& bounds) const {
    RECT instruction_rect = bounds;
    instruction_rect.left += kInstructionMargin;
    instruction_rect.top += kInstructionMargin;
    instruction_rect.right =
        std::min<LONG>(instruction_rect.left + 520, bounds.right - kInstructionMargin);
    instruction_rect.bottom = instruction_rect.top + kInstructionHeight;

    FillSolidRect(hdc, instruction_rect, kPanelColor);
    FrameRect(hdc, &instruction_rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    RECT text_rect = instruction_rect;
    InflateRect(&text_rect, -12, -8);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kPanelTextColor);
    DrawTextW(hdc,
              L"拖动鼠标选择截图区域，松开即完成。\n按 Esc 或右键取消。",
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
        error_message = L"创建选区后台缓冲位图失败。\n\n" +
                        common::GetLastErrorMessage(GetLastError());
        DestroyBackBuffer();
        return false;
    }

    back_buffer_previous_bitmap_ = SelectObject(back_buffer_dc_, back_buffer_bitmap_);
    if (back_buffer_previous_bitmap_ == nullptr || back_buffer_previous_bitmap_ == HGDI_ERROR) {
        error_message = L"绑定选区后台缓冲位图失败。\n\n" +
                        common::GetLastErrorMessage(GetLastError());
        DestroyBackBuffer();
        return false;
    }

    back_buffer_size_.cx = snapshot_->image.Width();
    back_buffer_size_.cy = snapshot_->image.Height();

    const RECT full_rect{0, 0, back_buffer_size_.cx, back_buffer_size_.cy};
    DrawInstructions(dimmed_buffer_dc_, full_rect);
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

    PaintBaseImage(base_buffer_dc_);
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
    if (!ApplyAlphaDim(dimmed_buffer_dc_, full_rect, kOverlayAlpha)) {
        error_message = L"生成暗化底图失败。";
        DestroyDimmedBuffer();
        return false;
    }

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

RECT RegionSelectionOverlay::SelectionLabelRect(const RECT& selection,
                                                const RECT& client_rect) const {
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
    label_rect.top = std::clamp(label_rect.top, client_rect.top, client_rect.bottom);
    label_rect.right = std::clamp(label_rect.right, client_rect.left, client_rect.right);
    label_rect.bottom = std::clamp(label_rect.bottom, client_rect.top, client_rect.bottom);
    return label_rect;
}

RECT RegionSelectionOverlay::SelectionVisualRect(const RECT& selection,
                                                 const RECT& client_rect) const {
    if (!common::HasArea(selection)) {
        return RECT{};
    }

    RECT selection_bounds = selection;
    InflateRect(&selection_bounds, 3, 3);
    selection_bounds.left = std::clamp(selection_bounds.left, client_rect.left, client_rect.right);
    selection_bounds.top = std::clamp(selection_bounds.top, client_rect.top, client_rect.bottom);
    selection_bounds.right =
        std::clamp(selection_bounds.right, client_rect.left, client_rect.right);
    selection_bounds.bottom =
        std::clamp(selection_bounds.bottom, client_rect.top, client_rect.bottom);

    const RECT label_rect = SelectionLabelRect(selection, client_rect);
    if (!common::HasArea(label_rect)) {
        return selection_bounds;
    }

    RECT visual_rect{};
    UnionRect(&visual_rect, &selection_bounds, &label_rect);
    return visual_rect;
}

void RegionSelectionOverlay::UpdateBackBuffer(const RECT& dirty_rect) {
    if (back_buffer_dc_ == nullptr || base_buffer_dc_ == nullptr || dimmed_buffer_dc_ == nullptr) {
        return;
    }

    RECT clamped_dirty_rect =
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
    const int region_type = GetRgnBox(pending_dirty_region_, &region_bounds);
    if (region_type == NULLREGION) {
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

    const RECT* dirty_rects = reinterpret_cast<const RECT*>(region_data->Buffer);
    for (DWORD index = 0; index < region_data->rdh.nCount; ++index) {
        UpdateBackBuffer(dirty_rects[index]);
    }

    SetRectRgn(pending_dirty_region_, 0, 0, 0, 0);
}

void RegionSelectionOverlay::InvalidateSelectionChange(const RECT& previous_selection,
                                                       bool previous_has_selection) {
    if (window_ == nullptr) {
        return;
    }

    const RECT current_selection = CurrentSelection();
    const bool current_has_selection = HasSelection();
    if (!previous_has_selection && !current_has_selection) {
        return;
    }

    RECT client_rect{};
    GetClientRect(window_, &client_rect);

    RECT previous_visual_rect{};
    if (previous_has_selection) {
        previous_visual_rect = SelectionVisualRect(previous_selection, client_rect);
        RefreshDirtyRect(previous_visual_rect);
    }

    if (current_has_selection) {
        const RECT current_visual_rect = SelectionVisualRect(current_selection, client_rect);
        if (!previous_has_selection || !EqualRect(&current_visual_rect, &previous_visual_rect)) {
            RefreshDirtyRect(current_visual_rect);
        }
    }
}

void RegionSelectionOverlay::PaintBaseImage(HDC hdc) const {
    const BITMAPINFO bitmap_info = snapshot_->image.CreateBitmapInfo();
    SetDIBitsToDevice(hdc,
                      0,
                      0,
                      snapshot_->image.Width(),
                      snapshot_->image.Height(),
                      0,
                      0,
                      0,
                      snapshot_->image.Height(),
                      snapshot_->image.Pixels().data(),
                      &bitmap_info,
                      DIB_RGB_COLORS);
}

void RegionSelectionOverlay::PaintOverlay(HDC hdc, const RECT& client_rect) const {
    if (HasSelection()) {
        const RECT selection = CurrentSelection();
        DrawSelectionBorder(hdc, selection);
        DrawSelectionLabel(hdc, selection, client_rect);
    }
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
    if (dimmed_buffer_dc_ == nullptr) {
        DrawInstructions(hdc, client_rect);
    }
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
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return TRUE;

    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE) {
            FinishSelection(false);
            return 0;
        }
        break;

    case WM_RBUTTONUP:
        FinishSelection(false);
        return 0;

    case WM_LBUTTONDOWN:
        anchor_.x = GET_X_LPARAM(l_param);
        anchor_.y = GET_Y_LPARAM(l_param);
        current_ = anchor_;
        dragging_ = true;
        SetCapture(window_);
        return 0;

    case WM_MOUSEMOVE:
        if (dragging_) {
            const RECT previous_selection = CurrentSelection();
            const bool previous_has_selection = HasSelection();
            current_.x = GET_X_LPARAM(l_param);
            current_.y = GET_Y_LPARAM(l_param);
            InvalidateSelectionChange(previous_selection, previous_has_selection);
        }
        return 0;

    case WM_LBUTTONUP:
        if (dragging_) {
            const RECT previous_selection = CurrentSelection();
            const bool previous_has_selection = HasSelection();
            current_.x = GET_X_LPARAM(l_param);
            current_.y = GET_Y_LPARAM(l_param);
            if (HasSelection()) {
                FinishSelection(true);
            } else {
                ReleaseCapture();
                dragging_ = false;
                InvalidateSelectionChange(previous_selection, previous_has_selection);
            }
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
