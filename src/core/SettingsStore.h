#pragma once

#include "Settings.h"

#include <filesystem>
#include <string>

namespace screenfx::core {

class SettingsStore {
public:
    static AppSettings Load();
    static bool Save(const AppSettings& settings);
    static std::filesystem::path Path();

private:
    static AppSettings Defaults();
};

} // namespace screenfx::core
