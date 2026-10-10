#include "ui/TitleBar.h"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>

#include "ui/UiTypography.h"

namespace {
constexpr UINT kMinimizeId = 0x7001;
constexpr UINT kMaximizeId = 0x7002;
constexpr UINT kCloseId = 0x7003;
constexpr int kButtonWidth = 46;

int FrameSize(HWND window) {
    const UINT dpi = ui::UiDpi(window);
    using GetMetrics = int(WINAPI*)(int, UINT);
    const auto get_metrics = reinterpret_cast<GetMetrics>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    if (get_metrics) return get_metrics(SM_CXSIZEFRAME, dpi) + get_metrics(SM_CXPADDEDBORDER, dpi);
    RECT bounds{};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW & ~WS_CAPTION, FALSE, 0);
    return -bounds.left;
}
}  // namespace

namespace ui {

bool TitleBar::Create(HWND parent, HINSTANCE instance) {
    parent_ = parent;
    const wchar_t* labels[] = {L"最小化", L"最大化", L"关闭"};
    for (std::size_t index = 0; index < buttons_.size(); ++index) {
        buttons_[index] = CreateWindowExW(0, L"BUTTON", labels[index],
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMinimizeId + index)),
            instance, nullptr);
        if (!buttons_[index]) return false;
        SetWindowSubclass(buttons_[index], ButtonProc, 1, 0);
    }
    return true;
}

void TitleBar::SetFont(HFONT font) {
    font_ = font;
    for (HWND button : buttons_) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void TitleBar::Layout(int client_width) {
    const UINT dpi = UiDpi(parent_);
    const int width = MulDiv(kButtonWidth, dpi, 96);
    const int height = MulDiv(kHeight, dpi, 96);
    for (std::size_t index = 0; index < buttons_.size(); ++index) {
        MoveWindow(buttons_[index], client_width - width * (3 - static_cast<int>(index)),
            0, width, height, TRUE);
    }
    SetWindowTextW(buttons_[1], IsZoomed(parent_) ? L"还原" : L"最大化");
    RECT caption{0, 0, client_width, height};
    InvalidateRect(parent_, &caption, FALSE);
}

void TitleBar::Paint(HDC dc) const {
    const UINT dpi = UiDpi(parent_);
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    RECT bounds{};
    GetClientRect(parent_, &bounds);
    bounds.bottom = scale(kHeight);
    FillRect(dc, &bounds, GetSysColorBrush(COLOR_WINDOW));
    DrawIconEx(dc, scale(12), scale(8), LoadIconW(nullptr, IDI_APPLICATION),
        scale(20), scale(20), 0, nullptr, DI_NORMAL);
    wchar_t title[256]{};
    GetWindowTextW(parent_, title, static_cast<int>(std::size(title)));
    bounds.left = scale(40);
    bounds.right -= scale(3 * kButtonWidth + 8);
    const auto previous = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(GetForegroundWindow() == parent_ ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
    DrawTextW(dc, title, -1, &bounds, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
}

bool TitleBar::DrawButton(const DRAWITEMSTRUCT& item) const {
    if (item.CtlType != ODT_BUTTON || item.CtlID < kMinimizeId || item.CtlID > kCloseId) return false;
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(item.hwndItem, &cursor);
    const bool hovered = PtInRect(&item.rcItem, cursor) != FALSE;
    const bool close = item.CtlID == kCloseId;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF background = hovered || pressed ?
        (close ? RGB(232, 17, 35) : GetSysColor(COLOR_BTNFACE)) : GetSysColor(COLOR_WINDOW);
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    const UINT dpi = UiDpi(parent_);
    const int size = MulDiv(10, dpi, 96);
    const int x = (item.rcItem.right - size) / 2;
    const int y = (item.rcItem.bottom - size) / 2;
    HPEN pen = CreatePen(PS_SOLID, std::max(1, MulDiv(1, dpi, 96)),
        close && (hovered || pressed) ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT));
    const auto old_pen = SelectObject(item.hDC, pen);
    const auto old_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
    if (item.CtlID == kMinimizeId) {
        MoveToEx(item.hDC, x, y + size / 2, nullptr);
        LineTo(item.hDC, x + size, y + size / 2);
    } else if (close) {
        MoveToEx(item.hDC, x, y, nullptr);
        LineTo(item.hDC, x + size, y + size);
        MoveToEx(item.hDC, x + size, y, nullptr);
        LineTo(item.hDC, x, y + size);
    } else if (IsZoomed(parent_)) {
        const int offset = MulDiv(2, dpi, 96);
        Rectangle(item.hDC, x + offset, y, x + size, y + size - offset);
        RECT front{x, y + offset, x + size - offset, y + size};
        HBRUSH front_brush = CreateSolidBrush(background);
        FillRect(item.hDC, &front, front_brush);
        DeleteObject(front_brush);
        Rectangle(item.hDC, x, y + offset, x + size - offset, y + size);
    } else {
        Rectangle(item.hDC, x, y, x + size, y + size);
    }
    SelectObject(item.hDC, old_brush);
    SelectObject(item.hDC, old_pen);
    DeleteObject(pen);
    if (item.itemState & ODS_FOCUS) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -MulDiv(4, dpi, 96), -MulDiv(4, dpi, 96));
        DrawFocusRect(item.hDC, &focus);
    }
    return true;
}

