#include "linux/CompositorBackend.h"
#include "linux/ShaderSource.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <iostream>
#include <stdexcept>

using namespace screenfx;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Near(QColor a, QColor b) { return std::abs(a.red() - b.red()) < 4 && std::abs(a.green() - b.green()) < 4 && std::abs(a.blue() - b.blue()) < 4; }
template<class F> bool Until(F test, int timeout = 8000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) { QCoreApplication::processEvents(); if (test()) return true; QThread::msleep(20); }
    return false;
}
class Fixture : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(0, 0, width()/2, height()/2, QColor(200, 50, 30));
        p.fillRect(width()/2, 0, width(), height()/2, QColor(30, 180, 70));
        p.fillRect(0, height()/2, width()/2, height(), QColor(30, 70, 200));
        p.fillRect(width()/2, height()/2, width(), height(), QColor(150, 150, 150));
    }
};
struct Child {
    QProcess process;
    ~Child() { if (process.state() != QProcess::NotRunning) { process.terminate(); if (!process.waitForFinished(2000)) { process.kill(); process.waitForFinished(1000); } } }
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().contains("--fixture")) {
        Fixture fixture; fixture.setWindowTitle("ScreenFX pixel fixture"); fixture.showFullScreen();
        QTimer repaint; QObject::connect(&repaint, &QTimer::timeout, &fixture, QOverload<>::of(&QWidget::update)); repaint.start(30);
        QTimer::singleShot(30000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    try {
        Check(app.arguments().size() == 2, "Usage: LinuxDesktopTests build-directory");
        QTemporaryDir fixture; Check(fixture.isValid(), "Private desktop fixture folder");
        const auto build = app.arguments()[1];
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "xcb"); env.remove("WAYLAND_DISPLAY");
        env.insert("QT_PLUGIN_PATH", build);
        env.insert("XDG_CONFIG_HOME", fixture.path()); env.insert("KWIN_COMPOSE", "O");
        env.insert("LIBGL_ALWAYS_SOFTWARE", "1");
        Child compositor;
        compositor.process.setProcessEnvironment(env);
        compositor.process.setStandardErrorFile(fixture.filePath("kwin.log"));
        compositor.process.start("kwin_wayland", {"--x11-display", qEnvironmentVariable("DISPLAY"), "--width", "640", "--height", "480",
            "--socket", "screenfx-test", "--no-lockscreen", "--no-global-shortcuts", "--no-kactivities"});
        Check(compositor.process.waitForStarted(), "Launch nested KWin");
        auto bus = QDBusConnection::sessionBus();
        if (!Until([&] { return bus.interface()->isServiceRegistered("org.kde.KWin").value(); })) {
            QFile log(fixture.filePath("kwin.log")); if (log.open(QIODevice::ReadOnly)) std::cerr << log.readAll().constData();
            throw std::runtime_error("Nested KWin did not start");
        }
        QDBusInterface diagnostic("org.kde.KWin", "/KWin", "org.kde.KWin");
        const QDBusReply<QString> support = diagnostic.call("supportInformation");
        Check(support.isValid(), "Read nested compositor capabilities");
        if (!support.value().contains("Compositing Type: OpenGL")) {
            std::cout << "SKIP: nested KWin has no OpenGL compositor. A usable DRM render device is required; "
                         "QPainter cannot run ScreenFX. Shader pixel tests run separately.\n";
            return 77;
        }
        env.insert("QT_QPA_PLATFORM", "wayland"); env.insert("WAYLAND_DISPLAY", "screenfx-test");
        Child client; client.process.setProcessEnvironment(env);
        client.process.setStandardErrorFile(fixture.filePath("client.log"));
        client.process.start(app.applicationFilePath(), {"--fixture"}); Check(client.process.waitForStarted(), "Launch Wayland fixture");
        auto grab = [&] { return app.primaryScreen()->grabWindow(0).toImage(); };
        const QPoint points[]{QPoint(80, 80), QPoint(480, 80), QPoint(80, 360), QPoint(480, 360)};
        const QColor colors[]{QColor(200,50,30), QColor(30,180,70), QColor(30,70,200), QColor(150,150,150)};
        auto matches = [&](bool tinted) {
            const auto frame = grab(); if (frame.width() < 640 || frame.height() < 480) return false;
            for (int i = 0; i < 4; ++i) {
                const QColor expected = tinted ? QColor(0, colors[i].green(), 0) : colors[i];
                if (!Near(frame.pixelColor(points[i]), expected)) return false;
            }
            return true;
        };
        if (!Until([&] { return matches(false); })) {
            std::cerr << support.value().toStdString() << '\n';
            for (const auto& name : {"kwin.log", "client.log"}) {
                QFile log(fixture.filePath(name)); if (log.open(QIODevice::ReadOnly)) std::cerr << name << ":\n" << log.readAll().constData();
            }
            grab().save(build + "/desktop-fixture.png");
            const auto frame = grab(); for (const auto& point : points) std::cerr << frame.pixelColor(point).name().toStdString() << ' ';
            throw std::runtime_error("Wayland fixture not visible in nested desktop");
        }
        Check(bus.registerService("org.screenfx.ScreenFX"), "Register isolated controller service");
        linuxfx::CompositorBackend backend("kde"); QString error; core::EffectSettings effects;
        Check(backend.Apply(effects, error), qPrintable(error));
        Check(Until([&] { return matches(false); }), "Identity changes screen pixels/orientation");
        effects.tintRed = 0; effects.tintBlue = 0; effects.tintIntensity = 1;
        Check(backend.Apply(effects, error), qPrintable(error));
        Check(Until([&] { return matches(true); }), "KWin full-screen shader did not tint composited output");
        for (int i = 0; i < 12; ++i) { QThread::msleep(30); app.processEvents(); Check(matches(true), "Filter feedback or stale screen damage"); }
        Check(backend.Stop(error), qPrintable(error));
        Check(Until([&] { return matches(false); }), "Stop did not restore unfiltered desktop");
        Check(backend.Apply(effects, error), qPrintable(error));
        Check(Until([&] { return matches(true); }), "Restart failed");
        bus.unregisterService("org.screenfx.ScreenFX");
        Check(Until([&] { return matches(false); }), "Controller loss did not stop KWin filtering");
        std::cout << "PASS: nested KWin plugin load, real Wayland output pixels/orientation, tint, repeated repaint, stop/restart and controller-loss recovery\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
