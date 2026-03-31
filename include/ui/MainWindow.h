#pragma once

#include <Windows.h>

#include <array>
#include <optional>
#include <string>

#include "common/Result.h"
#include "capture/AutoSaveService.h"
#include "capture/CapturedImage.h"
#include "capture/DesktopSnapshot.h"
#include "capture/ScreenCaptureService.h"
#include "capture/WindowCaptureService.h"
#include "hotkey/HotkeyManager.h"
#include "settings/AppSettings.h"
#include "settings/SettingsRepository.h"
#include "ui/RegionSelectionOverlay.h"
#include "ui/TrayIconController.h"
#include "ui/WindowSelectionOverlay.h"

namespace ui {

enum class HotkeySlot {
    None,
    FullCapture,
    RegionCapture,
    WindowCapture,
};

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);

    bool Create();
    void Show(int nCmdShow, bool start_hidden_to_tray);

private:
    struct SaveImageOutcome {
        std::wstring saved_path;
        std::wstring clipboard_error_message;
        bool clipboard_failed = false;
    };

    static constexpr wchar_t kClassName[] = L"NativeScreenshot.MainWindow";
    static constexpr std::size_t kHotkeySlotCount = 3;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    bool CreateChildControls();
    void CreateUIFont();
    void ApplyFontToAllControls();
    bool InitializeHotkeySettings();
    bool InitializeTrayIcon();
    void LayoutControls(int client_width, int client_height);
    void UpdateStatus(const std::wstring& text) const;
    void UpdateHotkeyLabels() const;
    void UpdateActionState() const;
    bool CaptureDesktop();
    bool CaptureRegion();
    bool CaptureWindow();
    void SaveCapture();
    void ClearCapture();
    void BeginHotkeyRecording(HotkeySlot slot);
    void CancelHotkeyRecording();
    void CommitRecordedHotkey(UINT virtual_key);
    bool ApplyRecordedHotkey(HotkeySlot slot, const hotkey::HotkeyDefinition& definition);
    common::Result<void> RegisterConfiguredHotkeys();
    void UnregisterAllHotkeys();
    bool TriggerCaptureByHotkey(int hotkey_identifier);
    void HideToTray(bool show_hint = true);
    void ShowFromTray();
    void ExitApplication();
    void HandleTrayCommand(TrayMenuCommand command);
    common::Result<capture::DesktopSnapshot> CaptureSnapshot() const;
    common::Result<SaveImageOutcome> SaveImageToConfiguredDirectory(
        const capture::CapturedImage& image) const;
    bool FinalizeCapturedImage(const std::wstring& capture_name);
    void UpdateSaveSettingsDisplay() const;
    void UpdateStartupOptionDisplay() const;
    void UpdateSaveButtonLabel() const;
    bool EnsureSaveDirectoryConfigured();
    common::Result<std::wstring> ResolveDefaultSaveDirectory() const;
    bool ChooseSaveDirectory(std::wstring& selected_directory) const;
    void ApplySaveFormatSelection();
    common::Result<void> SyncLaunchAtStartupSetting(bool enabled) const;
    void ToggleLaunchAtStartup();
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HFONT ui_font_ = nullptr;
    HWND full_capture_button_ = nullptr;
    HWND region_capture_button_ = nullptr;
    HWND window_capture_button_ = nullptr;
    HWND save_directory_button_ = nullptr;
    HWND save_directory_label_ = nullptr;
    HWND save_format_combo_ = nullptr;
    HWND launch_at_startup_checkbox_ = nullptr;
    HWND save_button_ = nullptr;
    HWND clear_button_ = nullptr;
    HWND status_label_ = nullptr;
    std::array<HWND, kHotkeySlotCount> hotkey_buttons_{};
    std::array<HWND, kHotkeySlotCount> hotkey_labels_{};
    bool capture_in_progress_ = false;
    HotkeySlot recording_hotkey_slot_ = HotkeySlot::None;
    std::array<bool, kHotkeySlotCount> hotkey_registered_{};
    bool hidden_to_tray_ = false;
    bool exiting_ = false;
    bool tray_hint_shown_ = false;
    UINT taskbar_created_message_ = 0;
    capture::CapturedImage image_;
    capture::ScreenCaptureService capture_service_;
    capture::WindowCaptureService window_capture_service_;
    capture::AutoSaveService auto_save_service_;
    settings::AppSettings settings_;
    settings::SettingsRepository settings_repository_;
    hotkey::HotkeyManager hotkey_manager_;
    RegionSelectionOverlay region_selection_overlay_;
    WindowSelectionOverlay window_selection_overlay_;
    TrayIconController tray_icon_controller_;
};

}  // namespace ui
