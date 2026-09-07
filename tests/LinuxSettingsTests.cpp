#include "linux/LinuxSettings.h"
#include "core/EffectFields.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace screenfx;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
QByteArray Read(const QString& path) { QFile file(path); Check(file.open(QIODevice::ReadOnly), "Open fixture"); return file.readAll(); }
void Write(const QString& path, QByteArray bytes) { QFile file(path); Check(file.open(QIODevice::WriteOnly), "Create fixture"); Check(file.write(bytes) == bytes.size(), "Write fixture"); }
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir fixture; Check(fixture.isValid(), "Temporary folder");
        linuxfx::JsonStore store(fixture.path()); QString error;
        core::EffectSettings effects; QMap<QString, core::EffectSettings> presets;
        Check(store.LoadEffects(effects, error) && store.LoadPresets(presets, error), "Missing files use defaults");
        Check(!QFile::exists(store.SettingsPath()) && !QFile::exists(store.PresetsPath()), "Reading never saves files");
        effects.tintRed = 0.1F; effects.tintGreen = 0.4F; effects.tintBlue = 0.8F; effects.tintIntensity = 0.75F;
        effects.scanlineIntensity = 0.9F;
        Check(store.SaveEffects(effects, error), "Explicit settings save"); core::EffectSettings loaded;
        Check(store.LoadEffects(loaded, error), "Load saved settings");
        for (const auto& field : core::kEffectFields) Check(loaded.*field.member == effects.*field.member, "Effect roundtrip");
        Check(store.SavePreset(QString::fromUtf8("Écran \"vert\""), effects, error), "Unicode preset save");
        Check(store.SavePreset(QString::fromUtf8("écran \"VERT\""), {}, error), "Case-insensitive replacement");
        Check(store.LoadPresets(presets, error) && presets.size() == 1 && presets.first().tintIntensity == 0, "Update matching preset");
        const auto before = Read(store.PresetsPath());
        Check(!store.SavePreset("   ", effects, error) && Read(store.PresetsPath()) == before, "Reject blank name without writing");
        Check(!store.SavePreset(QString(81, 'x'), effects, error), "Reject long name");
        effects.gamma = std::numeric_limits<float>::quiet_NaN();
        Check(!store.SavePreset("Invalid", effects, error) && Read(store.PresetsPath()) == before, "Reject nonfinite effects");
        const QByteArray invalidCases[]{"{broken", "{\"version\":2,\"presets\":[]}",
            "{\"version\":1,\"presets\":[{\"name\":\"a\",\"effects\":{}},{\"name\":\"A\",\"effects\":{}}]}",
            "{\"version\":1,\"version\":1,\"presets\":[]}",
            "{\"version\":1,\"presets\":[{\"name\":\"a\",\"effects\":{\"gamma\":\"bad\"}}]}"};
        for (const auto& invalid : invalidCases) {
            Write(store.PresetsPath(), invalid);
            Check(!store.LoadPresets(presets, error), "Reject invalid preset file");
            Check(!store.SavePreset("New", {}, error) && Read(store.PresetsPath()) == invalid, "Preserve invalid/future preset files");
        }
        const QByteArray future = "{\"version\":2,\"effects\":{}}";
        Write(store.SettingsPath(), future);
        Check(!store.SaveEffects({}, error) && Read(store.SettingsPath()) == future, "Preserve future settings file");
        Write(store.PresetsPath(), before);
        for (int i = 1; i < 64; ++i) Check(store.SavePreset(QString("Preset %1").arg(i), {}, error), "Fill preset library");
        const auto full = Read(store.PresetsPath());
        Check(!store.SavePreset("Overflow", {}, error) && Read(store.PresetsPath()) == full, "Enforce 64 preset limit");
        std::cout << "PASS: manual JSON saves, all effects, Unicode, replacement, invalid/future/duplicate protection, preset limits\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
