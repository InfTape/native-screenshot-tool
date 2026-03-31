#include "settings/SettingsRepository.h"

#include <Windows.h>

#include <cwctype>
#include <filesystem>
#include <vector>

#include "capture/ImageFileFormat.h"
#include "hotkey/HotkeyDefinition.h"

namespace {

constexpr wchar_t kHotkeySection[] = L"hotkeys";
constexpr wchar_t kCaptureSection[] = L"capture";
constexpr wchar_t kStartupSection[] = L"startup";
constexpr wchar_t kFullModifiersKey[] = L"full_capture_modifiers";
constexpr wchar_t kFullVirtualKeyKey[] = L"full_capture_virtual_key";
constexpr wchar_t kRegionModifiersKey[] = L"region_capture_modifiers";
constexpr wchar_t kRegionVirtualKeyKey[] = L"region_capture_virtual_key";
constexpr wchar_t kWindowModifiersKey[] = L"window_capture_modifiers";
constexpr wchar_t kWindowVirtualKeyKey[] = L"window_capture_virtual_key";
constexpr wchar_t kLaunchAtStartupKey[] = L"launch_at_startup";
constexpr wchar_t kSaveDirectoryKey[] = L"save_directory";
constexpr wchar_t kSaveFormatKey[] = L"save_format";

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

    return WritePrivateProfileStringW(
               kHotkeySection, modifiers_key, modifiers.c_str(), path.c_str()) != FALSE &&
           WritePrivateProfileStringW(
               kHotkeySection, virtual_key_key, virtual_key.c_str(), path.c_str()) != FALSE;
}

void LoadSaveDirectory(const std::wstring& path, settings::AppSettings& settings) {
    wchar_t buffer[32768]{};
    GetPrivateProfileStringW(
        kCaptureSection, kSaveDirectoryKey, L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    settings.save_directory = buffer;
}

void LoadLaunchAtStartup(const std::wstring& path, settings::AppSettings& settings) {
    settings.launch_at_startup = GetPrivateProfileIntW(
                                     kStartupSection,
                                     kLaunchAtStartupKey,
                                     settings.launch_at_startup ? 1 : 0,
                                     path.c_str()) != 0;
}

void LoadSaveFormat(const std::wstring& path, settings::AppSettings& settings) {
    wchar_t buffer[32]{};
    GetPrivateProfileStringW(
        kCaptureSection, kSaveFormatKey, L"png", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());

    std::wstring format_value = buffer;
    for (auto& character : format_value) {
        character = static_cast<wchar_t>(towlower(character));
    }

    settings.save_format =
        format_value == L"bmp" ? capture::ImageFileFormat::Bmp : capture::ImageFileFormat::Png;
}

bool SaveCaptureSettings(const std::wstring& path, const settings::AppSettings& settings) {
    const wchar_t* format_value =
        settings.save_format == capture::ImageFileFormat::Bmp ? L"bmp" : L"png";

    return WritePrivateProfileStringW(
               kCaptureSection, kSaveDirectoryKey, settings.save_directory.c_str(), path.c_str()) != FALSE &&
           WritePrivateProfileStringW(kCaptureSection, kSaveFormatKey, format_value, path.c_str()) != FALSE;
}

bool SaveLaunchAtStartup(const std::wstring& path, const settings::AppSettings& settings) {
    return WritePrivateProfileStringW(kStartupSection,
                                      kLaunchAtStartupKey,
                                      settings.launch_at_startup ? L"1" : L"0",
                                      path.c_str()) != FALSE;
}

}  // namespace

namespace settings {

common::Result<std::filesystem::path> SettingsRepository::ResolveSettingsPath() const {
    DWORD required_size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);

    std::filesystem::path base_directory;
    if (required_size > 0) {
        std::vector<wchar_t> buffer(required_size);
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required_size) == 0) {
            return common::Result<std::filesystem::path>::Failure(L"读取 LOCALAPPDATA 失败。");
        }
        base_directory = buffer.data();
    } else {
        wchar_t module_path[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) == 0) {
            return common::Result<std::filesystem::path>::Failure(L"获取程序目录失败。");
        }
        base_directory = std::filesystem::path(module_path).parent_path();
    }

    return common::Result<std::filesystem::path>::Success(
        base_directory / L"NativeScreenshot" / L"settings.ini");
}

common::Result<AppSettings> SettingsRepository::Load() const {
    AppSettings settings{};

    auto settings_path = ResolveSettingsPath();
    if (!settings_path) {
        return common::Result<AppSettings>::Failure(settings_path.Error());
    }

    const auto path_string = settings_path.Value().wstring();
    LoadHotkey(path_string, kFullModifiersKey, kFullVirtualKeyKey, settings.full_capture_hotkey);
    LoadHotkey(path_string,
               kRegionModifiersKey,
               kRegionVirtualKeyKey,
               settings.region_capture_hotkey);
    LoadHotkey(path_string,
               kWindowModifiersKey,
               kWindowVirtualKeyKey,
               settings.window_capture_hotkey);
    LoadLaunchAtStartup(path_string, settings);
    LoadSaveDirectory(path_string, settings);
    LoadSaveFormat(path_string, settings);

    return common::Result<AppSettings>::Success(std::move(settings));
}

common::Result<void> SettingsRepository::Save(const AppSettings& settings) const {
    auto settings_path = ResolveSettingsPath();
    if (!settings_path) {
        return common::Result<void>::Failure(settings_path.Error());
    }

    try {
        std::filesystem::create_directories(settings_path.Value().parent_path());
    } catch (const std::filesystem::filesystem_error&) {
        return common::Result<void>::Failure(L"创建设置目录失败。");
    }

    const auto path_string = settings_path.Value().wstring();
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
                    settings.window_capture_hotkey) ||
        !SaveLaunchAtStartup(path_string, settings) ||
        !SaveCaptureSettings(path_string, settings)) {
        return common::Result<void>::Failure(L"写入设置文件失败。");
    }

    return common::Result<void>::Success();
}

}  // namespace settings
