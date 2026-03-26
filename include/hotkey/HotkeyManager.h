#pragma once

#include <Windows.h>

#include <string>

#include "hotkey/HotkeyDefinition.h"

namespace hotkey {

class HotkeyManager {
public:
    bool RegisterHotkey(HWND window,
                        int identifier,
                        const HotkeyDefinition& definition,
                        std::wstring& error_message) const;
    void UnregisterHotkey(HWND window, int identifier) const;
};

}  // namespace hotkey
