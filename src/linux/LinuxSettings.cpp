#include "LinuxSettings.h"
#include "core/EffectFields.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace screenfx::linuxfx {
namespace {

constexpr qint64 kSettingsLimit = 64 * 1024;
constexpr qint64 kPresetsLimit = 256 * 1024;
constexpr qsizetype kPresetLimit = 64;
constexpr qsizetype kNameLimit = 80;
constexpr int kMaximumDepth = 16;

bool Fail(QString& error, const char* message) {
    error = QString::fromLatin1(message);
    return false;
}

// QJsonDocument keeps the last duplicate object member. Reject duplicates and
// excessive nesting before parsing so an ambiguous version or effect is never saved over.
class JsonStructure {
public:
    explicit JsonStructure(const QByteArray& text) : text_(text) {}

    bool Validate() {
        const bool valid = Value(0);
        Whitespace();
        return valid && position_ == text_.size();
    }

private:
    char Peek() const { return position_ < text_.size() ? text_[position_] : '\0'; }
    bool Take(char expected) {
        if (Peek() != expected) { return false; }
        ++position_;
        return true;
    }
    void Whitespace() {
        while (Peek() == ' ' || Peek() == '\t' || Peek() == '\n' || Peek() == '\r') { ++position_; }
    }
    bool String(QByteArray* token = nullptr) {
        const qsizetype start = position_;
        if (!Take('"')) { return false; }
        while (position_ < text_.size()) {
            const char character = text_[position_++];
            if (character == '"') {
                if (token != nullptr) { *token = text_.mid(start, position_ - start); }
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20) { return false; }
            if (character == '\\') {
                if (position_ == text_.size()) { return false; }
                ++position_;
            }
        }
        return false;
    }
    bool Value(int depth) {
        if (depth > kMaximumDepth) { return false; }
        Whitespace();
        if (Take('{')) {
            QSet<QString> keys;
            Whitespace();
            if (Take('}')) { return true; }
            do {
                Whitespace();
                QByteArray keyToken;
                if (!String(&keyToken)) { return false; }
                const auto decoded = QJsonDocument::fromJson('[' + keyToken + ']');
                if (!decoded.isArray() || decoded.array().size() != 1 || !decoded.array().first().isString()) {
                    return false;
                }
                const auto key = decoded.array().first().toString();
                if (keys.contains(key)) { return false; }
                keys.insert(key);
                Whitespace();
                if (!Take(':') || !Value(depth + 1)) { return false; }
                Whitespace();
                if (Take('}')) { return true; }
            } while (Take(','));
            return false;
        }
        if (Take('[')) {
            Whitespace();
            if (Take(']')) { return true; }
            do {
                if (!Value(depth + 1)) { return false; }
                Whitespace();
                if (Take(']')) { return true; }
            } while (Take(','));
            return false;
        }
        if (Peek() == '"') { return String(); }
        const qsizetype start = position_;
        while (position_ < text_.size() && Peek() != ',' && Peek() != '}' && Peek() != ']' &&
               Peek() != ' ' && Peek() != '\t' && Peek() != '\n' && Peek() != '\r') { ++position_; }
        return position_ != start;
    }

    const QByteArray& text_;
    qsizetype position_ = 0;
};

bool ReadRoot(const QString& path, qint64 limit, QJsonObject& root, bool& exists, QString& error) {
    exists = false;
    if (path.isEmpty()) { return Fail(error, "The settings folder is unavailable."); }
    const QFileInfo info(path);
    // A dangling link is an existing unsafe path, not a missing settings file.
    if (!info.exists() && !info.isSymbolicLink()) { return true; }
    if (!info.isFile() || info.isSymbolicLink()) { return Fail(error, "The settings path is not a regular file."); }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return Fail(error, "The file could not be opened for reading."); }
    auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError) { return Fail(error, "The file could not be read."); }
    if (bytes.size() > limit) { return Fail(error, "The file exceeds the size limit. It has been preserved."); }
    if (bytes.startsWith("\xEF\xBB\xBF")) { bytes.remove(0, 3); }
    if (!JsonStructure(bytes).Validate()) { return Fail(error, "The JSON file is invalid. It has been preserved."); }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Fail(error, "The JSON file is invalid. It has been preserved.");
    }
    root = document.object();
    const auto version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || !std::isfinite(version.toDouble()) || std::floor(version.toDouble()) != version.toDouble()) {
        return Fail(error, "The file version is invalid. It has been preserved.");
    }
    if (version.toDouble() != 1.0) { return Fail(error, "This file version is not supported. The file has been preserved."); }
    exists = true;
    return true;
}

