#include "CompositorBackend.h"
#include "LinuxSettings.h"
#include "ShaderSource.h"
#include "core/EffectFields.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSocketNotifier>
#include <QTimer>
#include <QVBoxLayout>
#include <iostream>
#include <vector>
#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

using namespace screenfx;
namespace {
int signalPipe = -1;
void TerminationSignal(int) {
    const int previousError = errno;
    if (signalPipe >= 0) { const char byte = 1; const auto result = ::write(signalPipe, &byte, 1); (void)result; }
    errno = previousError;
}
}
class Panel : public QWidget {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.screenfx.Controller")
public:
    explicit Panel(QString backend) : backend_(std::move(backend)) {
        setWindowTitle("ScreenFX"); resize(720, 850);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("ScreenFX — " + backend_.Name()));
        auto* description = new QLabel("Full-screen effects, rendered by your desktop compositor.\nDisplay synchronization is managed by KDE or Hyprland.");
        description->setWordWrap(true); layout->addWidget(description);
        auto* top = new QHBoxLayout;
        enabled_ = new QCheckBox("Enable filter"); enabled_->setObjectName("enabled"); top->addWidget(enabled_);
        auto* stop = new QPushButton("Stop filter"); top->addWidget(stop);
        auto* reset = new QPushButton("Reset effects"); top->addWidget(reset);
        auto* save = new QPushButton("Save settings"); save->setObjectName("saveSettings"); top->addWidget(save); layout->addLayout(top);
        auto* presets = new QHBoxLayout;
        presetName_ = new QComboBox; presetName_->setEditable(true); presetName_->setObjectName("presetName");
        presetName_->setPlaceholderText("Custom preset name"); presets->addWidget(presetName_, 1);
        auto* applyPreset = new QPushButton("Apply preset"); applyPreset->setObjectName("applyPreset"); presets->addWidget(applyPreset);
        auto* savePreset = new QPushButton("Save preset"); savePreset->setObjectName("savePreset"); presets->addWidget(savePreset); layout->addLayout(presets);
        tint_ = new QPushButton("Tint color"); tint_->setObjectName("tintColor"); layout->addWidget(tint_);
        auto* scroll = new QScrollArea; scroll->setWidgetResizable(true);
        auto* formWidget = new QWidget; auto* form = new QFormLayout(formWidget);
        for (const auto& field : core::kEffectFields) {
            if (field.member == &core::EffectSettings::tintRed || field.member == &core::EffectSettings::tintGreen || field.member == &core::EffectSettings::tintBlue) continue;
            auto* row = new QWidget; auto* rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 0);
            auto* slider = new QSlider(Qt::Horizontal); slider->setRange(0, 1000); slider->setMinimumWidth(180);
            auto* spin = new QDoubleSpinBox; spin->setObjectName(field.name); spin->setRange(field.minimum, field.maximum);
            spin->setDecimals(3); spin->setSingleStep((field.maximum - field.minimum) / 100); rowLayout->addWidget(slider, 1); rowLayout->addWidget(spin);
            form->addRow(QString::fromUtf8(field.label), row); controls_.push_back({field, slider, spin});
            connect(slider, &QSlider::valueChanged, this, [this, field, spin](int value) {
                spin->setValue(field.minimum + (field.maximum - field.minimum) * value / 1000.0);
            });
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, field, slider](double value) {
                effects_.*field.member = static_cast<float>(value);
                QSignalBlocker blocker(slider); slider->setValue(qRound(1000 * (value - field.minimum) / (field.maximum - field.minimum)));
                if (enabled_->isChecked()) pending_.start();
            });
        }
        scroll->setWidget(formWidget); layout->addWidget(scroll, 1);
        status_ = new QLabel("Filter stopped."); status_->setObjectName("status"); status_->setWordWrap(true); status_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status_);
        pending_.setSingleShot(true); pending_.setInterval(120);
        connect(&pending_, &QTimer::timeout, this, &Panel::Apply);
        connect(enabled_, &QCheckBox::toggled, this, [this](bool on) { if (on) Apply(); else Stop(); });
        connect(stop, &QPushButton::clicked, this, &Panel::Stop);
        connect(&backend_, &linuxfx::CompositorBackend::stopped, this, [this] {
            pending_.stop(); QSignalBlocker blocker(enabled_); enabled_->setChecked(false); status_->setText("Filter stopped.");
        });
        connect(reset, &QPushButton::clicked, this, [this] { effects_ = {}; Sync(); if (enabled_->isChecked()) Apply(); });
        connect(save, &QPushButton::clicked, this, [this] {
            QString error; status_->setText(store_.SaveEffects(effects_, error) ? "Settings saved." : error);
        });
        connect(savePreset, &QPushButton::clicked, this, [this] {
            QString error; const QString name = presetName_->currentText();
            if (!store_.SavePreset(name, effects_, error)) { status_->setText(error); return; }
            RefreshPresets(); presetName_->setEditText(name); status_->setText("Preset saved.");
        });
        connect(applyPreset, &QPushButton::clicked, this, [this] { SelectPreset(presetName_->currentText()); });
        connect(tint_, &QPushButton::clicked, this, [this] {
            const auto color = QColorDialog::getColor(QColor::fromRgbF(effects_.tintRed, effects_.tintGreen, effects_.tintBlue), this, "Tint color");
            if (!color.isValid()) return;
            effects_.tintRed = color.redF(); effects_.tintGreen = color.greenF(); effects_.tintBlue = color.blueF();
            if (effects_.tintIntensity == 0) effects_.tintIntensity = 1;
            Sync(); if (enabled_->isChecked()) Apply();
        });
        QString error; if (!store_.LoadEffects(effects_, error)) status_->setText(error);
        RefreshPresets(); Sync();
    }
    bool SelectPreset(const QString& name) {
        for (auto it = presets_.cbegin(); it != presets_.cend(); ++it) if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
            effects_ = it.value(); Sync(); if (enabled_->isChecked()) Apply(); return true;
        }
        status_->setText("Choose a saved preset first."); return false;
    }
    void Enable() { enabled_->setChecked(true); }
