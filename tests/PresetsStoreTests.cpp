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
void TestDeletePreset() {
    Fixture fixture;
    const auto path = fixture.folder / L"presets.json";
    const auto settingsPath = fixture.folder / L"settings.json";
    std::vector<Preset> remaining{{L"Unchanged UI library", {}}};
    std::wstring error;
    const auto unchangedOutput = [&] { return remaining.size() == 1 && remaining.front().name == L"Unchanged UI library"; };
    Check(!SettingsStore::DeletePreset(L"CRT", path, remaining, &error) && !error.empty() &&
          !std::filesystem::exists(path) && unchangedOutput(), "Missing library deletion preserves disk and UI");
    AppSettings settings;
    settings.effects.tintIntensity = 0.75F;
    Check(SettingsStore::Save(settings, settingsPath), "Write independent active settings fixture");
    const auto settingsDocument = Read(settingsPath);
    const std::string first = R"({"name":"CRT","effects":{}})";
    const std::string spaced = R"({"name":"  CRT  ","effects":{},"metadata":[null,false,true,"escaped\\value\""]})";
    const std::string other = R"({"name":"External preset","effects":{"gamma":1.31234,"tintBlue":0.25,"futureField":1234567890123456789},"note":"keep me"})";
    const std::string prefix = R"({"version":1,"rootMetadata":{"keep":true,"precise":1234567890123456789},"presets":[)";
    const std::string suffix = R"(],"tail":[{},[],null]})";
    const auto original = prefix + first + ",\n  " + spaced + ",\n  " + other + suffix;
    Write(path, original);
    for (const auto& name : {L"", L"   ", L"bad\nname", L"Unknown", L"CR"}) {
        Check(!SettingsStore::DeletePreset(name, path, remaining, &error) && !error.empty() &&
              Read(path) == original && unchangedOutput(), "Invalid and unmatched names preserve file and UI");
    }
    Check(SettingsStore::DeletePreset(L"cRt", path, remaining, &error) && error.empty() && remaining.size() == 2 &&
          remaining[0].name == L"  CRT  " && remaining[1].name == L"External preset" &&
          remaining[1].effects.gamma == 1.31234F && remaining[1].effects.tintBlue == 0.25F,
          "Delete case-insensitive exact name using current disk library");
    Check(Read(path) == prefix + spaced + ",\n  " + other + suffix,
          "Deleting first entry preserves all unrelated JSON metadata, precision and formatting");
    Check(SettingsStore::LoadPresets(path).presets.size() == 2, "Deleted preset stays deleted after reload");
    Check(SettingsStore::DeletePreset(L"  crt  ", path, remaining, &error) && remaining.size() == 1,
          "Stored names with leading and trailing spaces can be deleted exactly");
    Check(SettingsStore::DeletePreset(L"External preset", path, remaining, &error) && remaining.empty() &&
          SettingsStore::LoadPresets(path).status == SettingsLoadStatus::Loaded &&
          Read(path) == prefix + suffix, "Last deletion leaves a valid empty library and preserves root metadata");
    remaining = {{L"Unchanged UI library", {}}};
    const auto emptyLibrary = Read(path);
    Check(!SettingsStore::DeletePreset(L"CRT", path, remaining, &error) && !error.empty() &&
          Read(path) == emptyLibrary && unchangedOutput(), "Repeated deletion preserves empty library");
    Write(path, original);
    Check(SettingsStore::DeletePreset(L"  CRT  ", path, remaining, &error) &&
          Read(path) == prefix + first + ",\n  " + other + suffix, "Middle deletion removes exactly its entry and comma");
    Check(SettingsStore::DeletePreset(L"External preset", path, remaining, &error) &&
          Read(path) == prefix + first + suffix, "Last array entry deletion removes its preceding comma");
    remaining = {{L"Unchanged UI library", {}}};
    for (const auto& document : {std::string{}, std::string{"broken json"}, std::string{"{\"version\":2,\"presets\":[]}"}}) {
        Write(path, document);
        Check(!SettingsStore::DeletePreset(L"CRT", path, remaining, &error) && !error.empty() &&
              Read(path) == document && unchangedOutput(), "Deletion preserves blank, corrupt and future-format files");
    }
    Write(path, original);
    Check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "Read-only deletion fixture");
    const bool deleted = SettingsStore::DeletePreset(L"CRT", path, remaining, &error);
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    Check(!deleted && !error.empty() && Read(path) == original && unchangedOutput(), "Failed atomic deletion preserves file and UI");
    Check(!SettingsStore::DeletePreset(L"CRT", fixture.folder, remaining, &error) && !error.empty() && unchangedOutput(),
          "Deletion rejects invalid storage paths");
    Check(Read(settingsPath) == settingsDocument, "Deleting presets does not save or modify active settings");
    Check(std::distance(std::filesystem::directory_iterator(fixture.folder), std::filesystem::directory_iterator{}) == 2,
          "Deletion does not leak temporary files");
}
}
int main() {
    try {
        TestDeletePreset();
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
        std::cout << "PASS: custom preset JSON, explicit deletion, metadata preservation, Unicode, tint, bounds, atomic save/delete failures\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
