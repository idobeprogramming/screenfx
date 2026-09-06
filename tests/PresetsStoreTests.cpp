#include "core/SettingsStore.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

using namespace screenfx::core;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, const std::string& document) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc); file << document;
    Check(bool(file), "Fixture write failed");
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct Fixture {
    std::filesystem::path folder = std::filesystem::current_path() /
        (L"presets-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Fixture() { Check(std::filesystem::create_directory(folder), "Fixture directory already exists"); }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(folder, error); }
};
}
int main() {
    try {
        Fixture fixture;
        const auto path = fixture.folder / L"presets.json";
        Check(SettingsStore::LoadPresets(path).status == SettingsLoadStatus::NotFound && !std::filesystem::exists(path),
              "Loading missing presets must not create a file");
        Preset custom{L"My \"CRT\" \\ café \U0001F3A8"};
        custom.effects.brightness = -0.23145F; custom.effects.scanlineIntensity = 0.7F;
        custom.effects.tintRed = 0.1F; custom.effects.tintGreen = 0.75F;
        custom.effects.tintBlue = 0.31234F; custom.effects.tintIntensity = 0.625F;
        std::wstring error;
        Check(SettingsStore::SavePresets({custom, {L"Neutral", {}}}, path, &error) && error.empty(), "Save custom presets");
        auto loaded = SettingsStore::LoadPresets(path);
        Check(loaded.status == SettingsLoadStatus::Loaded && loaded.presets.size() == 2, "Read saved presets");
        Check(loaded.presets[0].name == custom.name && loaded.presets[0].effects.brightness == custom.effects.brightness &&
              loaded.presets[0].effects.scanlineIntensity == 0.7F && loaded.presets[0].effects.tintRed == 0.1F &&
              loaded.presets[0].effects.tintGreen == 0.75F && loaded.presets[0].effects.tintBlue == 0.31234F &&
              loaded.presets[0].effects.tintIntensity == 0.625F, "Unicode names, effects and tint exact roundtrip");
        auto original = Read(path);
        Check(original.find("enabled") == std::string::npos && original.find("monitorIndex") == std::string::npos &&
              original.find("framePacing") == std::string::npos, "Presets contain effects only");
        const std::vector<std::wstring> invalidNames{L"", L"   ", L"bad\nname", std::wstring(81, L'x'),
            std::wstring(1, wchar_t(0xD800)), std::wstring(L"a\0b", 3)};
        for (const auto& name : invalidNames) {
            Check(!SettingsStore::SavePresets({{name, {}}}, path, &error) && !error.empty() && Read(path) == original,
                  "Reject invalid names without overwriting presets");
        }
        Check(!SettingsStore::SavePresets({{L"CRT", {}}, {L"crt", {}}}, path) && Read(path) == original,
              "Reject duplicate names ignoring case");
        custom.effects.tintRed = std::numeric_limits<float>::quiet_NaN();
        Check(!SettingsStore::SavePresets({custom}, path) && Read(path) == original, "Reject nonfinite tint");
        custom.effects.tintRed = 2; custom.effects.tintIntensity = -1;
        Check(SettingsStore::SavePresets({custom}, path), "Bound tint on save");
        loaded = SettingsStore::LoadPresets(path);
        Check(loaded.presets[0].effects.tintRed == 1 && loaded.presets[0].effects.tintIntensity == 0, "Saved tint bounds");
        const std::vector<std::string> invalidDocuments{
            "", "{}", "[]", "{\"version\":1,\"presets\":{}}", "{\"version\":1,\"presets\":[true]}",
            "{\"version\":1,\"presets\":[{\"name\":\"CRT\"}]}",
            "{\"version\":1,\"presets\":[{\"name\":1,\"effects\":{}}]}",
            "{\"version\":1,\"presets\":[{\"name\":\" \u0020\",\"effects\":{}}]}",
            "{\"version\":1,\"presets\":[{\"name\":\"CRT\",\"effects\":{\"tintRed\":\"red\"}}]}",
            "{\"version\":1,\"presets\":[{\"name\":\"CRT\",\"effects\":{}},{\"name\":\"crt\",\"effects\":{}}]}",
            "{\"version\":1,\"presets\":[],\"presets\":[]}", "{\"version\":1,\"presets\":[]}trailing",
            std::string(256 * 1024 + 1, ' ')};
        for (const auto& document : invalidDocuments) {
            Write(path, document);
            Check(SettingsStore::LoadPresets(path).status == SettingsLoadStatus::Invalid, "Reject invalid presets JSON");
            Check(!SettingsStore::SavePresets({}, path, &error) && !error.empty() && Read(path) == document,
                  "Preserve invalid presets JSON");
        }
        Write(path, "{\"version\":2,\"presets\":[]}"); original = Read(path);
        Check(SettingsStore::LoadPresets(path).status == SettingsLoadStatus::UnsupportedVersion &&
              !SettingsStore::SavePresets({}, path) && Read(path) == original, "Preserve future preset format");
        Write(path, "\xEF\xBB\xBF{\"version\":1,\"presets\":[{\"name\":\"Old preset\",\"effects\":{\"gamma\":1.2}}]}");
        loaded = SettingsStore::LoadPresets(path);
        Check(loaded.status == SettingsLoadStatus::Loaded && loaded.presets[0].effects.tintRed == 1 &&
              loaded.presets[0].effects.tintIntensity == 0, "Old effects without tint remain neutral");
        std::vector<Preset> maximum;
        for (std::size_t i = 0; i < kMaximumPresets; ++i) maximum.push_back({L"Preset " + std::to_wstring(i), {}});
        Check(SettingsStore::SavePresets(maximum, path) && SettingsStore::LoadPresets(path).presets.size() == 64, "64 presets roundtrip");
        original = Read(path); maximum.push_back({L"One too many", {}});
        Check(!SettingsStore::SavePresets(maximum, path) && Read(path) == original, "Collection limit preserves file");
        Check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "Read-only fixture");
        const bool saved = SettingsStore::SavePresets({}, path, &error);
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        Check(!saved && !error.empty() && Read(path) == original, "Failed replace preserves presets");
        Check(!SettingsStore::SavePresets({}, {}) && SettingsStore::LoadPresets(fixture.folder).status == SettingsLoadStatus::IoError,
              "Invalid preset storage path");
        Check(std::distance(std::filesystem::directory_iterator(fixture.folder), std::filesystem::directory_iterator{}) == 1,
              "No temporary files leaked");
        std::cout << "PASS: custom preset JSON, Unicode, tint, bounds, corruption/version preservation, atomic save failures\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
