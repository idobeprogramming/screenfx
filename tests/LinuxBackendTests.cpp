#include "linux/CompositorBackend.h"
#include <QCoreApplication>
#include <QDir>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QProcess>
#include <QThread>
#include <iostream>
#include <stdexcept>

using namespace screenfx;
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
QJsonObject Read(const QString& path) { QFile f(path); Check(f.open(QIODevice::ReadOnly), "Open JSON fixture"); return QJsonDocument::fromJson(f.readAll()).object(); }
void Write(const QString& path, const QJsonObject& value) { QFile f(path); Check(f.open(QIODevice::WriteOnly), "Write JSON fixture"); f.write(QJsonDocument(value).toJson()); }
int FakeHyprctl(const QStringList& args) {
    const QString path = qEnvironmentVariable("SCREENFX_FIXTURE_STATE");
    auto state = Read(path);
    if (args.size() == 3 && args[1] == "-j" && args[2] == "status") {
        if (!state.contains("provider")) { std::cout << "unknown request"; return 1; }
        std::cout << QJsonDocument(QJsonObject{{"configProvider", state.value("provider")}}).toJson().constData(); return 0;
    }
    if (args.size() == 4 && args[1] == "-j" && args[2] == "getoption") {
        QJsonObject option;
        if (args[3] == "decoration:screen_shader") option.insert("str", state.value("shader"));
        else if (args[3] == "debug:damage_tracking") option.insert("int", state.value("damage"));
        std::cout << QJsonDocument(option).toJson().constData(); return 0;
    }
    if (args.size() == 4 && args[1] == "keyword") {
        if (state.value("provider").toString() == "lua") { std::cout << "unknown request"; return 1; }
        if (args[2] == "decoration:screen_shader") {
            QFile source(args[3]);
            if (source.open(QIODevice::ReadOnly) && source.readAll().contains("uniform float time;") && state.value("damage").toInt() != 0) {
                std::cout << "time uniform requires damage tracking off"; return 1;
            }
            state.insert("shader", args[3]);
        }
        else if (args[2] == "debug:damage_tracking") {
            if (state.value("failDamage").toBool()) {
                state.insert("failDamage", false); Write(path, state); std::cout << "fixture rejection"; return 1;
            }
            state.insert("damage", args[3].toInt());
        }
        Write(path, state); std::cout << "ok\n"; return 0;
    }
    if (args.size() == 3 && args[1] == "eval" && state.value("provider").toString() == "lua") {
        const QString code = args[2];
        const QString prefix = "hl.config({ decoration = { screen_shader = \"", suffix = "\" } })";
        if (code.startsWith(prefix) && code.endsWith(suffix)) {
            const auto literal = code.mid(prefix.size(), code.size() - prefix.size() - suffix.size());
            Check(literal.size() % 4 == 0, "Lua literal must have complete byte escapes");
            QByteArray decoded;
            for (qsizetype i = 0; i < literal.size(); i += 4) {
                Check(literal[i] == '\\', "Unsafe raw data in Lua literal");
                bool valid{}; const auto byte = literal.mid(i + 1, 3).toUInt(&valid);
                Check(valid && byte <= 255, "Invalid escaped UTF-8 byte"); decoded += static_cast<char>(byte);
            }
            state.insert("shader", QString::fromUtf8(decoded));
        } else if (code.startsWith("hl.config({ debug = { damage_tracking = ") && code.endsWith(" } })")) {
            bool valid{}; const int value = code.section(" = ", -1).section(' ', 0, 0).toInt(&valid);
            Check(valid && value >= 0 && value <= 2, "Lua damage setting must be a bounded integer");
            if (state.value("failDamage").toBool()) {
                state.insert("failDamage", false); Write(path, state); std::cout << "fixture rejection"; return 1;
            }
            state.insert("damage", value);
        } else throw std::runtime_error("Unexpected Lua expression");
        Write(path, state); std::cout << "ok\n"; return 0;
    }
    return 1;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        if (QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == "hyprctl") return FakeHyprctl(app.arguments());
        if (app.arguments().contains("--owner")) {
            auto bus = QDBusConnection::sessionBus();
            Check(bus.registerService("org.screenfx.ScreenFX"), "Register fixture controller");
            linuxfx::CompositorBackend controller("hyprland"); QString error;
            Check(controller.Apply({}, error), qPrintable(error));
            std::cout << bus.baseService().toStdString() << std::endl;
            return app.exec();
        }
        QTemporaryDir fixture; Check(fixture.isValid(), "Fixture folder");
        const QString binary = fixture.filePath("hyprctl"), runtime = fixture.filePath("runtime"), state = fixture.filePath("state.json");
        Check(QFile::link(app.applicationFilePath(), binary), "Fixture hyprctl executable link");
        Check(QDir().mkpath(runtime), "Private runtime folder");
        QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("XDG_RUNTIME_DIR", runtime.toUtf8()); qputenv("HYPRLAND_INSTANCE_SIGNATURE", "screenfx-test-instance");
        qputenv("SCREENFX_FIXTURE_STATE", state.toUtf8()); qputenv("PATH", fixture.path().toUtf8() + ':' + qgetenv("PATH"));
        const QString previous = "/tmp/a filter ; $literal.frag";
        Write(state, {{"shader", previous}, {"damage", 2}});
        linuxfx::CompositorBackend backend("hyprland"); QString error; core::EffectSettings effects; effects.scanlineIntensity = 0.5F; effects.grainIntensity = 0.2F;
        Check(backend.Apply(effects, error), qPrintable(error));
        auto current = Read(state);
        Check(current.value("damage").toInt() == 0 && backend.Active(), "Active shader gets full repaint");
        const QString first = current.value("shader").toString();
        Check(QFile::exists(first), "Runtime shader exists");
        effects.tintIntensity = 1;
        Check(backend.Apply(effects, error), qPrintable(error));
        Check(Read(state).value("shader").toString() != first, "Changed effects force shader reload");
        Check(backend.Stop(error), qPrintable(error));
        Check(Read(state).value("shader").toString() == previous && Read(state).value("damage").toInt() == 2,
            "Stop restores previous shader and damage exactly");
        Check(!backend.Active(), "Stop clears active state");
        Check(backend.Apply(effects, error), qPrintable(error));
        current = Read(state); current.insert("failDamage", true); Write(state, current);
        Check(!backend.Stop(error), "A partially failed restoration must be reported");
        Check(backend.Stop(error), qPrintable(error));
        Check(Read(state).value("damage").toInt() == 2 && Read(state).value("shader").toString() == previous,
            "Retry after failed restoration must restore both settings");
        Write(state, {{"shader", previous}, {"damage", 2}, {"failDamage", true}});
        Check(!backend.Apply(effects, error), "Rejected compositor option must fail");
        Check(Read(state).value("shader").toString() == previous && !backend.Active(), "Failed enable rolls back shader");
        Check(backend.Apply(effects, error), qPrintable(error));
        const auto ownedShader = Read(state).value("shader");
        Check(linuxfx::CompositorBackend::RecoverHyprland(error, ":unrelated-controller"), qPrintable(error));
        Check(Read(state).value("shader") == ownedShader, "Old guard must not restore a new controller's state");
        Check(linuxfx::CompositorBackend::RecoverHyprland(error), qPrintable(error));
        Check(Read(state).value("shader").toString() == previous, "Recovery restores interrupted session");
        Check(backend.Stop(error), qPrintable(error));
        Check(backend.Apply(effects, error), qPrintable(error));
        current = Read(state); current.insert("shader", "/tmp/user-selected.frag"); Write(state, current);
        Check(backend.Stop(error), qPrintable(error));
        Check(Read(state).value("shader").toString() == "/tmp/user-selected.frag", "Do not overwrite a newer user shader");
        const QString unusual = QString::fromUtf8("/tmp/écran \"; os.exit(); --\\9\n.frag");
        Write(state, {{"shader", unusual}, {"damage", 2}, {"provider", "lua"}});
        Check(backend.Apply(effects, error), qPrintable(error));
        Check(Read(state).value("damage").toInt() == 0 && QFile::exists(Read(state).value("shader").toString()), "Lua provider applies screen shader");
        current = Read(state); current.insert("failDamage", true); Write(state, current);
        Check(!backend.Stop(error), "Lua restore failure must be reported");
        Check(backend.Stop(error), qPrintable(error));
        Check(Read(state).value("shader").toString() == unusual && Read(state).value("damage").toInt() == 2,
            "Lua restoration preserves path bytes and retries damage restoration");
        if (argc == 2) {
            Write(state, {{"shader", previous}, {"damage", 2}});
            QProcess owner; owner.start(app.applicationFilePath(), {"--owner"});
            Check(owner.waitForStarted() && owner.waitForReadyRead(5000), "Start crash-recovery controller fixture");
            const auto uniqueOwner = QString::fromUtf8(owner.readLine()).trimmed();
            Check(uniqueOwner.startsWith(':'), "Fixture controller bus identity");
            QProcess guard; guard.start(QString::fromLocal8Bit(argv[1]), {"--guard", uniqueOwner});
            Check(guard.waitForStarted(), "Start recovery helper");
            owner.kill(); owner.waitForFinished(2000);
            Check(guard.waitForFinished(5000) && guard.exitCode() == 0, "Recovery helper did not finish after controller crash");
            Check(Read(state).value("shader").toString() == previous && Read(state).value("damage").toInt() == 2,
                "Crash recovery must restore previous compositor settings");
        }
        std::cout << "PASS: legacy/Lua Hyprland IPC, safe path literals, shader reload, stop/rollback/retry/crash recovery and newer shader preservation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
