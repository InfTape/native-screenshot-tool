#include "ui/WindowSelectionOverlay.h"

#include <Windows.h>
#include <WindowsX.h>

#include <algorithm>
#include <string>

#include "common/RectUtils.h"
#include "common/Win32Error.h"
#include "ui/OverlayPrimitives.h"

namespace {

constexpr int kInstructionHeight = 56;
constexpr int kInstructionMargin = 16;

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

common::Result<std::optional<capture::WindowInfo>> WindowSelectionOverlay::SelectWindow(
    const capture::DesktopSnapshot& snapshot,
    const std::vector<HWND>& excluded_windows) {
    snapshot_ = &snapshot;
    excluded_windows_ = excluded_windows;
    hovered_window_.reset();
    finished_ = false;
    accepted_ = false;

    auto register_result = RegisterWindowClass();
    if (!register_result) {
        return common::Result<std::optional<capture::WindowInfo>>::Failure(register_result.Error());
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
        snapshot_ = nullptr;
        excluded_windows_.clear();
        return common::Result<std::optional<capture::WindowInfo>>::Failure(
            L"创建窗口选择遮罩层失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    excluded_windows_.push_back(window_);

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    SetFocus(window_);

    common::Result<void> loop_result = common::Result<void>::Success();
    MSG message{};
    while (!finished_) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            loop_result = common::Result<void>::Failure(
                L"窗口选择消息循环失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
            break;
        }

        if (result == 0) {
            finished_ = true;
            break;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    std::optional<capture::WindowInfo> selection;
    if (accepted_ && hovered_window_.has_value()) {
        selection = hovered_window_;
    }

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    snapshot_ = nullptr;
    excluded_windows_.clear();
    hovered_window_.reset();

    if (!loop_result) {
        return common::Result<std::optional<capture::WindowInfo>>::Failure(loop_result.Error());
    }

    return common::Result<std::optional<capture::WindowInfo>>::Success(std::move(selection));
}

common::Result<void> WindowSelectionOverlay::RegisterWindowClass() const {
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
            return common::Result<void>::Failure(
                L"注册窗口选择遮罩层失败。\n\n" + common::GetLastErrorMessage(error_code));
        }
    }

    return common::Result<void>::Success();
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
    AlphaFillRect(hdc, rect, DefaultOverlayTheme().overlay_alpha);
}

void WindowSelectionOverlay::DrawHoveredWindow(HDC hdc) const {
    if (!hovered_window_.has_value()) {
        return;
    }

    const OverlayTheme& theme = DefaultOverlayTheme();
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

    DrawOutlineRect(hdc, hovered_rect, theme.panel_border_color, 3);

    RECT label_rect{hovered_rect.left,
                    std::max(0L, hovered_rect.top - 54),
                    std::min<LONG>(hovered_rect.right, hovered_rect.left + 420),
                    hovered_rect.top};
    if (common::RectHeight(label_rect) < 40) {
        label_rect.top = hovered_rect.bottom;
        label_rect.bottom = hovered_rect.bottom + 54;
    }

    OverlayPanelStyle label_style{};
    label_style.background_mode = OverlayPanelStyle::BackgroundMode::Dimmed;
    label_style.background_alpha = static_cast<BYTE>(theme.overlay_alpha + 40);
    label_style.text_color = theme.panel_text_color;
    PaintPanel(hdc, label_rect, label_style);

    RECT text_rect = label_rect;
    InflateRect(&text_rect, -10, -8);
    const std::wstring label = hovered_window_->title + L"\n" +
                               std::to_wstring(common::RectWidth(hovered_window_->bounds)) + L" x " +
                               std::to_wstring(common::RectHeight(hovered_window_->bounds));
    DrawTextBlock(
        hdc, text_rect, label, DT_LEFT | DT_VCENTER | DT_WORDBREAK, label_style.text_color);
}

void WindowSelectionOverlay::DrawInstructions(HDC hdc, const RECT& bounds) const {
    const OverlayTheme& theme = DefaultOverlayTheme();

    RECT instruction_rect = bounds;
    instruction_rect.left += kInstructionMargin;
    instruction_rect.top += kInstructionMargin;
    instruction_rect.right =
        std::min<LONG>(instruction_rect.left + 560, bounds.right - kInstructionMargin);
    instruction_rect.bottom = instruction_rect.top + kInstructionHeight;

    OverlayPanelStyle panel_style{};
    panel_style.background_mode = OverlayPanelStyle::BackgroundMode::Dimmed;
    panel_style.background_alpha = static_cast<BYTE>(theme.overlay_alpha + 40);
    panel_style.text_color = theme.panel_text_color;
    PaintPanel(hdc, instruction_rect, panel_style);

    RECT text_rect = instruction_rect;
    InflateRect(&text_rect, -12, -8);
    DrawTextBlock(hdc,
                  text_rect,
                  L"移动到目标窗口上并单击完成窗口截图。\n按 Esc 或右键取消。",
                  DT_LEFT | DT_VCENTER | DT_WORDBREAK,
                  panel_style.text_color);
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
