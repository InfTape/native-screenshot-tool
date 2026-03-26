#include "ui/WindowSelectionOverlay.h"

#include <Windows.h>
#include <WindowsX.h>

#include <algorithm>
#include <string>

#include "common/RectUtils.h"
#include "common/Win32Error.h"

namespace {

constexpr BYTE kOverlayAlpha = 120;
constexpr int kInstructionHeight = 56;
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

bool SameHoveredWindow(const std::optional<capture::WindowInfo>& left,
                       const std::optional<capture::WindowInfo>& right) {
    if (!left.has_value() && !right.has_value()) {
        return true;
    }

    if (!left.has_value() || !right.has_value()) {
        return false;
    }

    return left->handle == right->handle;
}

}  // namespace

namespace ui {

WindowSelectionOverlay::WindowSelectionOverlay(HINSTANCE instance) : instance_(instance) {}

std::optional<capture::WindowInfo> WindowSelectionOverlay::SelectWindow(
    const capture::DesktopSnapshot& snapshot,
    const std::vector<HWND>& excluded_windows,
    std::wstring& error_message) {
    snapshot_ = &snapshot;
    excluded_windows_ = excluded_windows;
    hovered_window_.reset();
    finished_ = false;
    accepted_ = false;

    if (!RegisterWindowClass()) {
        error_message = L"注册窗口选择遮罩层失败。\n\n" +
                        common::GetLastErrorMessage(GetLastError());
        return std::nullopt;
    }

    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                              kClassName,
                              L"窗口截图",
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
        error_message = L"创建窗口选择遮罩层失败。\n\n" +
                        common::GetLastErrorMessage(GetLastError());
        snapshot_ = nullptr;
        excluded_windows_.clear();
        return std::nullopt;
    }

    excluded_windows_.push_back(window_);

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    SetFocus(window_);

    MSG message{};
    while (!finished_) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            error_message = L"窗口选择消息循环失败。\n\n" +
                            common::GetLastErrorMessage(GetLastError());
            break;
        }

        if (result == 0) {
            finished_ = true;
            break;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    std::optional<capture::WindowInfo> result;
    if (accepted_ && hovered_window_.has_value()) {
        result = hovered_window_;
    }

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    snapshot_ = nullptr;
    excluded_windows_.clear();
    hovered_window_.reset();
    return result;
}

bool WindowSelectionOverlay::RegisterWindowClass() const {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &WindowSelectionOverlay::WindowProc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_HAND);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
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

bool WindowSelectionOverlay::UpdateHoveredWindow(const POINT& client_point) {
    POINT screen_point = client_point;
    ClientToScreen(window_, &screen_point);

    const auto next_hovered =
        window_locator_.FindTopLevelWindowAtPoint(screen_point, excluded_windows_);
    if (SameHoveredWindow(hovered_window_, next_hovered)) {
        return false;
    }

    hovered_window_ = next_hovered;
    return true;
}

RECT WindowSelectionOverlay::HoveredWindowRectInClient() const {
    RECT client_rect = hovered_window_->bounds;
    OffsetRect(&client_rect, -snapshot_->origin_x, -snapshot_->origin_y);
    return client_rect;
}

void WindowSelectionOverlay::FinishSelection(bool accepted) {
    accepted_ = accepted;
    finished_ = true;

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

void WindowSelectionOverlay::DrawDimmedRect(HDC hdc, const RECT& rect) const {
    AlphaFillRect(hdc, rect, kOverlayAlpha);
}

void WindowSelectionOverlay::DrawHoveredWindow(HDC hdc) const {
    if (!hovered_window_.has_value()) {
        return;
    }

    const RECT hovered_rect = HoveredWindowRectInClient();
    RECT client_rect{};
    GetClientRect(window_, &client_rect);

    RECT top_rect{client_rect.left, client_rect.top, client_rect.right, hovered_rect.top};
    RECT left_rect{client_rect.left, hovered_rect.top, hovered_rect.left, hovered_rect.bottom};
    RECT right_rect{hovered_rect.right, hovered_rect.top, client_rect.right, hovered_rect.bottom};
    RECT bottom_rect{client_rect.left, hovered_rect.bottom, client_rect.right, client_rect.bottom};

    DrawDimmedRect(hdc, top_rect);
    DrawDimmedRect(hdc, left_rect);
    DrawDimmedRect(hdc, right_rect);
    DrawDimmedRect(hdc, bottom_rect);

    HPEN border_pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
    HGDIOBJ previous_pen = SelectObject(hdc, border_pen);
    HGDIOBJ previous_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, hovered_rect.left, hovered_rect.top, hovered_rect.right, hovered_rect.bottom);
    SelectObject(hdc, previous_brush);
    SelectObject(hdc, previous_pen);
    DeleteObject(border_pen);

    RECT label_rect{hovered_rect.left,
                    std::max(0L, hovered_rect.top - 54),
                    std::min<LONG>(hovered_rect.right, hovered_rect.left + 420),
                    hovered_rect.top};
    if (common::RectHeight(label_rect) < 40) {
        label_rect.top = hovered_rect.bottom;
        label_rect.bottom = hovered_rect.bottom + 54;
    }

    AlphaFillRect(hdc, label_rect, static_cast<BYTE>(kOverlayAlpha + 40));

    RECT text_rect = label_rect;
    InflateRect(&text_rect, -10, -8);
    std::wstring label = hovered_window_->title + L"\n" +
                         std::to_wstring(common::RectWidth(hovered_window_->bounds)) + L" x " +
                         std::to_wstring(common::RectHeight(hovered_window_->bounds));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, label.c_str(), -1, &text_rect, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
}

void WindowSelectionOverlay::DrawInstructions(HDC hdc, const RECT& bounds) const {
    RECT instruction_rect = bounds;
    instruction_rect.left += kInstructionMargin;
    instruction_rect.top += kInstructionMargin;
    instruction_rect.right =
        std::min<LONG>(instruction_rect.left + 560, bounds.right - kInstructionMargin);
    instruction_rect.bottom = instruction_rect.top + kInstructionHeight;

    AlphaFillRect(hdc, instruction_rect, static_cast<BYTE>(kOverlayAlpha + 40));

    RECT text_rect = instruction_rect;
    InflateRect(&text_rect, -12, -8);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc,
              L"移动到目标窗口上并单击完成窗口截图。\n按 Esc 或右键取消。",
              -1,
              &text_rect,
              DT_LEFT | DT_VCENTER | DT_WORDBREAK);
}

LRESULT CALLBACK WindowSelectionOverlay::WindowProc(HWND hwnd,
                                                    UINT message,
                                                    WPARAM w_param,
                                                    LPARAM l_param) {
    WindowSelectionOverlay* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = static_cast<WindowSelectionOverlay*>(create_struct->lpCreateParams);
        self->window_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WindowSelectionOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(message, w_param, l_param);
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT WindowSelectionOverlay::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, hovered_window_.has_value() ? IDC_HAND : IDC_ARROW));
        return TRUE;

    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        if (UpdateHoveredWindow(point)) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        if (UpdateHoveredWindow(point)) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        if (hovered_window_.has_value()) {
            FinishSelection(true);
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE) {
            FinishSelection(false);
            return 0;
        }
        break;

    case WM_RBUTTONUP:
        FinishSelection(false);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint_struct{};
        HDC hdc = BeginPaint(window_, &paint_struct);

        RECT client_rect{};
        GetClientRect(window_, &client_rect);

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

        if (hovered_window_.has_value()) {
            DrawHoveredWindow(hdc);
        } else {
            DrawDimmedRect(hdc, client_rect);
        }

        DrawInstructions(hdc, client_rect);

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
