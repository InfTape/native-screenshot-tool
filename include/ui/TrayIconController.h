#pragma once

#include <Windows.h>
#include <shellapi.h>

#include <optional>
#include <string>

#include "common/Result.h"

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

    common::Result<void> AddIcon(HWND window, UINT callback_message);
    common::Result<void> ReAddIcon(HWND window, UINT callback_message);
    void RemoveIcon();
    bool ShowBalloon(const std::wstring& title, const std::wstring& message) const;
    std::optional<TrayMenuCommand> ShowContextMenu(HWND owner, const POINT& screen_point) const;
    bool IsInstalled() const;

private:
    common::Result<void> InstallIcon(DWORD message);
    NOTIFYICONDATAW BuildNotifyIconData(HWND window, UINT callback_message) const;

    HINSTANCE instance_ = nullptr;
    bool installed_ = false;
    NOTIFYICONDATAW notify_icon_data_{};
};

}  // namespace ui