public Q_SLOTS:
    void Show() { show(); raise(); activateWindow(); }
    bool Stop() {
        pending_.stop(); QString error;
        const bool stopped = backend_.Stop(error);
        QSignalBlocker blocker(enabled_); enabled_->setChecked(!stopped && backend_.Active());
        status_->setText(stopped ? "Filter stopped." : error); return stopped;
    }
protected:
    void closeEvent(QCloseEvent* event) override { if (Stop()) event->accept(); else event->ignore(); }
private:
    void Apply() {
        pending_.stop(); if (!enabled_->isChecked()) return;
        if (backend_.Name() == "Hyprland" && !guardStarted_) {
            guardStarted_ = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                {"--guard", QDBusConnection::sessionBus().baseService()});
            if (!guardStarted_) {
                QSignalBlocker blocker(enabled_); enabled_->setChecked(false);
                status_->setText("Could not start the filter recovery helper."); return;
            }
        }
        QString error;
        if (!backend_.Apply(effects_, error)) { QSignalBlocker blocker(enabled_); enabled_->setChecked(backend_.Active()); status_->setText(error); }
        else status_->setText("Filter enabled — " + backend_.Name() + ".");
    }
    void RefreshPresets() {
        QString error; if (!store_.LoadPresets(presets_, error)) { status_->setText(error); return; }
        const QString text = presetName_->currentText(); QSignalBlocker blocker(presetName_);
        presetName_->clear(); presetName_->addItems(presets_.keys()); presetName_->setEditText(text);
    }
    void Sync() {
        for (const auto& control : controls_) {
            QSignalBlocker first(control.slider), second(control.spin);
            const float value = effects_.*control.field.member;
            control.spin->setValue(value); control.slider->setValue(qRound(1000 * (value - control.field.minimum) / (control.field.maximum - control.field.minimum)));
        }
        tint_->setText("Tint color: " + QColor::fromRgbF(effects_.tintRed, effects_.tintGreen, effects_.tintBlue).name().toUpper());
    }
    struct Control { core::EffectField field; QSlider* slider; QDoubleSpinBox* spin; };
    std::vector<Control> controls_;
    linuxfx::JsonStore store_;
    linuxfx::CompositorBackend backend_;
    core::EffectSettings effects_;
    QMap<QString, core::EffectSettings> presets_;
    QCheckBox* enabled_{}; QComboBox* presetName_{}; QPushButton* tint_{}; QLabel* status_{};
    QTimer pending_;
    bool guardStarted_ = false;
};

