#include "ui/TrayIconController.h"

#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>

#include "common/Win32Error.h"
#include "ui/UiTypography.h"

namespace {

constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayShowWindowMenuId = 3001;
constexpr UINT kTrayCaptureRegionMenuId = 3002;
constexpr UINT kTrayCaptureFullMenuId = 3003;
constexpr UINT kTrayCaptureWindowMenuId = 3004;
constexpr UINT kTrayExitMenuId = 3005;
constexpr UINT kTrayRecordFullMenuId = 3006;
constexpr UINT kTrayRecordRegionMenuId = 3007;
constexpr UINT kTrayStopRecordingMenuId = 3008;
constexpr UINT kTrayRecordWindowMenuId = 3009;

}  // namespace

namespace ui {

TrayIconController::TrayIconController(HINSTANCE instance) : instance_(instance) {}

common::Result<void> TrayIconController::AddIcon(HWND window, UINT callback_message) {
    if (installed_) {
        return common::Result<void>::Success();
    }

    notify_icon_data_ = BuildNotifyIconData(window, callback_message);
    return InstallIcon(NIM_ADD);
}

common::Result<void> TrayIconController::ReAddIcon(HWND window, UINT callback_message) {
    HideTooltip();
    notify_icon_data_ = BuildNotifyIconData(window, callback_message);
    installed_ = false;
    return InstallIcon(NIM_ADD);
}

void TrayIconController::RemoveIcon() {
    HideTooltip();
    if (tooltip_) DestroyWindow(tooltip_);
    tooltip_ = nullptr;
    if (popup_font_) DeleteObject(popup_font_);
    popup_font_ = nullptr;
    if (!installed_) {
        return;
    }

    Shell_NotifyIconW(NIM_DELETE, &notify_icon_data_);
    installed_ = false;
}

bool TrayIconController::ShowBalloon(const std::wstring& title, const std::wstring& message) const {
    if (!installed_) {
        return false;
    }

    NOTIFYICONDATAW balloon = notify_icon_data_;
    balloon.uFlags = NIF_INFO;
    wcsncpy_s(balloon.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(balloon.szInfo, message.c_str(), _TRUNCATE);
    balloon.dwInfoFlags = NIIF_INFO;
    return Shell_NotifyIconW(NIM_MODIFY, &balloon) == TRUE;
}

std::optional<TrayMenuCommand> TrayIconController::ShowContextMenu(HWND owner,
    const POINT& screen_point, bool recording, bool stopping) {
    if (!installed_) {
        return std::nullopt;
    }

    HideTooltip();
    if (!PreparePopupFont(screen_point)) return std::nullopt;

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }

    AppendMenuW(menu, MF_STRING, kTrayShowWindowMenuId, L"显示主窗口");
    const UINT idle_flags = MF_STRING | (recording ? MF_GRAYED : 0);
    AppendMenuW(menu, idle_flags, kTrayCaptureRegionMenuId, L"选区截图");
    AppendMenuW(menu, idle_flags, kTrayCaptureFullMenuId, L"全屏截图");
    AppendMenuW(menu, idle_flags, kTrayCaptureWindowMenuId, L"窗口截图");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, idle_flags, kTrayRecordFullMenuId, L"全屏录制");
    AppendMenuW(menu, idle_flags, kTrayRecordRegionMenuId, L"选区录制");
    AppendMenuW(menu, idle_flags, kTrayRecordWindowMenuId, L"窗口录制");
    AppendMenuW(menu, MF_STRING | (!recording || stopping ? MF_GRAYED : 0),
                 kTrayStopRecordingMenuId, stopping ? L"正在保存视频…" : L"停止录制并保存");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitMenuId, L"退出");

    // Retain text for accessibility while measuring and drawing with our UI font.
    const wchar_t* labels[] = {L"显示主窗口", L"选区截图", L"全屏截图", L"窗口截图",
        nullptr, L"全屏录制", L"选区录制", L"窗口录制", stopping ? L"正在保存视频…" : L"停止录制并保存",
        nullptr, L"退出"};
    for (UINT index = 0; index < std::size(labels); ++index) {
        if (!labels[index]) continue;
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING;
        item.fType = MFT_OWNERDRAW;
        item.dwItemData = reinterpret_cast<ULONG_PTR>(labels[index]);
        item.dwTypeData = const_cast<wchar_t*>(labels[index]);
        SetMenuItemInfoW(menu, index, TRUE, &item);
    }

    SetForegroundWindow(owner);
    menu_open_ = true;
    const UINT command = TrackPopupMenu(menu,
                                        TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screen_point.x,
                                        screen_point.y,
                                        0,
                                        owner,
                                        nullptr);
    menu_open_ = false;
    DestroyMenu(menu);
    PostMessageW(owner, WM_NULL, 0, 0);

    switch (command) {
    case kTrayRecordFullMenuId:
        return TrayMenuCommand::RecordFull;
    case kTrayRecordRegionMenuId:
        return TrayMenuCommand::RecordRegion;
    case kTrayRecordWindowMenuId:
        return TrayMenuCommand::RecordWindow;
    case kTrayStopRecordingMenuId:
        return TrayMenuCommand::StopRecording;
    case kTrayShowWindowMenuId:
        return TrayMenuCommand::ShowWindow;
    case kTrayCaptureRegionMenuId:
        return TrayMenuCommand::CaptureRegion;
    case kTrayCaptureFullMenuId:
        return TrayMenuCommand::CaptureFull;
    case kTrayCaptureWindowMenuId:
        return TrayMenuCommand::CaptureWindow;
    case kTrayExitMenuId:
        return TrayMenuCommand::ExitApplication;
    default:
        return std::nullopt;
    }
}

