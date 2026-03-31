#include "ui/TrayIconController.h"

#include <shellapi.h>

#include "common/Win32Error.h"

namespace {

constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayShowWindowMenuId = 3001;
constexpr UINT kTrayCaptureRegionMenuId = 3002;
constexpr UINT kTrayCaptureFullMenuId = 3003;
constexpr UINT kTrayCaptureWindowMenuId = 3004;
constexpr UINT kTrayExitMenuId = 3005;

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
    notify_icon_data_ = BuildNotifyIconData(window, callback_message);
    installed_ = false;
    return InstallIcon(NIM_ADD);
}

void TrayIconController::RemoveIcon() {
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
                                                                   const POINT& screen_point) const {
    if (!installed_) {
        return std::nullopt;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }

    AppendMenuW(menu, MF_STRING, kTrayShowWindowMenuId, L"显示主窗口");
    AppendMenuW(menu, MF_STRING, kTrayCaptureRegionMenuId, L"选区截图");
    AppendMenuW(menu, MF_STRING, kTrayCaptureFullMenuId, L"全屏截图");
    AppendMenuW(menu, MF_STRING, kTrayCaptureWindowMenuId, L"窗口截图");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitMenuId, L"退出");

    SetForegroundWindow(owner);
    const UINT command = TrackPopupMenu(menu,
                                        TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screen_point.x,
                                        screen_point.y,
                                        0,
                                        owner,
                                        nullptr);
    DestroyMenu(menu);

    switch (command) {
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
    return common::Result<void>::Success();
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