bool DecodeEffects(const QJsonObject& object, core::EffectSettings& effects, QString& error) {
    core::EffectSettings result;
    for (const auto& field : core::kEffectFields) {
        const auto key = QString::fromLatin1(field.name);
        if (!object.contains(key)) { continue; }
        const auto value = object.value(key);
        const double number = value.toDouble();
        if (!value.isDouble() || !std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
            return Fail(error, "An effect contains an invalid number. The file has been preserved.");
        }
        result.*(field.member) = std::clamp(static_cast<float>(number), field.minimum, field.maximum);
    }
    effects = result;
    return true;
}

bool EncodeEffects(const core::EffectSettings& effects, QJsonObject& object, QString& error) {
    for (const auto& field : core::kEffectFields) {
        const float value = effects.*(field.member);
        if (!std::isfinite(value)) { return Fail(error, "An effect contains an invalid number."); }
        object.insert(QString::fromLatin1(field.name), std::clamp(value, field.minimum, field.maximum));
    }
    return true;
}

bool DecodeSettings(const QJsonObject& root, core::EffectSettings& effects, QString& error) {
    // Validate Windows settings fields even though Linux only loads the effects.
    if (root.contains(QStringLiteral("enabled")) && !root.value(QStringLiteral("enabled")).isBool()) {
        return Fail(error, "The enabled setting is invalid. The file has been preserved.");
    }
    if (root.contains(QStringLiteral("monitorIndex"))) {
        const auto monitor = root.value(QStringLiteral("monitorIndex"));
        const double index = monitor.toDouble();
        if (!monitor.isDouble() || !std::isfinite(index) || index < 0 || index > std::numeric_limits<std::uint32_t>::max() ||
            std::floor(index) != index) {
            return Fail(error, "The monitor setting is invalid. The file has been preserved.");
        }
    }
    if (root.contains(QStringLiteral("framePacing"))) {
        const auto pacing = root.value(QStringLiteral("framePacing"));
        if (!pacing.isString() || (pacing.toString() != QStringLiteral("uncapped") && pacing.toString() != QStringLiteral("vsync"))) {
            return Fail(error, "The frame pacing setting is invalid. The file has been preserved.");
        }
    }
    if (!root.contains(QStringLiteral("effects"))) { effects = {}; return true; }
    if (!root.value(QStringLiteral("effects")).isObject()) {
        return Fail(error, "The effects object is invalid. The file has been preserved.");
    }
    return DecodeEffects(root.value(QStringLiteral("effects")).toObject(), effects, error);
}

bool ValidName(const QString& name) {
    if (name.isEmpty() || name.size() > kNameLimit || name.trimmed().isEmpty()) { return false; }
    for (qsizetype index = 0; index < name.size(); ++index) {
        const auto character = name[index];
        if (character.category() == QChar::Other_Control) { return false; }
        if (character.isHighSurrogate()) {
            if (++index == name.size() || !name[index].isLowSurrogate()) { return false; }
        } else if (character.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

bool DecodePresets(const QJsonObject& root, QMap<QString, core::EffectSettings>& presets, QString& error) {
    const auto value = root.value(QStringLiteral("presets"));
    if (!value.isArray() || value.toArray().size() > kPresetLimit) {
        return Fail(error, "The presets collection is invalid. The file has been preserved.");
    }
    QMap<QString, core::EffectSettings> result;
    QSet<QString> names;
    for (const auto& item : value.toArray()) {
        if (!item.isObject()) { return Fail(error, "A preset is invalid. The file has been preserved."); }
        const auto object = item.toObject();
        const auto name = object.value(QStringLiteral("name"));
        const auto effects = object.value(QStringLiteral("effects"));
        if (!name.isString() || !ValidName(name.toString()) || !effects.isObject()) {
            return Fail(error, "A preset name or effects object is invalid. The file has been preserved.");
        }
        const auto folded = name.toString().toCaseFolded();
        if (names.contains(folded)) { return Fail(error, "Preset names must be unique, ignoring letter case. The file has been preserved."); }
        core::EffectSettings decoded;
        if (!DecodeEffects(effects.toObject(), decoded, error)) { return false; }
        names.insert(folded);
        result.insert(name.toString(), decoded);
    }
    presets = std::move(result);
    return true;
}

bool WriteRoot(const QString& path, const QJsonObject& root, qint64 limit, QString& error) {
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > limit) { return Fail(error, "The settings exceed the supported file size limit."); }
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        return Fail(error, "The settings folder could not be created.");
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { return Fail(error, "The save could not be prepared."); }
    if (file.write(bytes) != bytes.size()) { return Fail(error, "The file could not be written."); }
    if (!file.commit()) { return Fail(error, "The file could not be replaced. The previous file has been preserved."); }
    return true;
}

} // namespace

JsonStore::JsonStore(QString directory) : directory_(std::move(directory)) {
    if (directory_.isEmpty()) {
        const auto config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        if (!config.isEmpty()) { directory_ = QDir(config).filePath(QStringLiteral("screenfx")); }
    }
}

QString JsonStore::SettingsPath() const {
    return directory_.isEmpty() ? QString{} : QDir(directory_).filePath(QStringLiteral("settings.json"));
}

QString JsonStore::PresetsPath() const {
    return directory_.isEmpty() ? QString{} : QDir(directory_).filePath(QStringLiteral("presets.json"));
}

bool JsonStore::LoadEffects(core::EffectSettings& effects, QString& error) const {
    error.clear();
    QJsonObject root;
    bool exists = false;
    if (!ReadRoot(SettingsPath(), kSettingsLimit, root, exists, error)) { return false; }
    if (!exists) { effects = {}; return true; }
    return DecodeSettings(root, effects, error);
}

bool JsonStore::SaveEffects(const core::EffectSettings& effects, QString& error) const {
    error.clear();
    QJsonObject encoded;
    if (!EncodeEffects(effects, encoded, error)) { return false; }
    QJsonObject root;
    bool exists = false;
    if (!ReadRoot(SettingsPath(), kSettingsLimit, root, exists, error)) { return false; }
    core::EffectSettings previous;
    if (exists && !DecodeSettings(root, previous, error)) { return false; }
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("effects"), encoded);
    return WriteRoot(SettingsPath(), root, kSettingsLimit, error);
}

