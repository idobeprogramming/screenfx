// SPDX-License-Identifier: GPL-2.0-or-later
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <core/output.h>
#include <core/renderviewport.h>
#include <opengl/glutils.h>
#include <KGlobalAccel>
#include <QAction>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <map>

namespace KWin {
class ScreenFxEffect : public Effect {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.screenfx.Effect")
public:
    ScreenFxEffect() {
        QDBusConnection::sessionBus().registerObject("/ScreenFX", this,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        auto* watcher = new QDBusServiceWatcher("org.screenfx.ScreenFX", QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForUnregistration, this);
        connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &ScreenFxEffect::Stop);
        auto* emergency = new QAction("Stop ScreenFX", this);
        emergency->setObjectName("Stop ScreenFX");
        KGlobalAccel::self()->setDefaultShortcut(emergency, {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F12)});
        KGlobalAccel::self()->setShortcut(emergency, {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F12)});
        connect(emergency, &QAction::triggered, this, &ScreenFxEffect::Stop);
        connect(effects, &EffectsHandler::screenRemoved, this, [this](LogicalOutput* output) {
            effects->makeOpenGLContextCurrent(); buffers_.erase(output);
        });
        clock_.start();
    }
    ~ScreenFxEffect() override {
        effects->makeOpenGLContextCurrent();
        buffers_.clear(); shader_.reset();
        QDBusConnection::sessionBus().unregisterObject("/ScreenFX");
    }
    static bool supported() { return effects->openglContext() && effects->openglContext()->supportsBlits(); }
    bool isActive() const override { return active_; }
    bool blocksDirectScanout() const override { return active_; }
    int requestedEffectChainPosition() const override { return 99; }
    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds time) override {
        // A previous output buffer may already contain our filter. Repaint the
        // entire underlying scene before sampling it, including spatial effects.
        data.mask |= PAINT_SCREEN_TRANSFORMED;
        if (data.screen) data.paint += data.screen->geometry();
        effects->prePaintScreen(data, time);
    }
    void paintScreen(const RenderTarget& target, const RenderViewport& viewport, int mask,
                     const Region& region, LogicalOutput* output) override {
        effects->paintScreen(target, viewport, mask, region, output);
        if (!active_ || !shader_ || !output) return;
        const auto area = viewport.renderRect();
        const QSize size(qRound(area.width() * viewport.scale()), qRound(area.height() * viewport.scale()));
        if (size.isEmpty()) return;
        auto& buffer = buffers_[output];
        if (!buffer.texture || buffer.texture->size() != size) {
            auto texture = GLTexture::allocate(GL_RGBA16F, size);
            if (!texture) { Stop(); return; }
            texture->setFilter(GL_LINEAR); texture->setWrapMode(GL_CLAMP_TO_EDGE);
            auto framebuffer = std::make_unique<GLFramebuffer>(texture.get());
            if (!framebuffer->valid()) { Stop(); return; }
            buffer.framebuffer.reset();
            buffer.texture = std::move(texture); buffer.framebuffer = std::move(framebuffer);
        }
        if (!buffer.framebuffer->blitFromRenderTarget(target, viewport, area.toRect(), Rect(QPoint(), size))) {
            Stop(); return;
        }
        ShaderBinder binder(shader_.get());
        auto matrix = viewport.projectionMatrix();
        matrix.translate(area.x() * viewport.scale(), area.y() * viewport.scale());
        shader_->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
        shader_->setUniform("sampler", 0);
        shader_->setUniform("screenSize", QVector2D(size.width(), size.height()));
        if (animated_) shader_->setUniform("time", float(clock_.elapsed() % 1000000) / 1000.0F);
        const bool blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        buffer.texture->render(size);
        if (blend) glEnable(GL_BLEND);
        if (scissor) glEnable(GL_SCISSOR_TEST);
    }
    void postPaintScreen() override {
        if (active_ && animated_) effects->addRepaintFull();
        effects->postPaintScreen();
    }
public Q_SLOTS:
    bool Apply(const QString& source) {
        if (!supported() || source.size() > 128 * 1024 || !source.startsWith("#version 140")) return false;
        effects->makeOpenGLContextCurrent();
        auto candidate = ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, {}, source.toUtf8());
        if (!candidate || !candidate->isValid()) return false;
        shader_ = std::move(candidate); animated_ = source.contains("uniform float time;"); active_ = true;
        effects->addRepaintFull(); return true;
    }
    void Stop() {
        if (!active_) return;
        active_ = false; effects->makeOpenGLContextCurrent();
        buffers_.clear(); shader_.reset(); effects->addRepaintFull(); Q_EMIT Stopped();
    }
    bool Active() const { return active_; }
Q_SIGNALS:
    void Stopped();
private:
    struct Buffer {
        std::unique_ptr<GLTexture> texture;
        std::unique_ptr<GLFramebuffer> framebuffer;
    };
    std::map<LogicalOutput*, Buffer> buffers_;
    std::unique_ptr<GLShader> shader_;
    QElapsedTimer clock_;
    bool active_ = false;
    bool animated_ = false;
};
KWIN_EFFECT_FACTORY_SUPPORTED(ScreenFxEffect, "metadata.json", return ScreenFxEffect::supported();)
}
#include "ScreenFxEffect.moc"
