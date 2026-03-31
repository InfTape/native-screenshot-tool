#pragma once

#include <filesystem>

#include "common/Result.h"
#include "settings/AppSettings.h"

namespace settings {

class SettingsRepository {
public:
    common::Result<AppSettings> Load() const;
    common::Result<void> Save(const AppSettings& settings) const;

private:
    common::Result<std::filesystem::path> ResolveSettingsPath() const;
};

}  // namespace settings