int main(int argc, char** argv) {
    if (argc == 3 && QByteArray(argv[1]) == "--guard") {
        QCoreApplication app(argc, argv);
        const QString owner = QString::fromLocal8Bit(argv[2]);
        auto bus = QDBusConnection::sessionBus();
        QDBusServiceWatcher watcher(owner, bus, QDBusServiceWatcher::WatchForUnregistration);
        QObject::connect(&watcher, &QDBusServiceWatcher::serviceUnregistered, &app, &QCoreApplication::quit);
        if (bus.interface() && bus.interface()->isServiceRegistered(owner).value()) app.exec();
        QString error;
        return linuxfx::CompositorBackend::RecoverHyprland(error, owner) ? 0 : 1;
    }
    // Stop must also work from a terminal with no GUI connection.
    if (argc == 2 && QByteArray(argv[1]) == "--stop") {
        QCoreApplication app(argc, argv);
        QDBusInterface controller("org.screenfx.ScreenFX", "/ScreenFX", "org.screenfx.Controller");
        controller.setTimeout(4000);
        if (controller.isValid()) {
            const QDBusReply<bool> reply = controller.call("Stop");
            return reply.isValid() && reply.value() ? 0 : 1;
        }
        QString error;
        if (!linuxfx::CompositorBackend::RecoverHyprland(error)) { std::cerr << error.toStdString() << '\n'; return 1; }
        return 0;
    }
    QApplication app(argc, argv);
    int descriptors[2]{};
    if (::pipe2(descriptors, O_NONBLOCK | O_CLOEXEC) != 0) return 1;
    signalPipe = descriptors[1];
    QSocketNotifier termination(descriptors[0], QSocketNotifier::Read);
    QObject::connect(&termination, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    std::signal(SIGINT, TerminationSignal); std::signal(SIGTERM, TerminationSignal);
    QCoreApplication::setApplicationName("ScreenFX");
    QCoreApplication::setApplicationVersion(SCREENFX_VERSION);
    QCommandLineParser parser; parser.setApplicationDescription("Full-screen CRT effects for KDE Plasma and Hyprland");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"stop", "Stop the active filter (use this option on its own)."});
    parser.addOption({"backend", "Use kde or hyprland (normally detected automatically).", "backend"});
    parser.addOption({"enable", "Enable the filter when the panel opens."});
    parser.addOption({"preset", "Apply an existing custom preset.", "name"});
    parser.process(app);
    if (parser.isSet("backend") && parser.value("backend") != "kde" && parser.value("backend") != "hyprland") parser.showHelp(1);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { std::cerr << "A desktop D-Bus session is required.\n"; return 1; }
    if (!bus.registerService("org.screenfx.ScreenFX")) {
        QDBusInterface existing("org.screenfx.ScreenFX", "/ScreenFX", "org.screenfx.Controller"); existing.call("Show"); return 0;
    }
    Panel panel(parser.value("backend"));
    bus.registerObject("/ScreenFX", &panel, QDBusConnection::ExportAllSlots);
    if (parser.isSet("preset") && !panel.SelectPreset(parser.value("preset"))) return 1;
    panel.show();
    if (parser.isSet("enable")) QTimer::singleShot(0, &panel, &Panel::Enable);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &panel, &Panel::Stop);
    return app.exec();
}
#include "main.moc"
