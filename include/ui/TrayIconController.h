#pragma once

#include <Windows.h>
#include <shellapi.h>

#include <optional>
#include <string>

namespace ui {

enum class TrayMenuCommand {
    None,
    ShowWindow,
    CaptureRegion,
    CaptureFull,
    CaptureWindow,
    ExitApplication,
};

class TrayIconController {
public:
    explicit TrayIconController(HINSTANCE instance);

    bool AddIcon(HWND window, UINT callback_message, std::wstring& error_message);
    bool ReAddIcon(HWND window, UINT callback_message, std::wstring& error_message);
    void RemoveIcon();
    bool ShowBalloon(const std::wstring& title, const std::wstring& message) const;
    std::optional<TrayMenuCommand> ShowContextMenu(HWND owner, const POINT& screen_point) const;
    bool IsInstalled() const;

private:
    bool InstallIcon(DWORD message, std::wstring& error_message);
    NOTIFYICONDATAW BuildNotifyIconData(HWND window, UINT callback_message) const;

    HINSTANCE instance_ = nullptr;
    bool installed_ = false;
    NOTIFYICONDATAW notify_icon_data_{};
};

}  // namespace ui
