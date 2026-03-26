#pragma once

#include "hotkey/HotkeyDefinition.h"

namespace settings {

struct AppSettings {
    hotkey::HotkeyDefinition full_capture_hotkey = hotkey::GetDefaultFullCaptureHotkey();
    hotkey::HotkeyDefinition region_capture_hotkey = hotkey::GetDefaultRegionCaptureHotkey();
    hotkey::HotkeyDefinition window_capture_hotkey = hotkey::GetDefaultWindowCaptureHotkey();
};

}  // namespace settings
