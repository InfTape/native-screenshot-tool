#include "ui/RecordingOverlay.h"

#include <windowsx.h>
#include <algorithm>
#include "ui/UiTypography.h"
#include "common/Win32Error.h"

namespace {
constexpr wchar_t kClass[] = L"NativeScreenshot.RecordingOverlay";
constexpr COLORREF kTransparent = RGB(255, 0, 255);
constexpr UINT kStopId = 1;
constexpr int kBarWidth = 352;
constexpr int kBarHeight = 42;
}

namespace ui {

common::Result<void> RecordingOverlay::Show(HWND owner, UINT stop_message,
    const RECT& region, const std::wstring& mode) {
    Close();
    owner_ = owner;
    stop_message_ = stop_message;
    region_ = region;
    mode_ = mode;
    bar_moved_ = false;
    WNDCLASSW window_class{};
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = WindowProc;
    window_class.lpszClassName = kClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return common::Result<void>::Failure(L"无法注册录屏提示窗口。");
    }
    const auto create = [this](DWORD extra, const wchar_t* name) {
        return CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | extra, kClass, name,
            WS_POPUP, region_.left, region_.top, 1, 1, nullptr, nullptr, instance_, this);
    };
    mask_ = create(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, L"录制范围遮罩");
    border_ = create(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, L"录制范围边框");
    bar_ = create(0, L"录制状态 - 原生 Win32 截图工具");
    if (!mask_ || !border_ || !bar_ ||
        !SetLayeredWindowAttributes(mask_, kTransparent, 48, LWA_COLORKEY | LWA_ALPHA) ||
        !SetLayeredWindowAttributes(border_, 0, 255, LWA_ALPHA) ||
        !SetWindowDisplayAffinity(mask_, WDA_EXCLUDEFROMCAPTURE) ||
        !SetWindowDisplayAffinity(border_, WDA_EXCLUDEFROMCAPTURE) ||
        !SetWindowDisplayAffinity(bar_, WDA_EXCLUDEFROMCAPTURE)) {
        const DWORD error = GetLastError();
        Close();
        return common::Result<void>::Failure(L"无法创建不进入录制画面的提示层。\n" + common::GetLastErrorMessage(error));
    }
    label_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 0, 0, bar_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)), instance_, nullptr);
    stop_button_ = CreateWindowExW(0, L"BUTTON", L"停止并保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, bar_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopId)), instance_, nullptr);
    if (!label_ || !stop_button_) { Close(); return common::Result<void>::Failure(L"无法创建录屏计时浮条。"); }
    UpdateFont();
    Layout();
    Update(region, 0, false);
    for (HWND window : {mask_, border_, bar_}) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
    return common::Result<void>::Success();
}

void RecordingOverlay::UpdateFont() {
    const HFONT replacement = CreateUiFont(UiDpi(bar_));
    if (!replacement) return;
    const HFONT previous = font_;
    font_ = replacement;
    for (HWND control : {label_, stop_button_}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    if (previous) DeleteObject(previous);
}

void RecordingOverlay::Layout() {
    if (!bar_ || !border_ || !mask_) return;
    const UINT dpi = UiDpi(bar_);
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const int thickness = scale(2);
    const int width = region_.right - region_.left;
    const int height = region_.bottom - region_.top;
    // Draw inside the bounds so full-screen recordings still have a visible edge.
    SetWindowPos(border_, HWND_TOPMOST, region_.left, region_.top,
        width, height, SWP_NOACTIVATE);
    HRGN ring = CreateRectRgn(0, 0, width, height);
    HRGN hole = CreateRectRgn(thickness, thickness,
        std::max(thickness, width - thickness), std::max(thickness, height - thickness));
    CombineRgn(ring, ring, hole, RGN_DIFF);
    DeleteObject(hole);
    if (!SetWindowRgn(border_, ring, TRUE)) DeleteObject(ring);
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    SetWindowPos(mask_, HWND_TOPMOST, left, top, GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_CYVIRTUALSCREEN), SWP_NOACTIVATE);
    InvalidateRect(mask_, nullptr, TRUE);

    RECT bar_bounds{};
    GetWindowRect(bar_, &bar_bounds);
    POINT anchor{region_.left, region_.top};
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = region_.left + (width - scale(kBarWidth)) / 2;
    int y = region_.top - scale(kBarHeight + 8);
    if (y < monitor.rcWork.top) y = region_.top + scale(12);
    if (bar_moved_) { x = bar_bounds.left; y = bar_bounds.top; }
    x = std::clamp(x, static_cast<int>(monitor.rcWork.left),
        std::max(static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - scale(kBarWidth)));
    y = std::clamp(y, static_cast<int>(monitor.rcWork.top),
        std::max(static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom) - scale(kBarHeight)));
    SetWindowPos(bar_, HWND_TOPMOST, x, y, scale(kBarWidth), scale(kBarHeight), SWP_NOACTIVATE);
    MoveWindow(label_, scale(16), scale(6), scale(222), scale(30), TRUE);
    MoveWindow(stop_button_, scale(246), scale(6), scale(96), scale(30), TRUE);
}

