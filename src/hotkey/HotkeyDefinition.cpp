#include "hotkey/HotkeyDefinition.h"

#include <string>

namespace {

bool IsPressed(int virtual_key) {
    return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

std::wstring FormatVirtualKey(UINT virtual_key) {
    if ((virtual_key >= 'A' && virtual_key <= 'Z') || (virtual_key >= '0' && virtual_key <= '9')) {
        return std::wstring(1, static_cast<wchar_t>(virtual_key));
    }

    if (virtual_key >= VK_F1 && virtual_key <= VK_F24) {
        return L"F" + std::to_wstring(virtual_key - VK_F1 + 1);
    }

    UINT scan_code = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    LONG key_name_lparam = static_cast<LONG>(scan_code << 16);

    switch (virtual_key) {
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_END:
    case VK_HOME:
    case VK_INSERT:
    case VK_DELETE:
    case VK_DIVIDE:
    case VK_NUMLOCK:
        key_name_lparam |= 1 << 24;
        break;
    default:
        break;
    }

    wchar_t buffer[64]{};
    if (GetKeyNameTextW(key_name_lparam, buffer, static_cast<int>(std::size(buffer))) > 0) {
        return buffer;
    }

    return L"VK " + std::to_wstring(virtual_key);
}

}  // namespace

namespace hotkey {

HotkeyDefinition GetDefaultFullCaptureHotkey() {
    return HotkeyDefinition{MOD_CONTROL | MOD_SHIFT, 'F'};
}

HotkeyDefinition GetDefaultRegionCaptureHotkey() {
    return HotkeyDefinition{MOD_CONTROL | MOD_SHIFT, 'A'};
}

HotkeyDefinition GetDefaultWindowCaptureHotkey() {
    return HotkeyDefinition{MOD_CONTROL | MOD_SHIFT, 'W'};
}

bool IsModifierKey(UINT virtual_key) {
    switch (virtual_key) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

bool IsValid(const HotkeyDefinition& definition) {
    return definition.modifiers != 0 && definition.virtual_key != 0 &&
           !IsModifierKey(definition.virtual_key);
}

HotkeyDefinition BuildFromKeyboardState(UINT virtual_key) {
    UINT modifiers = 0;

    if (IsPressed(VK_CONTROL) || IsPressed(VK_LCONTROL) || IsPressed(VK_RCONTROL)) {
        modifiers |= MOD_CONTROL;
    }

    if (IsPressed(VK_SHIFT) || IsPressed(VK_LSHIFT) || IsPressed(VK_RSHIFT)) {
        modifiers |= MOD_SHIFT;
    }

    if (IsPressed(VK_MENU) || IsPressed(VK_LMENU) || IsPressed(VK_RMENU)) {
        modifiers |= MOD_ALT;
    }

    if (IsPressed(VK_LWIN) || IsPressed(VK_RWIN)) {
        modifiers |= MOD_WIN;
    }

    return HotkeyDefinition{modifiers, virtual_key};
}

std::wstring FormatHotkey(const HotkeyDefinition& definition) {
    if (!IsValid(definition)) {
        return L"未设置";
    }

    std::wstring text;
    if ((definition.modifiers & MOD_CONTROL) != 0) {
        text += L"Ctrl+";
    }
    if ((definition.modifiers & MOD_SHIFT) != 0) {
        text += L"Shift+";
    }
    if ((definition.modifiers & MOD_ALT) != 0) {
        text += L"Alt+";
    }
    if ((definition.modifiers & MOD_WIN) != 0) {
        text += L"Win+";
    }

    text += FormatVirtualKey(definition.virtual_key);
    return text;
}

}  // namespace hotkey
