#pragma once
#include "core/Settings.h"
#include <QObject>
#include <QString>

namespace screenfx::linuxfx {
class CompositorBackend : public QObject {
    Q_OBJECT
public:
    explicit CompositorBackend(QString requested = {}, QObject* parent = nullptr);
    ~CompositorBackend() override;
    QString Name() const;
    bool Apply(const core::EffectSettings& effects, QString& error);
    bool Stop(QString& error);
    bool Active() const { return active_; }
    static bool RecoverHyprland(QString& error, const QString& controllerOwner = {});
Q_SIGNALS:
    void stopped();
private Q_SLOTS:
    void RemoteStopped();
private:
    enum class Kind { None, KWin, Hyprland } kind_ = Kind::None;
    bool active_ = false;
    unsigned generation_ = 0;
};
}