bool TitleBar::HandleCommand(UINT identifier) const {
    switch (identifier) {
    case kMinimizeId: SendMessageW(parent_, WM_SYSCOMMAND, SC_MINIMIZE, 0); return true;
    case kMaximizeId: SendMessageW(parent_, WM_SYSCOMMAND, IsZoomed(parent_) ? SC_RESTORE : SC_MAXIMIZE, 0); return true;
    case kCloseId: SendMessageW(parent_, WM_SYSCOMMAND, SC_CLOSE, 0); return true;
    default: return false;
    }
}

LRESULT TitleBar::CalculateClient(HWND window, WPARAM w_param, LPARAM l_param) {
    auto* bounds = w_param ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(l_param)->rgrc[0] :
        reinterpret_cast<RECT*>(l_param);
    const LONG top = bounds->top;
    DefWindowProcW(window, WM_NCCALCSIZE, w_param, l_param);
    // The resize border is a hit-test area, not a visible strip above the caption.
    // Maximized windows still need their off-screen frame inset to keep text in the work area.
    bounds->top = top + (IsZoomed(window) ? FrameSize(window) : 1);
    return 0;
}

LRESULT TitleBar::HitTest(HWND window, LPARAM l_param) {
    POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    RECT bounds{};
    GetWindowRect(window, &bounds);
    if (!IsZoomed(window)) {
        const int border = FrameSize(window);
        const bool left = point.x < bounds.left + border;
        const bool right = point.x >= bounds.right - border;
        // The top frame now overlaps the painted caption; keep its resize grip slim.
        const int top_grip = std::min(border, std::max(1, MulDiv(4, UiDpi(window), 96)));
        const bool top = point.y < bounds.top + top_grip;
        const bool bottom = point.y >= bounds.bottom - border;
        if (top) return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
        if (bottom) return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
    }
    ScreenToClient(window, &point);
    RECT client{};
    GetClientRect(window, &client);
    const UINT dpi = UiDpi(window);
    if (point.y < MulDiv(kHeight, dpi, 96) && point.x < client.right - MulDiv(3 * kButtonWidth, dpi, 96)) {
        return point.x < MulDiv(36, dpi, 96) ? HTSYSMENU : HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK TitleBar::ButtonProc(HWND window, UINT message, WPARAM w_param,
    LPARAM l_param, UINT_PTR subclass_id, DWORD_PTR reference_data) {
    static_cast<void>(reference_data);
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        InvalidateRect(window, nullptr, FALSE);
    } else if (message == WM_MOUSELEAVE) {
        InvalidateRect(window, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, ButtonProc, subclass_id);
    }
    return DefSubclassProc(window, message, w_param, l_param);
}

}  // namespace ui
