#pragma once

#include <Windows.h>

#include <string>

namespace hotkey {

struct HotkeyDefinition {
    UINT modifiers = MOD_CONTROL | MOD_SHIFT;
    UINT virtual_key = 'A';
};

HotkeyDefinition GetDefaultFullCaptureHotkey();
HotkeyDefinition GetDefaultRegionCaptureHotkey();
HotkeyDefinition GetDefaultWindowCaptureHotkey();
bool IsModifierKey(UINT virtual_key);
bool IsValid(const HotkeyDefinition& definition);
HotkeyDefinition BuildFromKeyboardState(UINT virtual_key);
std::wstring FormatHotkey(const HotkeyDefinition& definition);

}  // namespace hotkey
