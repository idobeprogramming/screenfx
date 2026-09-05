#include "SettingsStore.h"

#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>

namespace screenfx::core {
namespace {

constexpr int kSettingsVersion = 1;

std::string FindValue(const std::string& document, const char* key) {
    const std::string quotedKey = std::string("\"") + key + "\"";
    const std::size_t keyPosition = document.find(quotedKey);
    if (keyPosition == std::string::npos) {
        return {};
    }
    const std::size_t colon = document.find(':', keyPosition + quotedKey.size());
    if (colon == std::string::npos) {
        return {};
    }
    const std::size_t valueStart = document.find_first_not_of(" \t\r\n", colon + 1);
    if (valueStart == std::string::npos) {
        return {};
    }
    if (document[valueStart] == '"') {
        const std::size_t valueEnd = document.find('"', valueStart + 1);
        return valueEnd == std::string::npos ? std::string{} : document.substr(valueStart + 1, valueEnd - valueStart - 1);
    }
    const std::size_t valueEnd = document.find_first_of(",}\r\n", valueStart);
    return document.substr(valueStart, valueEnd == std::string::npos ? std::string::npos : valueEnd - valueStart);
}

std::string Trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

float Number(const std::string& document, const char* key, float fallback, float minimum, float maximum) {
    const std::string value = Trim(FindValue(document, key));
    if (value.empty()) {
        return fallback;
    }
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE || !std::isfinite(parsed)) {
        return fallback;
    }
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0') {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

bool Boolean(const std::string& document, const char* key, bool fallback) {
    const std::string value = Trim(FindValue(document, key));
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    return fallback;
}

std::uint32_t Unsigned(const std::string& document, const char* key, std::uint32_t fallback) {
    const std::string value = Trim(FindValue(document, key));
    if (value.empty()) {
        return fallback;
    }
    std::uint32_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    return result.ec != std::errc{} || result.ptr != value.data() + value.size() ? fallback : parsed;
}

std::string ToJson(const AppSettings& settings) {
    const auto& effects = settings.effects;
    std::ostringstream document;
    document << std::setprecision(std::numeric_limits<float>::max_digits10);
    document << "{\n"
             << "  \"version\": " << kSettingsVersion << ",\n"
             << "  \"enabled\": " << (settings.enabled ? "true" : "false") << ",\n"
             << "  \"monitorIndex\": " << settings.monitorIndex << ",\n"
             << "  \"framePacing\": \"" << (settings.framePacing == FramePacingMode::VSync ? "vsync" : "uncapped") << "\",\n"
             << "  \"effects\": {\n"
             << "    \"globalIntensity\": " << effects.globalIntensity << ",\n"
             << "    \"brightness\": " << effects.brightness << ",\n"
             << "    \"contrast\": " << effects.contrast << ",\n"
             << "    \"saturation\": " << effects.saturation << ",\n"
             << "    \"gamma\": " << effects.gamma << ",\n"
             << "    \"grayscale\": " << effects.grayscale << ",\n"
             << "    \"sepia\": " << effects.sepia << ",\n"
             << "    \"scanlineIntensity\": " << effects.scanlineIntensity << ",\n"
             << "    \"scanlineSpacing\": " << effects.scanlineSpacing << ",\n"
             << "    \"scanlineThickness\": " << effects.scanlineThickness << ",\n"
             << "    \"phosphorIntensity\": " << effects.phosphorIntensity << ",\n"
             << "    \"pixelSize\": " << effects.pixelSize << ",\n"
             << "    \"sharpen\": " << effects.sharpen << ",\n"
             << "    \"chromaticAberration\": " << effects.chromaticAberration << ",\n"
             << "    \"bloomIntensity\": " << effects.bloomIntensity << ",\n"
             << "    \"bloomThreshold\": " << effects.bloomThreshold << ",\n"
             << "    \"bloomRadius\": " << effects.bloomRadius << ",\n"
             << "    \"vignetteIntensity\": " << effects.vignetteIntensity << ",\n"
             << "    \"vignetteWidth\": " << effects.vignetteWidth << ",\n"
             << "    \"grainIntensity\": " << effects.grainIntensity << ",\n"
             << "    \"grainSize\": " << effects.grainSize << "\n"
             << "  }\n"
             << "}\n";
    return document.str();
}

AppSettings FromJson(const std::string& document) {
    AppSettings settings;
    const std::string version = Trim(FindValue(document, "version"));
    if (!version.empty()) {
        std::uint32_t parsedVersion = 0;
        const auto result = std::from_chars(version.data(), version.data() + version.size(), parsedVersion, 10);
        if (result.ec != std::errc{} || result.ptr != version.data() + version.size() || parsedVersion != kSettingsVersion) {
            return settings;
        }
    }
    settings.enabled = Boolean(document, "enabled", false);
    settings.monitorIndex = Unsigned(document, "monitorIndex", 0);
    if (FindValue(document, "framePacing") == "vsync") {
        settings.framePacing = FramePacingMode::VSync;
    }

    auto& target = settings.effects;
    target.globalIntensity = Number(document, "globalIntensity", target.globalIntensity, 0.0F, 1.0F);
    target.brightness = Number(document, "brightness", target.brightness, -1.0F, 1.0F);
    target.contrast = Number(document, "contrast", target.contrast, 0.0F, 4.0F);
    target.saturation = Number(document, "saturation", target.saturation, 0.0F, 4.0F);
    target.gamma = Number(document, "gamma", target.gamma, 0.1F, 4.0F);
    target.grayscale = Number(document, "grayscale", target.grayscale, 0.0F, 1.0F);
    target.sepia = Number(document, "sepia", target.sepia, 0.0F, 1.0F);
    target.scanlineIntensity = Number(document, "scanlineIntensity", target.scanlineIntensity, 0.0F, 1.0F);
    target.scanlineSpacing = Number(document, "scanlineSpacing", target.scanlineSpacing, 1.0F, 8.0F);
    target.scanlineThickness = Number(document, "scanlineThickness", target.scanlineThickness, 0.05F, 1.0F);
    target.phosphorIntensity = Number(document, "phosphorIntensity", target.phosphorIntensity, 0.0F, 1.0F);
    target.pixelSize = Number(document, "pixelSize", target.pixelSize, 1.0F, 32.0F);
    target.sharpen = Number(document, "sharpen", target.sharpen, 0.0F, 2.0F);
    target.chromaticAberration = Number(document, "chromaticAberration", target.chromaticAberration, 0.0F, 8.0F);
    target.bloomIntensity = Number(document, "bloomIntensity", target.bloomIntensity, 0.0F, 2.0F);
    target.bloomThreshold = Number(document, "bloomThreshold", target.bloomThreshold, 0.0F, 1.0F);
    target.bloomRadius = Number(document, "bloomRadius", target.bloomRadius, 0.5F, 8.0F);
    target.vignetteIntensity = Number(document, "vignetteIntensity", target.vignetteIntensity, 0.0F, 1.0F);
    target.vignetteWidth = Number(document, "vignetteWidth", target.vignetteWidth, 0.1F, 1.0F);
    target.grainIntensity = Number(document, "grainIntensity", target.grainIntensity, 0.0F, 1.0F);
    target.grainSize = Number(document, "grainSize", target.grainSize, 0.25F, 8.0F);
    return settings;
}

} // namespace

AppSettings SettingsStore::Defaults() {
    return AppSettings{};
}

std::filesystem::path SettingsStore::Path() {
    PWSTR knownFolder = nullptr;
    std::filesystem::path directory;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &knownFolder)) && knownFolder != nullptr) {
        directory = std::filesystem::path(knownFolder) / L"ScreenFX";
        CoTaskMemFree(knownFolder);
    } else {
        try {
            directory = std::filesystem::temp_directory_path() / L"ScreenFX";
        } catch (...) {
            directory = std::filesystem::path(L".") / L"ScreenFX";
        }
    }
    return directory / L"settings.json";
}

AppSettings SettingsStore::Load() {
    AppSettings defaults = Defaults();
    try {
        std::ifstream file(Path(), std::ios::binary);
        if (!file) {
            return defaults;
        }
        const std::string document((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::string trimmed = Trim(document);
        if (trimmed.empty() || trimmed.front() != '{') {
            return defaults;
        }
        return FromJson(trimmed);
    } catch (...) {
        return defaults;
    }
}

bool SettingsStore::Save(const AppSettings& settings) {
    try {
        const auto path = Path();
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.wstring() + L".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                return false;
            }
            file << ToJson(settings);
            file.flush();
            if (!file.good()) {
                file.close();
                DeleteFileW(temporary.c_str());
                return false;
            }
        }
        const bool replaced = MoveFileExW(temporary.c_str(), path.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!replaced) {
            DeleteFileW(temporary.c_str());
        }
        return replaced;
    } catch (...) {
        return false;
    }
}

} // namespace screenfx::core
