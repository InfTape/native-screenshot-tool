#pragma once

#include <string>

#include "capture/ImageFileFormat.h"
#include "hotkey/HotkeyDefinition.h"

namespace settings {

struct AppSettings {
    hotkey::HotkeyDefinition full_capture_hotkey = hotkey::GetDefaultFullCaptureHotkey();
    hotkey::HotkeyDefinition region_capture_hotkey = hotkey::GetDefaultRegionCaptureHotkey();
    hotkey::HotkeyDefinition window_capture_hotkey = hotkey::GetDefaultWindowCaptureHotkey();
    hotkey::HotkeyDefinition screen_recording_hotkey{MOD_CONTROL | MOD_SHIFT, 'R'};
    bool launch_at_startup = true;
    std::wstring save_directory;
    capture::ImageFileFormat save_format = capture::ImageFileFormat::Png;
};

}  // namespace settings
