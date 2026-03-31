#pragma once

#include <Windows.h>

#include "common/Result.h"
#include "hotkey/HotkeyDefinition.h"

namespace hotkey {

class HotkeyManager {
public:
    common::Result<void> RegisterHotkey(HWND window,
                                        int identifier,
                                        const HotkeyDefinition& definition) const;
    void UnregisterHotkey(HWND window, int identifier) const;
};

}  // namespace hotkey