bool TrayIconController::IsInstalled() const {
    return installed_;
}

common::Result<void> TrayIconController::InstallIcon(DWORD message) {
    if (!Shell_NotifyIconW(message, &notify_icon_data_)) {
        return common::Result<void>::Failure(
            L"创建托盘图标失败。\n\n" + common::GetLastErrorMessage(GetLastError()));
    }

    installed_ = true;
    notify_icon_data_.uVersion = NOTIFYICON_VERSION_4;
    version4_ = Shell_NotifyIconW(NIM_SETVERSION, &notify_icon_data_) == TRUE;
    return common::Result<void>::Success();
}

bool TrayIconController::PreparePopupFont(const POINT& anchor) {
    if (!tooltip_) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            anchor.x, anchor.y, 0, 0, notify_icon_data_.hWnd, nullptr, instance_, nullptr);
        if (!tooltip_) return false;
        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        tool.hwnd = notify_icon_data_.hWnd;
        tool.uId = kTrayIconId;
        tool.lpszText = const_cast<wchar_t*>(L"原生 Win32 截图工具");
        if (!SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) {
            DestroyWindow(tooltip_);
            tooltip_ = nullptr;
            return false;
        }
    }
    SetWindowPos(tooltip_, HWND_TOPMOST, anchor.x, anchor.y, 0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE);
    const UINT dpi = UiDpi(tooltip_);
    if (!popup_font_ || dpi != popup_dpi_) {
        HFONT replacement = CreateUiFont(dpi);
        if (!replacement) return false;
        const HFONT previous = popup_font_;
        popup_font_ = replacement;
        popup_dpi_ = dpi;
        SendMessageW(tooltip_, WM_SETFONT, reinterpret_cast<WPARAM>(popup_font_), TRUE);
        if (previous) DeleteObject(previous);
    }
    return true;
}

void TrayIconController::ShowTooltip(const POINT& anchor) {
    if (!installed_ || menu_open_ || !PreparePopupFont(anchor)) return;
    NOTIFYICONIDENTIFIER identifier{sizeof(identifier)};
    identifier.hWnd = notify_icon_data_.hWnd;
    identifier.uID = kTrayIconId;
    RECT icon{};
    if (FAILED(Shell_NotifyIconGetRect(&identifier, &icon))) {
        icon = {anchor.x, anchor.y, anchor.x, anchor.y};
    }
    const int padding = MulDiv(8, popup_dpi_, 96);
    RECT margin{padding, padding / 2, padding, padding / 2};
    SendMessageW(tooltip_, TTM_SETMARGIN, 0, reinterpret_cast<LPARAM>(&margin));
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = notify_icon_data_.hWnd;
    tool.uId = kTrayIconId;
    SIZE size{};
    HDC dc = GetDC(tooltip_);
    const auto previous = SelectObject(dc, popup_font_);
    constexpr wchar_t text[] = L"原生 Win32 截图工具";
    GetTextExtentPoint32W(dc, text, static_cast<int>(std::size(text) - 1), &size);
    SelectObject(dc, previous);
    ReleaseDC(tooltip_, dc);
    const int width = size.cx + 2 * padding + MulDiv(6, popup_dpi_, 96);
    const int height = size.cy + padding + MulDiv(6, popup_dpi_, 96);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = std::clamp(static_cast<int>(icon.left), static_cast<int>(monitor.rcWork.left),
        std::max(static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - width));
    int y = icon.top - height - MulDiv(4, popup_dpi_, 96);
    if (y < monitor.rcWork.top) y = icon.bottom + MulDiv(4, popup_dpi_, 96);
    y = std::clamp(y, static_cast<int>(monitor.rcWork.top),
        std::max(static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom) - height));
    SendMessageW(tooltip_, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
    SendMessageW(tooltip_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
}

void TrayIconController::HideTooltip() {
    if (!tooltip_) return;
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = notify_icon_data_.hWnd;
    tool.uId = kTrayIconId;
    SendMessageW(tooltip_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
}

bool TrayIconController::MeasureMenuItem(MEASUREITEMSTRUCT& item) const {
    if (item.CtlType != ODT_MENU || !item.itemData || !popup_font_) return false;
    const auto text = reinterpret_cast<const wchar_t*>(item.itemData);
    HDC dc = GetDC(notify_icon_data_.hWnd);
    const auto previous = SelectObject(dc, popup_font_);
    SIZE size{};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    SelectObject(dc, previous);
    ReleaseDC(notify_icon_data_.hWnd, dc);
    item.itemWidth = size.cx + MulDiv(40, popup_dpi_, 96);
    item.itemHeight = std::max(static_cast<int>(size.cy) + MulDiv(10, popup_dpi_, 96), MulDiv(32, popup_dpi_, 96));
    return true;
}

bool TrayIconController::DrawMenuItem(const DRAWITEMSTRUCT& item) const {
    if (item.CtlType != ODT_MENU || !item.itemData || !popup_font_) return false;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    FillRect(item.hDC, &item.rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
    const auto previous = SelectObject(item.hDC, popup_font_);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
    RECT bounds = item.rcItem;
    bounds.left += MulDiv(18, popup_dpi_, 96);
    bounds.right -= MulDiv(12, popup_dpi_, 96);
    DrawTextW(item.hDC, reinterpret_cast<const wchar_t*>(item.itemData), -1, &bounds,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(item.hDC, previous);
    return true;
}

NOTIFYICONDATAW TrayIconController::BuildNotifyIconData(HWND window, UINT callback_message) const {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = callback_message;
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(data.szTip, L"原生 Win32 截图工具", _TRUNCATE);
    return data;
}

}  // namespace ui
