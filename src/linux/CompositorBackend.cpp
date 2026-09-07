#include "CompositorBackend.h"
#include "ShaderSource.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace screenfx::linuxfx {
namespace {
QString RuntimeDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty()) return {};
    const QString path = QDir(base).filePath("screenfx");
    if (!QDir().mkpath(path)) return {};
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}
bool Write(const QString& path, const QByteArray& bytes, QString& error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Could not write the temporary compositor state: " + file.errorString(); return false;
    }
    return true;
}
bool Hyprctl(const QStringList& arguments, QByteArray& response, QString& error) {
    QProcess process;
    process.start("hyprctl", arguments);
    if (!process.waitForStarted(1500) || !process.waitForFinished(2500)) {
        process.kill(); process.waitForFinished(500);
        error = "Hyprland did not respond. Check that hyprctl is installed and this is a Hyprland session.";
        return false;
    }
    response = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = "Hyprland: " + QString::fromUtf8(response + process.readAllStandardError()).trimmed(); return false;
    }
    return true;
}
bool Option(const QString& name, QJsonObject& object, QString& error) {
    QByteArray response;
    if (!Hyprctl({"-j", "getoption", name}, response, error)) return false;
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(response, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = "Hyprland returned an invalid option response."; return false;
    }
    object = document.object(); return true;
}
QString LuaString(const QString& value) {
    // Fixed-width decimal byte escapes preserve UTF-8 and keep every path byte
    // inside the literal, including quotes, backslashes and line breaks.
    QString result = "\"";
    for (const unsigned char byte : value.toUtf8()) result += QString("\\%1").arg(byte, 3, 10, QLatin1Char('0'));
    return result + '"';
}
bool Keyword(const QString& name, const QString& value, QString& error) {
    QByteArray status; QString ignored;
    // Older releases have no status command. They retain the keyword API.
    // Query again during recovery so a helper also uses the current provider.
    const bool lua = Hyprctl({"-j", "status"}, status, ignored) &&
        QJsonDocument::fromJson(status).object().value("configProvider").toString() == "lua";
    QStringList arguments;
    if (lua) {
        if (name == "decoration:screen_shader")
            arguments = {"eval", "hl.config({ decoration = { screen_shader = " + LuaString(value) + " } })"};
        else if (name == "debug:damage_tracking") {
            bool valid{}; const int damage = value.toInt(&valid);
            if (!valid || damage < 0 || damage > 2) { error = "Invalid damage tracking value."; return false; }
            arguments = {"eval", QString("hl.config({ debug = { damage_tracking = %1 } })").arg(damage)};
        } else { error = "Unsupported compositor setting."; return false; }
    } else arguments = {"keyword", name, value};
    QByteArray response;
    if (!Hyprctl(arguments, response, error)) return false;
    if (response.trimmed() != "ok") {
        error = "Hyprland could not apply " + name + ": " + QString::fromUtf8(response).trimmed(); return false;
    }
    return true;
}
QString NormalizeShader(QString value) { return value == "[[EMPTY]]" || value == "[[Empty]]" ? QString{} : value; }
QString Journal() { const auto path = RuntimeDirectory(); return path.isEmpty() ? QString{} : QDir(path).filePath("hyprland-state.json"); }
bool SaveHyprlandState(QString& error) {
    QJsonObject shader, damage;
    if (!Option("decoration:screen_shader", shader, error) || !Option("debug:damage_tracking", damage, error)) return false;
    if (!shader.value("str").isString() || !damage.value("int").isDouble()) {
        error = "This Hyprland version does not expose screen_shader and damage_tracking."; return false;
    }
    const auto path = Journal();
    if (path.isEmpty()) { error = "No private runtime directory is available."; return false; }
    return Write(path, QJsonDocument(QJsonObject{{"version", 1}, {"instance", qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE")},
        {"controller", QDBusConnection::sessionBus().baseService()},
        {"shader", NormalizeShader(shader.value("str").toString())}, {"damage", damage.value("int")}}).toJson(), error);
}
}

CompositorBackend::CompositorBackend(QString requested, QObject* parent) : QObject(parent) {
    if (requested == "hyprland" || (requested.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))) kind_ = Kind::Hyprland;
    else if (requested == "kde" || (requested.isEmpty() && QDBusConnection::sessionBus().interface()->isServiceRegistered("org.kde.KWin"))) kind_ = Kind::KWin;
    QDBusConnection::sessionBus().connect("org.kde.KWin", "/ScreenFX", "org.screenfx.Effect", "Stopped", this, SLOT(RemoteStopped()));
    auto* watcher = new QDBusServiceWatcher("org.kde.KWin", QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &CompositorBackend::RemoteStopped);
}
CompositorBackend::~CompositorBackend() { QString ignored; Stop(ignored); }
QString CompositorBackend::Name() const {
    switch (kind_) {
    case Kind::KWin: return "KDE Plasma";
    case Kind::Hyprland: return "Hyprland";
    default: return "No supported compositor detected";
    }
}
void CompositorBackend::RemoteStopped() { active_ = false; Q_EMIT stopped(); }

