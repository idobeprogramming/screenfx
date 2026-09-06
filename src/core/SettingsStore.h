#pragma once

#include "Settings.h"

#include <filesystem>
#include <string>

namespace screenfx::core {

enum class SettingsLoadStatus {
    Loaded,
    NotFound,
    Invalid,
    UnsupportedVersion,
    IoError,
};

struct SettingsLoadResult {
    AppSettings settings{};
    SettingsLoadStatus status = SettingsLoadStatus::NotFound;
    std::wstring error;
};

class SettingsStore {
public:
    static AppSettings Load();
    static AppSettings Load(const std::filesystem::path& path);
    static SettingsLoadResult LoadWithStatus(const std::filesystem::path& path);
    static bool Save(const AppSettings& settings);
    static bool Save(const AppSettings& settings, const std::filesystem::path& path, std::wstring* error = nullptr);
    static std::filesystem::path Path();
};

} // namespace screenfx::core
