#include "hotkey/HotkeyManager.h"

#include "common/Win32Error.h"

namespace hotkey {

common::Result<void> HotkeyManager::RegisterHotkey(HWND window,
                                                   int identifier,
                                                   const HotkeyDefinition& definition) const {
    UnregisterHotkey(window, identifier);

    if (!RegisterHotKey(
            window, identifier, definition.modifiers | MOD_NOREPEAT, definition.virtual_key)) {
        return common::Result<void>::Failure(
            L"注册全局快捷键失败。\n\n" + common::GetLastErrorMessage(GetLastError()) +
            L"\n\n这通常表示该组合键已经被其他程序占用。");
    }

    return common::Result<void>::Success();
}

void HotkeyManager::UnregisterHotkey(HWND window, int identifier) const {
    if (window != nullptr) {
        UnregisterHotKey(window, identifier);
    }
}

}  // namespace hotkey