bool CompositorBackend::RecoverHyprland(QString& error, const QString& controllerOwner) {
    error.clear();
    const auto journalPath = Journal();
    QFile journal(journalPath);
    if (!journal.exists()) return true;
    if (!journal.open(QIODevice::ReadOnly) || journal.size() > 16384) {
        error = "Could not read the previous Hyprland state."; return false;
    }
    auto state = QJsonDocument::fromJson(journal.readAll()).object();
    if (state.value("version").toInt() != 1 || !state.value("shader").isString() || !state.value("damage").isDouble()) {
        error = "The previous Hyprland state is invalid; it has been preserved."; return false;
    }
    if (!controllerOwner.isEmpty() && state.value("controller").toString() != controllerOwner) return true;
    if (state.value("instance").toString() != qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE")) {
        journal.close(); return QFile::remove(journalPath);
    }
    QJsonObject current;
    if (!Option("decoration:screen_shader", current, error)) return false;
    if (!current.value("str").isString()) {
        error = "Hyprland did not return the current shader. Recovery state has been preserved."; return false;
    }
    const QString shader = NormalizeShader(current.value("str").toString());
    const QString directory = RuntimeDirectory();
    // Never undo a different filter that the user applied after ScreenFX.
    const bool owned = shader == QDir(directory).filePath("active-0.frag") || shader == QDir(directory).filePath("active-1.frag");
    const bool finishingRestore = state.value("restoring").toBool() && shader == state.value("shader").toString();
    if (owned) {
        // Persist the phase before restoring the shader. A crash or failed
        // damage command can then resume even though our shader is gone.
        state.insert("restoring", true);
        journal.close();
        if (!Write(journalPath, QJsonDocument(state).toJson(), error) ||
            !Keyword("decoration:screen_shader", state.value("shader").toString(), error)) return false;
    }
    if ((owned || finishingRestore) && !Keyword("debug:damage_tracking", QString::number(state.value("damage").toInt()), error)) return false;
    journal.close();
    if (!QFile::remove(journalPath)) { error = "Could not remove the recovered runtime state."; return false; }
    return true;
}

bool CompositorBackend::Apply(const core::EffectSettings& effects, QString& error) {
    error.clear();
    if (kind_ == Kind::None) { error = "Run ScreenFX inside KDE Plasma or Hyprland."; return false; }
    if (kind_ == Kind::KWin) {
        QDBusInterface manager("org.kde.KWin", "/Effects", "org.kde.kwin.Effects");
        manager.setTimeout(2500);
        const QDBusReply<bool> loaded = manager.call("isEffectLoaded", "screenfx");
        if (!loaded.isValid() || !loaded.value()) {
            const QDBusReply<bool> result = manager.call("loadEffect", "screenfx");
            if (!result.isValid() || !result.value()) {
                error = "Install the ScreenFX KWin effect built for your Plasma version, then try again."; return false;
            }
        }
        QDBusInterface effect("org.kde.KWin", "/ScreenFX", "org.screenfx.Effect");
        effect.setTimeout(2500);
        const QDBusReply<bool> result = effect.call("Apply", BuildShader(effects, ShaderTarget::KWin));
        if (!result.isValid() || !result.value()) {
            error = "KWin could not compile or activate the ScreenFX shader."; return false;
        }
    } else {
        if (!active_ && (!RecoverHyprland(error) || !SaveHyprlandState(error))) return false;
        const QString directory = RuntimeDirectory();
        if (directory.isEmpty()) { error = "No private runtime directory is available."; return false; }
        const auto applyShader = [&](const core::EffectSettings& settings) {
            const QString path = QDir(directory).filePath(QString("active-%1.frag").arg(generation_++ % 2));
            return Write(path, BuildShader(settings, ShaderTarget::Hyprland).toUtf8(), error) && Keyword("decoration:screen_shader", path, error);
        };
        // Establish shader ownership before changing damage policy. A static
        // first pass avoids Hyprland's time-uniform error during activation.
        const bool startingGrain = !active_ && effects.grainIntensity > 0.001F && effects.globalIntensity > 0;
        auto initial = effects;
        if (startingGrain) initial.grainIntensity = 0;
        if (!applyShader(initial)) return false;
        active_ = true;
        if (!Keyword("debug:damage_tracking", "0", error) || (startingGrain && !applyShader(effects))) {
            const QString originalError = error; QString ignored; Stop(ignored); error = originalError; return false;
        }
    }
    active_ = true; return true;
}
bool CompositorBackend::Stop(QString& error) {
    error.clear();
    if (kind_ == Kind::Hyprland) {
        if (!RecoverHyprland(error)) return false;
    } else if (kind_ == Kind::KWin && active_) {
        QDBusInterface effect("org.kde.KWin", "/ScreenFX", "org.screenfx.Effect");
        effect.setTimeout(2500);
        const auto reply = effect.call("Stop");
        if (reply.type() == QDBusMessage::ErrorMessage) { error = reply.errorMessage(); return false; }
    }
    active_ = false; return true;
}
}
