#include "settings/SettingsRepository.h"

#include <Windows.h>

#include <filesystem>
#include <vector>

#include "hotkey/HotkeyDefinition.h"

namespace {

constexpr wchar_t kHotkeySection[] = L"hotkeys";
constexpr wchar_t kFullModifiersKey[] = L"full_capture_modifiers";
constexpr wchar_t kFullVirtualKeyKey[] = L"full_capture_virtual_key";
constexpr wchar_t kRegionModifiersKey[] = L"region_capture_modifiers";
constexpr wchar_t kRegionVirtualKeyKey[] = L"region_capture_virtual_key";
constexpr wchar_t kWindowModifiersKey[] = L"window_capture_modifiers";
constexpr wchar_t kWindowVirtualKeyKey[] = L"window_capture_virtual_key";

void LoadHotkey(const std::wstring& path,
                const wchar_t* modifiers_key,
                const wchar_t* virtual_key_key,
                hotkey::HotkeyDefinition& definition) {
    const UINT modifiers =
        GetPrivateProfileIntW(kHotkeySection, modifiers_key, definition.modifiers, path.c_str());
    const UINT virtual_key =
        GetPrivateProfileIntW(kHotkeySection, virtual_key_key, definition.virtual_key, path.c_str());

    const hotkey::HotkeyDefinition loaded_hotkey{modifiers, virtual_key};
    if (hotkey::IsValid(loaded_hotkey)) {
        definition = loaded_hotkey;
    }
}

bool SaveHotkey(const std::wstring& path,
                const wchar_t* modifiers_key,
                const wchar_t* virtual_key_key,
                const hotkey::HotkeyDefinition& definition) {
    const std::wstring modifiers = std::to_wstring(definition.modifiers);
    const std::wstring virtual_key = std::to_wstring(definition.virtual_key);

    return WritePrivateProfileStringW(kHotkeySection,
                                      modifiers_key,
                                      modifiers.c_str(),
                                      path.c_str()) != FALSE &&
           WritePrivateProfileStringW(kHotkeySection,
                                      virtual_key_key,
                                      virtual_key.c_str(),
                                      path.c_str()) != FALSE;
}

}  // namespace

namespace settings {

std::optional<std::filesystem::path> SettingsRepository::ResolveSettingsPath(
    std::wstring& error_message) const {
    DWORD required_size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);

    std::filesystem::path base_directory;
    if (required_size > 0) {
        std::vector<wchar_t> buffer(required_size);
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required_size) == 0) {
            error_message = L"读取 LOCALAPPDATA 失败。";
            return std::nullopt;
        }
        base_directory = buffer.data();
    } else {
        wchar_t module_path[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) == 0) {
            error_message = L"获取程序目录失败。";
            return std::nullopt;
        }
        base_directory = std::filesystem::path(module_path).parent_path();
    }

    return base_directory / L"NativeScreenshot" / L"settings.ini";
}

bool SettingsRepository::Load(AppSettings& settings, std::wstring& error_message) const {
    settings = AppSettings{};

    const auto settings_path = ResolveSettingsPath(error_message);
    if (!settings_path.has_value()) {
        return false;
    }

    const auto path_string = settings_path->wstring();
    LoadHotkey(path_string, kFullModifiersKey, kFullVirtualKeyKey, settings.full_capture_hotkey);
    LoadHotkey(path_string,
               kRegionModifiersKey,
               kRegionVirtualKeyKey,
               settings.region_capture_hotkey);
    LoadHotkey(path_string,
               kWindowModifiersKey,
               kWindowVirtualKeyKey,
               settings.window_capture_hotkey);

    return true;
}

bool SettingsRepository::Save(const AppSettings& settings, std::wstring& error_message) const {
    const auto settings_path = ResolveSettingsPath(error_message);
    if (!settings_path.has_value()) {
        return false;
    }

    try {
        std::filesystem::create_directories(settings_path->parent_path());
    } catch (const std::filesystem::filesystem_error&) {
        error_message = L"创建设置目录失败。";
        return false;
    }

    const auto path_string = settings_path->wstring();
    if (!SaveHotkey(path_string,
                    kFullModifiersKey,
                    kFullVirtualKeyKey,
                    settings.full_capture_hotkey) ||
        !SaveHotkey(path_string,
                    kRegionModifiersKey,
                    kRegionVirtualKeyKey,
                    settings.region_capture_hotkey) ||
        !SaveHotkey(path_string,
                    kWindowModifiersKey,
                    kWindowVirtualKeyKey,
                    settings.window_capture_hotkey)) {
        error_message = L"写入设置文件失败。";
        return false;
    }

    return true;
}

}  // namespace settings