bool JsonStore::LoadPresets(QMap<QString, core::EffectSettings>& presets, QString& error) const {
    error.clear();
    QJsonObject root;
    bool exists = false;
    if (!ReadRoot(PresetsPath(), kPresetsLimit, root, exists, error)) { return false; }
    if (!exists) { presets.clear(); return true; }
    return DecodePresets(root, presets, error);
}

bool JsonStore::SavePreset(const QString& name, const core::EffectSettings& effects, QString& error) const {
    error.clear();
    if (!ValidName(name)) { return Fail(error, "Preset names need 1 to 80 characters and cannot be blank or contain control characters."); }
    QJsonObject encoded;
    if (!EncodeEffects(effects, encoded, error)) { return false; }
    QJsonObject root;
    bool exists = false;
    if (!ReadRoot(PresetsPath(), kPresetsLimit, root, exists, error)) { return false; }
    QMap<QString, core::EffectSettings> previous;
    if (exists && !DecodePresets(root, previous, error)) { return false; }
    QJsonArray presets = root.value(QStringLiteral("presets")).toArray();
    for (qsizetype index = 0; index < presets.size(); ++index) {
        auto item = presets[index].toObject();
        if (item.value(QStringLiteral("name")).toString().toCaseFolded() == name.toCaseFolded()) {
            // Keep the original user spelling and unknown metadata when replacing a preset.
            item.insert(QStringLiteral("effects"), encoded);
            presets[index] = item;
            root.insert(QStringLiteral("presets"), presets);
            return WriteRoot(PresetsPath(), root, kPresetsLimit, error);
        }
    }
    if (presets.size() == kPresetLimit) { return Fail(error, "Up to 64 custom presets can be saved."); }
    presets.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("effects"), encoded}});
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("presets"), presets);
    return WriteRoot(PresetsPath(), root, kPresetsLimit, error);
}

bool JsonStore::DeletePreset(const QString& name, QString& error) const {
    error.clear();
    if (!ValidName(name)) { return Fail(error, "Choose a saved preset to delete."); }
    QJsonObject root;
    bool exists = false;
    if (!ReadRoot(PresetsPath(), kPresetsLimit, root, exists, error)) { return false; }
    if (!exists) { return Fail(error, "The selected preset was not found."); }
    QMap<QString, core::EffectSettings> previous;
    if (!DecodePresets(root, previous, error)) { return false; }
    auto presets = root.value(QStringLiteral("presets")).toArray();
    const auto folded = name.toCaseFolded();
    for (qsizetype index = 0; index < presets.size(); ++index) {
        if (presets[index].toObject().value(QStringLiteral("name")).toString().toCaseFolded() == folded) {
            presets.removeAt(index);
            // Keep every remaining entry and its unknown metadata exactly as decoded.
            root.insert(QStringLiteral("presets"), presets);
            return WriteRoot(PresetsPath(), root, kPresetsLimit, error);
        }
    }
    return Fail(error, "The selected preset was not found.");
}

} // namespace screenfx::linuxfx