void RecordingOverlay::Update(const RECT& region, ULONGLONG seconds, bool stopping) {
    if (!bar_) return;
    if (!EqualRect(&region_, &region)) { region_ = region; Layout(); }
    wchar_t text[128]{};
    swprintf_s(text, L"● %s  %02llu:%02llu:%02llu", mode_.c_str(),
        seconds / 3600, (seconds / 60) % 60, seconds % 60);
    SetWindowTextW(label_, text);
    SetWindowTextW(stop_button_, stopping ? L"保存中…" : L"停止并保存");
    EnableWindow(stop_button_, !stopping);
}

void RecordingOverlay::Close() {
    for (HWND window : {bar_, border_, mask_}) if (window) DestroyWindow(window);
    bar_ = border_ = mask_ = label_ = stop_button_ = nullptr;
    if (font_) DeleteObject(font_);
    font_ = nullptr;
}

LRESULT CALLBACK RecordingOverlay::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
            reinterpret_cast<CREATESTRUCTW*>(l_param)->lpCreateParams));
    }
    auto* self = reinterpret_cast<RecordingOverlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!self) return DefWindowProcW(window, message, w_param, l_param);
    switch (message) {
    case WM_NCHITTEST:
        if (window != self->bar_) return HTTRANSPARENT;
        // Native button children handle clicks; the remaining toolbar can be dragged.
        return HTCLIENT;
    case WM_LBUTTONDOWN:
        if (window == self->bar_) {
            POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            ClientToScreen(window, &point);
            SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
        }
        return 0;
    case WM_EXITSIZEMOVE: self->bar_moved_ = true; return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_COMMAND:
        if (LOWORD(w_param) == kStopId) PostMessageW(self->owner_, self->stop_message_, 0, 0);
        return 0;
    case WM_CLOSE:
        PostMessageW(self->owner_, self->stop_message_, 0, 0);
        return 0;
    case WM_DPICHANGED:
        if (window == self->bar_) { self->UpdateFont(); self->Layout(); }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(200, 32, 32));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        if (window == self->mask_) {
            FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            POINT origin{};
            ClientToScreen(window, &origin);
            RECT hole = self->region_;
            OffsetRect(&hole, -origin.x, -origin.y);
            HBRUSH clear = CreateSolidBrush(kTransparent);
            FillRect(dc, &hole, clear);
            DeleteObject(clear);
        } else if (window == self->border_) {
            HBRUSH border = CreateSolidBrush(RGB(235, 55, 55));
            FillRect(dc, &bounds, border);
            DeleteObject(border);
        } else {
            FillRect(dc, &bounds, GetSysColorBrush(COLOR_WINDOW));
            FrameRect(dc, &bounds, GetSysColorBrush(COLOR_3DSHADOW));
        }
        EndPaint(window, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

}  // namespace ui
