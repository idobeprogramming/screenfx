#define main ScreenFxApplicationMain
#include "linux/main.cpp"
#undef main
#include <QTemporaryDir>
#include <QDir>
#include <stdexcept>

void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
QByteArray Read(const QString& path) { QFile file(path); Check(file.open(QIODevice::ReadOnly), "Read saved fixture"); return file.readAll(); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        Check(directory.isValid(), "Create isolated settings directory");
        qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
        Panel panel("kde"); panel.show(); app.processEvents();
        linuxfx::JsonStore store;
        auto* brightness = panel.findChild<QDoubleSpinBox*>("brightness");
        auto* preset = panel.findChild<QComboBox*>("presetName");
        auto* enabled = panel.findChild<QCheckBox*>("enabled");
        Check(brightness && preset && enabled, "Panel exposes effect and preset controls");
        Check(!enabled->isChecked(), "Startup must keep the filter disabled");
        brightness->setValue(0.35); preset->setEditText("My green CRT"); app.processEvents();
        Check(!QFile::exists(store.SettingsPath()) && !QFile::exists(store.PresetsPath()), "Editing must not save settings or presets");
        panel.findChild<QPushButton*>("savePreset")->click();
        const auto savedPreset = Read(store.PresetsPath());
        Check(!QFile::exists(store.SettingsPath()), "Save preset must not save application settings");
        brightness->setValue(-0.5);
        panel.findChild<QPushButton*>("applyPreset")->click();
        Check(std::abs(brightness->value() - 0.35) < 0.001, "Apply preset restores effects");
        Check(Read(store.PresetsPath()) == savedPreset, "Applying a preset must not rewrite it");
        panel.findChild<QPushButton*>("saveSettings")->click();
        const auto savedSettings = Read(store.SettingsPath());
        auto* deletePreset = panel.findChild<QPushButton*>("deletePreset");
        auto* status = panel.findChild<QLabel*>("status");
        Check(deletePreset && status, "Panel exposes the Delete preset button and feedback");
        brightness->setValue(0.9);
        preset->setEditText("Keep this preset"); panel.findChild<QPushButton*>("savePreset")->click();
        preset->setEditText("MY GREEN CRT");
        deletePreset->click();
        QMap<QString, core::EffectSettings> remaining; QString error;
        Check(store.LoadPresets(remaining, error) && remaining.size() == 1 && remaining.contains("Keep this preset"),
              "Delete button removes the chosen preset and preserves other presets on disk");
        Check(preset->count() == 1 && preset->currentIndex() == -1 && preset->currentText().isEmpty(),
              "Delete refreshes the preset choices and clears the selection");
        Check(status->text() == "Preset deleted." && std::abs(brightness->value() - 0.9) < 0.001 &&
              !enabled->isChecked() && Read(store.SettingsPath()) == savedSettings,
              "Delete reports success without changing effects, filter state or saved settings");
        const auto afterDelete = Read(store.PresetsPath());
        preset->setEditText("Missing"); deletePreset->click();
        Check(!status->text().isEmpty() && status->text() != "Preset deleted." && preset->currentText() == "Missing" &&
              Read(store.PresetsPath()) == afterDelete && std::abs(brightness->value() - 0.9) < 0.001,
              "Failed deletion displays an error and preserves the selection, library and effects");
        preset->setEditText("Keep this preset"); deletePreset->click();
        Check(store.LoadPresets(remaining, error) && remaining.isEmpty() && preset->count() == 0 && preset->currentText().isEmpty(),
              "Delete the last preset through the actual button");
        const auto emptyLibrary = Read(store.PresetsPath());
        panel.Stop(); panel.close(); app.processEvents();
        Check(Read(store.PresetsPath()) == emptyLibrary && Read(store.SettingsPath()) == savedSettings,
              "Changing, stopping and closing must not persist unsaved edits");
        Panel reopened("kde");
        Check(std::abs(reopened.findChild<QDoubleSpinBox*>("brightness")->value() - 0.35) < 0.001,
              "Next launch loads explicitly saved settings");
        Check(!reopened.findChild<QCheckBox*>("enabled")->isChecked(), "Saved settings must not enable the filter at launch");
        Check(reopened.findChild<QComboBox*>("presetName")->count() == 0, "Deleted presets stay deleted after reopening the panel");
        std::cout << "PASS: real Qt controls, explicit settings/preset saves and deletion, apply, stop, close and reopen\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
