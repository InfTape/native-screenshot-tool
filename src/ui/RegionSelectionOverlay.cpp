#include "ui/RegionSelectionOverlay.h"

#include <Windows.h>
#include <WindowsX.h>

#include <algorithm>
#include <string>

#include "common/RectUtils.h"
#include "common/Win32Error.h"

namespace {

constexpr BYTE kOverlayAlpha = 120;
constexpr int kInstructionHeight = 54;
constexpr int kInstructionMargin = 16;

void AlphaFillRect(HDC hdc, const RECT& rect, BYTE alpha) {
    if (!common::HasArea(rect)) {
        return;
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
        return;
    }

    *static_cast<DWORD*>(bits) = 0x00000000;

    HDC memory_dc = CreateCompatibleDC(hdc);
    if (memory_dc == nullptr) {
        DeleteObject(bitmap);
        return;
    }

    const HGDIOBJ previous = SelectObject(memory_dc, bitmap);
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = alpha;

    AlphaBlend(hdc,
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

    SelectObject(memory_dc, previous);
    DeleteDC(memory_dc);
    DeleteObject(bitmap);
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

void RegionSelectionOverlay::DrawDimmedRect(HDC hdc, const RECT& rect) const {
    AlphaFillRect(hdc, rect, kOverlayAlpha);
}

void RegionSelectionOverlay::DrawInstructions(HDC hdc, const RECT& bounds) const {
    RECT instruction_rect = bounds;
    instruction_rect.left += kInstructionMargin;
    instruction_rect.top += kInstructionMargin;
    instruction_rect.right = std::min<LONG>(instruction_rect.left + 520, bounds.right - kInstructionMargin);
    instruction_rect.bottom = instruction_rect.top + kInstructionHeight;

    AlphaFillRect(hdc, instruction_rect, static_cast<BYTE>(kOverlayAlpha + 40));

    RECT text_rect = instruction_rect;
    InflateRect(&text_rect, -12, -8);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc,
              L"拖动鼠标选择截图区域，松开即完成。\n按 Esc 或右键取消。",
              -1,
              &text_rect,
              DT_LEFT | DT_VCENTER | DT_WORDBREAK);
}

void RegionSelectionOverlay::DrawSelection(HDC hdc) const {
    if (!HasSelection()) {
        return;
    }

    RECT selection = CurrentSelection();

    RECT client_rect{};
    GetClientRect(window_, &client_rect);

    RECT top_rect{client_rect.left, client_rect.top, client_rect.right, selection.top};
    RECT left_rect{client_rect.left, selection.top, selection.left, selection.bottom};
    RECT right_rect{selection.right, selection.top, client_rect.right, selection.bottom};
    RECT bottom_rect{client_rect.left, selection.bottom, client_rect.right, client_rect.bottom};

    DrawDimmedRect(hdc, top_rect);
    DrawDimmedRect(hdc, left_rect);
    DrawDimmedRect(hdc, right_rect);
    DrawDimmedRect(hdc, bottom_rect);

    HPEN border_pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HGDIOBJ previous_pen = SelectObject(hdc, border_pen);
    HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, selection.left, selection.top, selection.right, selection.bottom);
    SelectObject(hdc, previous_brush);
    SelectObject(hdc, previous_pen);
    DeleteObject(border_pen);

    RECT label_rect = SelectionLabelRect(selection, client_rect);

    AlphaFillRect(hdc, label_rect, static_cast<BYTE>(kOverlayAlpha + 40));

    RECT text_rect = label_rect;
    InflateRect(&text_rect, -8, -6);
    const std::wstring size_text =
        std::to_wstring(common::RectWidth(selection)) + L" x " +
        std::to_wstring(common::RectHeight(selection));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, size_text.c_str(), -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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

RECT RegionSelectionOverlay::SelectionVisualRect(const RECT& selection, const RECT& client_rect) const {
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

void RegionSelectionOverlay::InvalidateSelectionChange(const RECT& previous_selection,
                                                       bool previous_has_selection) const {
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

    RECT dirty_rect{};
    bool has_dirty_rect = false;
    if (previous_has_selection) {
        dirty_rect = SelectionVisualRect(previous_selection, client_rect);
        has_dirty_rect = common::HasArea(dirty_rect);
    }

    if (current_has_selection) {
        const RECT current_dirty_rect = SelectionVisualRect(current_selection, client_rect);
        if (common::HasArea(current_dirty_rect)) {
            if (has_dirty_rect) {
                RECT union_rect{};
                UnionRect(&union_rect, &dirty_rect, &current_dirty_rect);
                dirty_rect = union_rect;
            } else {
                dirty_rect = current_dirty_rect;
                has_dirty_rect = true;
            }
        }
    }

    if (has_dirty_rect) {
        InvalidateRect(window_, &dirty_rect, FALSE);
    }
}

void RegionSelectionOverlay::PaintFrame(HDC hdc, const RECT& client_rect) const {
    const BITMAPINFO bitmap_info = snapshot_->image.CreateBitmapInfo();
    StretchDIBits(hdc,
                  0,
                  0,
                  snapshot_->image.Width(),
                  snapshot_->image.Height(),
                  0,
                  0,
                  snapshot_->image.Width(),
                  snapshot_->image.Height(),
                  snapshot_->image.Pixels().data(),
                  &bitmap_info,
                  DIB_RGB_COLORS,
                  SRCCOPY);

    if (HasSelection()) {
        DrawSelection(hdc);
    } else {
        DrawDimmedRect(hdc, client_rect);
    }

    DrawInstructions(hdc, client_rect);
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

        RECT client_rect{};
        GetClientRect(window_, &client_rect);

        const int paint_width = common::RectWidth(paint_struct.rcPaint);
        const int paint_height = common::RectHeight(paint_struct.rcPaint);
        if (paint_width > 0 && paint_height > 0) {
            HDC memory_dc = CreateCompatibleDC(hdc);
            HBITMAP bitmap =
                memory_dc == nullptr ? nullptr : CreateCompatibleBitmap(hdc, paint_width, paint_height);

            if (memory_dc != nullptr && bitmap != nullptr) {
                const HGDIOBJ previous_bitmap = SelectObject(memory_dc, bitmap);
                SetViewportOrgEx(memory_dc, -paint_struct.rcPaint.left, -paint_struct.rcPaint.top, nullptr);
                PaintFrame(memory_dc, client_rect);
                SetViewportOrgEx(memory_dc, 0, 0, nullptr);
                BitBlt(hdc,
                       paint_struct.rcPaint.left,
                       paint_struct.rcPaint.top,
                       paint_width,
                       paint_height,
                       memory_dc,
                       0,
                       0,
                       SRCCOPY);
                SelectObject(memory_dc, previous_bitmap);
            } else {
                PaintFrame(hdc, client_rect);
            }

            if (bitmap != nullptr) {
                DeleteObject(bitmap);
            }
            if (memory_dc != nullptr) {
                DeleteDC(memory_dc);
            }
        }

        EndPaint(window_, &paint_struct);
        return 0;
    }

    case WM_CLOSE:
        FinishSelection(false);
        return 0;

    case WM_DESTROY:
        finished_ = true;
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window_, message, w_param, l_param);
}

}  // namespace ui
