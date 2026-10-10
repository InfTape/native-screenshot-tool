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
    RecordFull,
    RecordRegion,
    RecordWindow,
    StopRecording,
    ExitApplication,
};

class TrayIconController {
public:
    explicit TrayIconController(HINSTANCE instance);

    common::Result<void> AddIcon(HWND window, UINT callback_message);
    common::Result<void> ReAddIcon(HWND window, UINT callback_message);
    void RemoveIcon();
    bool ShowBalloon(const std::wstring& title, const std::wstring& message) const;
    std::optional<TrayMenuCommand> ShowContextMenu(HWND owner, const POINT& screen_point,
        bool recording = false, bool stopping = false);
    bool MeasureMenuItem(MEASUREITEMSTRUCT& item) const;
    bool DrawMenuItem(const DRAWITEMSTRUCT& item) const;
    void ShowTooltip(const POINT& anchor);
    void HideTooltip();
    bool UsesVersion4() const { return version4_; }
    bool IsInstalled() const;

private:
    common::Result<void> InstallIcon(DWORD message);
    NOTIFYICONDATAW BuildNotifyIconData(HWND window, UINT callback_message) const;
    bool PreparePopupFont(const POINT& anchor);

    HINSTANCE instance_ = nullptr;
    bool installed_ = false;
    NOTIFYICONDATAW notify_icon_data_{};
    HWND tooltip_ = nullptr;
    HFONT popup_font_ = nullptr;
    UINT popup_dpi_ = 96;
    bool version4_ = false;
    bool menu_open_ = false;
};

}  // namespace ui
