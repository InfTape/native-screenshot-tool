#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "settings/AppSettings.h"

namespace settings {

class SettingsRepository {
public:
    bool Load(AppSettings& settings, std::wstring& error_message) const;
    bool Save(const AppSettings& settings, std::wstring& error_message) const;

private:
    std::optional<std::filesystem::path> ResolveSettingsPath(
        std::wstring& error_message) const;
};

}  // namespace settings
