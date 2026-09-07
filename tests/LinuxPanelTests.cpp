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
        brightness->setValue(0.9); panel.Stop(); panel.close(); app.processEvents();
        Check(Read(store.PresetsPath()) == savedPreset && Read(store.SettingsPath()) == savedSettings,
              "Changing, stopping and closing must not persist unsaved edits");
        Panel reopened("kde");
        Check(std::abs(reopened.findChild<QDoubleSpinBox*>("brightness")->value() - 0.35) < 0.001,
              "Next launch loads explicitly saved settings");
        Check(!reopened.findChild<QCheckBox*>("enabled")->isChecked(), "Saved settings must not enable the filter at launch");
        std::cout << "PASS: real Qt controls, explicit settings/preset saves, apply, stop, close and reopen\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
