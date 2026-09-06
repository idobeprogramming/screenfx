#pragma once

#include "Settings.h"

#include <filesystem>
#include <string>
#include <vector>

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

inline constexpr std::size_t kMaximumPresets = 64;
inline constexpr std::size_t kMaximumPresetNameLength = 80;

struct Preset {
    std::wstring name;
    EffectSettings effects{};
};

struct PresetsLoadResult {
    std::vector<Preset> presets;
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
    static std::filesystem::path PresetsPath();
    static PresetsLoadResult LoadPresets(const std::filesystem::path& path);
    static bool SavePresets(const std::vector<Preset>& presets, const std::filesystem::path& path,
                            std::wstring* error = nullptr);
};

} // namespace screenfx::core
