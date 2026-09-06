#include "core/SettingsStore.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace screenfx::core;
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    Check(bool(file), "Fixture write failed");
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct Fixtures {
    std::filesystem::path directory = std::filesystem::current_path() /
        (L"settings-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Fixtures() { Check(std::filesystem::create_directory(directory), "Fixture directory already exists"); }
    ~Fixtures() { std::error_code error; std::filesystem::remove_all(directory, error); }
};
}

int main() {
    try {
        Fixtures fixture;
        const auto path = fixture.directory / L"réglages.json";
        Check(SettingsStore::LoadWithStatus(path).status == SettingsLoadStatus::NotFound, "Missing file status");
        AppSettings settings;
        settings.enabled = true; settings.monitorIndex = 42; settings.framePacing = FramePacingMode::VSync;
        const std::vector<float EffectSettings::*> fields = {
            &EffectSettings::globalIntensity, &EffectSettings::brightness, &EffectSettings::contrast,
            &EffectSettings::saturation, &EffectSettings::gamma, &EffectSettings::grayscale, &EffectSettings::sepia,
            &EffectSettings::scanlineIntensity, &EffectSettings::scanlineSpacing, &EffectSettings::scanlineThickness,
            &EffectSettings::phosphorIntensity, &EffectSettings::pixelSize, &EffectSettings::sharpen,
            &EffectSettings::chromaticAberration, &EffectSettings::bloomIntensity, &EffectSettings::bloomThreshold,
            &EffectSettings::bloomRadius, &EffectSettings::vignetteIntensity, &EffectSettings::vignetteWidth,
            &EffectSettings::grainIntensity, &EffectSettings::grainSize};
        for (auto member : fields) settings.effects.*member = 1.0F;
        settings.effects.brightness = -0.321987F;
        std::wstring error;
        Check(SettingsStore::Save(settings, path, &error) && error.empty(), "Save valid settings");
        auto loaded = SettingsStore::LoadWithStatus(path);
        Check(loaded.status == SettingsLoadStatus::Loaded && loaded.error.empty(), "Roundtrip load status");
        Check(loaded.settings.enabled && loaded.settings.monitorIndex == 42 &&
              loaded.settings.framePacing == FramePacingMode::VSync, "Application settings roundtrip");
        for (auto member : fields) Check(loaded.settings.effects.*member == settings.effects.*member, "Effect roundtrip");
        auto original = Read(path);
        settings.effects.gamma = std::numeric_limits<float>::quiet_NaN();
        Check(!SettingsStore::Save(settings, path, &error) && !error.empty() && Read(path) == original, "Reject NaN without overwrite");
        settings.effects.gamma = std::numeric_limits<float>::infinity();
        Check(!SettingsStore::Save(settings, path) && Read(path) == original, "Reject infinity");
        settings = {};
        const std::vector<std::string> invalid = {
            "", "{}", "[]", "null", "{\"version\":1}garbage", "{\"version\":1,}",
            "{\"version\":01}", "{\"version\":1,\"version\":1}", "{\"version\":\"1\"}",
            "{\"version\":1,\"enabled\":1}", "{\"version\":1,\"enabled\":trueXYZ}",
            "{\"version\":1,\"monitorIndex\":-1}", "{\"version\":1,\"monitorIndex\":4294967296}",
            "{\"version\":1,\"monitorIndex\":1.5}", "{\"version\":1,\"framePacing\":\"other\"}",
            "{\"version\":1,\"effects\":[]}", "{\"version\":1,\"effects\":{\"gamma\":true}}",
            "{\"version\":1,\"effects\":{\"gamma\":1e400}}", "{\"version\":1,\"effects\":{\"gamma\":NaN}}",
            "{\"version\":1,\"effects\":{\"gamma\":1,\"gamma\":2}}",
            "{\"version\":1,\"unknown\":[true,]}", "{\"version\":1,\"unknown\":\"\\uD800\"}",
            "{\"version\":1,\"unknown\":\"\xFF\"}", std::string(65537, ' '),
            "{\"version\":1,\"unknown\":" + std::string(32, '[') + "0" + std::string(32, ']') + "}"};
        for (const auto& document : invalid) {
            Write(path, document);
            auto rejected = SettingsStore::LoadWithStatus(path);
            Check(rejected.status == SettingsLoadStatus::Invalid && !rejected.settings.enabled, "Reject malformed JSON with disabled defaults");
            Check(!SettingsStore::Save(settings, path, &error) && !error.empty() && Read(path) == document, "Preserve invalid file");
        }
        Write(path, "{\"version\":2,\"enabled\":true}");
        Check(SettingsStore::LoadWithStatus(path).status == SettingsLoadStatus::UnsupportedVersion, "Future version status");
        original = Read(path);
        Check(!SettingsStore::Save(settings, path) && Read(path) == original, "Preserve future version");
        Write(path, "\xEF\xBB\xBF{\"version\":1,\"unknown\":[{},false,null,\"\\uD83D\\uDE00\"],\"effects\":{\"gamma\":-2,\"brightness\":-50,\"pixelSize\":900}}");
        loaded = SettingsStore::LoadWithStatus(path);
        Check(loaded.status == SettingsLoadStatus::Loaded && loaded.settings.effects.gamma == 0.1F &&
              loaded.settings.effects.brightness == -1 && loaded.settings.effects.pixelSize == 32, "BOM, unknown fields, clamping");
        settings.effects.contrast = 100;
        Check(SettingsStore::Save(settings, path) && SettingsStore::Load(path).effects.contrast == 4, "Clamp on save");
        original = Read(path);
        Check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "Read-only fixture");
        const bool readOnlySaved = SettingsStore::Save(settings, path, &error);
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        Check(!readOnlySaved && !error.empty() && Read(path) == original, "Failed replacement preserves existing file");
        Check(SettingsStore::LoadWithStatus(fixture.directory).status == SettingsLoadStatus::IoError, "Directory is not file");
        Check(!SettingsStore::Save(settings, {}) && SettingsStore::LoadWithStatus({}).status == SettingsLoadStatus::IoError, "Missing storage path");
        Check(std::distance(std::filesystem::directory_iterator(fixture.directory), std::filesystem::directory_iterator{}) == 1,
              "Temporary files leaked after failure");
        std::cout << "PASS: settings roundtrip, JSON validation, bounds, versions, atomic save failures and preservation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
