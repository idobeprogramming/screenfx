#pragma once

#include "core/Settings.h"

#include <QMap>
#include <QString>

namespace screenfx::linuxfx {

// Only explicit Save and Delete methods write files. Failed loads leave their outputs unchanged.
class JsonStore {
public:
    explicit JsonStore(QString directory = {});

    const QString& Directory() const { return directory_; }
    QString SettingsPath() const;
    QString PresetsPath() const;

    bool LoadEffects(core::EffectSettings& effects, QString& error) const;
    bool SaveEffects(const core::EffectSettings& effects, QString& error) const;
    bool LoadPresets(QMap<QString, core::EffectSettings>& presets, QString& error) const;
    bool SavePreset(const QString& name, const core::EffectSettings& effects, QString& error) const;
    bool DeletePreset(const QString& name, QString& error) const;

private:
    QString directory_;
};

} // namespace screenfx::linuxfx
