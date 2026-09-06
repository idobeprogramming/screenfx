#include "SettingsStore.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace screenfx::core {
namespace {

constexpr std::uint32_t kSettingsVersion = 1;
constexpr std::size_t kMaximumDocumentBytes = 64 * 1024;
constexpr unsigned int kMaximumDepth = 16;

struct EffectField {
    const char* name;
    float EffectSettings::* member;
    float minimum;
    float maximum;
};

constexpr std::array kEffectFields{
    EffectField{"globalIntensity", &EffectSettings::globalIntensity, 0.0F, 1.0F},
    EffectField{"brightness", &EffectSettings::brightness, -1.0F, 1.0F},
    EffectField{"contrast", &EffectSettings::contrast, 0.0F, 4.0F},
    EffectField{"saturation", &EffectSettings::saturation, 0.0F, 4.0F},
    EffectField{"gamma", &EffectSettings::gamma, 0.1F, 4.0F},
    EffectField{"grayscale", &EffectSettings::grayscale, 0.0F, 1.0F},
    EffectField{"sepia", &EffectSettings::sepia, 0.0F, 1.0F},
    EffectField{"scanlineIntensity", &EffectSettings::scanlineIntensity, 0.0F, 1.0F},
    EffectField{"scanlineSpacing", &EffectSettings::scanlineSpacing, 1.0F, 8.0F},
    EffectField{"scanlineThickness", &EffectSettings::scanlineThickness, 0.05F, 1.0F},
    EffectField{"phosphorIntensity", &EffectSettings::phosphorIntensity, 0.0F, 1.0F},
    EffectField{"pixelSize", &EffectSettings::pixelSize, 1.0F, 32.0F},
    EffectField{"sharpen", &EffectSettings::sharpen, 0.0F, 2.0F},
    EffectField{"chromaticAberration", &EffectSettings::chromaticAberration, 0.0F, 8.0F},
    EffectField{"bloomIntensity", &EffectSettings::bloomIntensity, 0.0F, 2.0F},
    EffectField{"bloomThreshold", &EffectSettings::bloomThreshold, 0.0F, 1.0F},
    EffectField{"bloomRadius", &EffectSettings::bloomRadius, 0.5F, 8.0F},
    EffectField{"vignetteIntensity", &EffectSettings::vignetteIntensity, 0.0F, 1.0F},
    EffectField{"vignetteWidth", &EffectSettings::vignetteWidth, 0.1F, 1.0F},
    EffectField{"grainIntensity", &EffectSettings::grainIntensity, 0.0F, 1.0F},
    EffectField{"grainSize", &EffectSettings::grainSize, 0.25F, 8.0F},
};

struct JsonValue {
    enum class Kind { Object, Array, String, Number, Boolean, Null };
    Kind kind = Kind::Null;
    std::string text;
    std::map<std::string, JsonValue, std::less<>> members;

    const JsonValue* Find(std::string_view key) const {
        const auto found = members.find(key);
        return found == members.end() ? nullptr : &found->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view document) : document_(document) {}

    JsonValue Parse() {
        JsonValue result = Value(0);
        Whitespace();
        Require(position_ == document_.size());
        return result;
    }

private:
    static void Require(bool condition) {
        if (!condition) { throw std::invalid_argument("Invalid settings JSON"); }
    }
    char Peek() const { return position_ < document_.size() ? document_[position_] : '\0'; }
    bool Take(char expected) {
        if (Peek() != expected) { return false; }
        ++position_;
        return true;
    }
    void Whitespace() {
        while (Peek() == ' ' || Peek() == '\t' || Peek() == '\r' || Peek() == '\n') { ++position_; }
    }
    static bool Digit(char value) { return value >= '0' && value <= '9'; }

    unsigned int HexQuad() {
        unsigned int value = 0;
        for (int digit = 0; digit < 4; ++digit) {
            const char character = Peek();
            Require((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                    (character >= 'A' && character <= 'F'));
            ++position_;
            value = value * 16 + (character <= '9' ? character - '0' :
                                 character <= 'F' ? character - 'A' + 10 : character - 'a' + 10);
        }
        return value;
    }

    static void AppendUtf8(std::string& result, unsigned int value) {
        if (value <= 0x7F) {
            result += static_cast<char>(value);
        } else if (value <= 0x7FF) {
            result += static_cast<char>(0xC0 | (value >> 6));
            result += static_cast<char>(0x80 | (value & 0x3F));
        } else if (value <= 0xFFFF) {
            result += static_cast<char>(0xE0 | (value >> 12));
            result += static_cast<char>(0x80 | ((value >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (value & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (value >> 18));
            result += static_cast<char>(0x80 | ((value >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((value >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (value & 0x3F));
        }
    }

    std::string String() {
        Require(Take('"'));
        std::string result;
        while (!Take('"')) {
            Require(position_ < document_.size());
            char character = document_[position_++];
            Require(static_cast<unsigned char>(character) >= 0x20);
            if (character != '\\') {
                result += character;
                continue;
            }
            Require(position_ < document_.size());
            character = document_[position_++];
            switch (character) {
            case '"': case '\\': case '/': result += character; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'u': {
                unsigned int value = HexQuad();
                if (value >= 0xD800 && value <= 0xDBFF) {
                    Require(Take('\\') && Take('u'));
                    const unsigned int low = HexQuad();
                    Require(low >= 0xDC00 && low <= 0xDFFF);
                    value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                } else {
                    Require(value < 0xDC00 || value > 0xDFFF);
                }
                AppendUtf8(result, value);
                break;
            }
            default: Require(false);
            }
        }
        Require(result.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, result.data(),
                                                      static_cast<int>(result.size()), nullptr, 0) != 0);
        return result;
    }

    JsonValue Value(unsigned int depth) {
        Require(depth <= kMaximumDepth);
        Whitespace();
        JsonValue result;
        if (Take('{')) {
            result.kind = JsonValue::Kind::Object;
            Whitespace();
            if (Take('}')) { return result; }
            do {
                Whitespace();
                std::string key = String();
                Whitespace();
                Require(Take(':'));
                JsonValue child = Value(depth + 1);
                Require(result.members.emplace(std::move(key), std::move(child)).second);
                Whitespace();
                if (Take('}')) { return result; }
            } while (Take(','));
            Require(false);
        } else if (Take('[')) {
            result.kind = JsonValue::Kind::Array;
            Whitespace();
            if (Take(']')) { return result; }
            do {
                Value(depth + 1); // Unknown arrays still receive complete syntax validation.
                Whitespace();
                if (Take(']')) { return result; }
            } while (Take(','));
            Require(false);
        } else if (Peek() == '"') {
            result.kind = JsonValue::Kind::String;
            result.text = String();
        } else if (Peek() == 't' || Peek() == 'f' || Peek() == 'n') {
            const std::string_view token = Peek() == 't' ? "true" : Peek() == 'f' ? "false" : "null";
            Require(document_.substr(position_, token.size()) == token);
            position_ += token.size();
            result.kind = token == "null" ? JsonValue::Kind::Null : JsonValue::Kind::Boolean;
            result.text = token;
        } else {
            result.kind = JsonValue::Kind::Number;
            const std::size_t start = position_;
            Take('-');
            if (!Take('0')) {
                Require(Peek() >= '1' && Peek() <= '9');
                while (Digit(Peek())) { ++position_; }
            }
            if (Take('.')) {
                Require(Digit(Peek()));
                while (Digit(Peek())) { ++position_; }
            }
            if (Take('e') || Take('E')) {
                if (!Take('+')) { Take('-'); }
                Require(Digit(Peek()));
                while (Digit(Peek())) { ++position_; }
            }
            result.text = document_.substr(start, position_ - start);
        }
        return result;
    }

    std::string_view document_;
    std::size_t position_ = 0;
};

template<typename Number>
bool ReadNumber(const JsonValue& value, Number& number) {
    if (value.kind != JsonValue::Kind::Number) { return false; }
    const auto result = std::from_chars(value.text.data(), value.text.data() + value.text.size(), number);
    return result.ec == std::errc{} && result.ptr == value.text.data() + value.text.size();
}

SettingsLoadResult FromJson(const std::string& document) {
    SettingsLoadResult result;
    result.status = SettingsLoadStatus::Invalid;
    result.error = L"Le fichier de réglages est invalide. Il a été conservé.";
    const JsonValue root = JsonParser(document).Parse();
    if (root.kind != JsonValue::Kind::Object) { return result; }
    const JsonValue* version = root.Find("version");
    std::uint32_t parsedVersion = 0;
    if (version == nullptr || !ReadNumber(*version, parsedVersion)) { return result; }
    if (parsedVersion != kSettingsVersion) {
        result.status = SettingsLoadStatus::UnsupportedVersion;
        result.error = L"La version du fichier de réglages n'est pas prise en charge. Le fichier a été conservé.";
        return result;
    }

    AppSettings settings;
    if (const JsonValue* enabled = root.Find("enabled")) {
        if (enabled->kind != JsonValue::Kind::Boolean) { return result; }
        settings.enabled = enabled->text == "true";
    }
    if (const JsonValue* monitor = root.Find("monitorIndex")) {
        if (!ReadNumber(*monitor, settings.monitorIndex)) { return result; }
    }
    if (const JsonValue* pacing = root.Find("framePacing")) {
        if (pacing->kind != JsonValue::Kind::String || (pacing->text != "uncapped" && pacing->text != "vsync")) {
            return result;
        }
        settings.framePacing = pacing->text == "vsync" ? FramePacingMode::VSync : FramePacingMode::Uncapped;
    }
    if (const JsonValue* effects = root.Find("effects")) {
        if (effects->kind != JsonValue::Kind::Object) { return result; }
        for (const auto& field : kEffectFields) {
            if (const JsonValue* value = effects->Find(field.name)) {
                float parsed = 0.0F;
                if (!ReadNumber(*value, parsed) || !std::isfinite(parsed)) { return result; }
                settings.effects.*(field.member) = std::clamp(parsed, field.minimum, field.maximum);
            }
        }
    }
    result.settings = settings;
    result.status = SettingsLoadStatus::Loaded;
    result.error.clear();
    return result;
}

std::string ToJson(const AppSettings& settings) {
    std::ostringstream document;
    document.imbue(std::locale::classic());
    document << std::setprecision(std::numeric_limits<float>::max_digits10);
    document << "{\n"
             << "  \"version\": " << kSettingsVersion << ",\n"
             << "  \"enabled\": " << (settings.enabled ? "true" : "false") << ",\n"
             << "  \"monitorIndex\": " << settings.monitorIndex << ",\n"
             << "  \"framePacing\": \"" << (settings.framePacing == FramePacingMode::VSync ? "vsync" : "uncapped") << "\",\n"
             << "  \"effects\": {\n";
    for (std::size_t index = 0; index < kEffectFields.size(); ++index) {
        const auto& field = kEffectFields[index];
        document << "    \"" << field.name << "\": " << settings.effects.*(field.member)
                 << (index + 1 < kEffectFields.size() ? ",\n" : "\n");
    }
    document << "  }\n}\n";
    return document.str();
}

std::wstring WindowsError(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = length != 0 ? std::wstring(buffer, length) : L"Erreur Windows " + std::to_wstring(code);
    LocalFree(buffer);
    return message;
}

class PendingFile {
public:
    explicit PendingFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~PendingFile() {
        if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); }
        if (created && !committed) { DeleteFileW(path_.c_str()); }
    }
    PendingFile(const PendingFile&) = delete;
    PendingFile& operator=(const PendingFile&) = delete;

    const std::filesystem::path& Path() const { return path_; }
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool created = false;
    bool committed = false;

private:
    std::filesystem::path path_;
};

} // namespace

std::filesystem::path SettingsStore::Path() {
    PWSTR knownFolder = nullptr;
    const HRESULT status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &knownFolder);
    std::filesystem::path path;
    if (SUCCEEDED(status) && knownFolder != nullptr) {
        path = std::filesystem::path(knownFolder) / L"ScreenFX" / L"settings.json";
    }
    CoTaskMemFree(knownFolder);
    return path;
}

AppSettings SettingsStore::Load() { return Load(Path()); }

AppSettings SettingsStore::Load(const std::filesystem::path& path) { return LoadWithStatus(path).settings; }

SettingsLoadResult SettingsStore::LoadWithStatus(const std::filesystem::path& path) {
    SettingsLoadResult result;
    try {
        if (path.empty()) {
            result.status = SettingsLoadStatus::IoError;
            result.error = L"Le dossier des réglages est indisponible.";
            return result;
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            result.status = SettingsLoadStatus::IoError;
            result.error = L"Impossible d'accéder au fichier de réglages.";
            return result;
        }
        if (!exists) { return result; }
        if (!std::filesystem::is_regular_file(path, error) || error) {
            result.status = SettingsLoadStatus::IoError;
            result.error = L"Le chemin des réglages n'est pas un fichier accessible.";
            return result;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            result.status = SettingsLoadStatus::IoError;
            result.error = L"Impossible de lire le fichier de réglages.";
            return result;
        }
        // Reading at most the limit plus one also handles files growing after they are opened.
        std::string document(kMaximumDocumentBytes + 1, '\0');
        file.read(document.data(), static_cast<std::streamsize>(document.size()));
        if (file.bad() || (file.fail() && !file.eof())) {
            result.status = SettingsLoadStatus::IoError;
            result.error = L"La lecture du fichier de réglages a échoué.";
            return result;
        }
        document.resize(static_cast<std::size_t>(file.gcount()));
        if (document.size() > kMaximumDocumentBytes) {
            result.status = SettingsLoadStatus::Invalid;
            result.error = L"Le fichier de réglages dépasse 64 Ko. Il a été conservé.";
            return result;
        }
        // Accept the UTF-8 BOM commonly emitted by Windows text editors.
        if (document.starts_with("\xEF\xBB\xBF")) { document.erase(0, 3); }
        return FromJson(document);
    } catch (const std::invalid_argument&) {
        result.status = SettingsLoadStatus::Invalid;
        result.error = L"Le fichier de réglages est invalide. Il a été conservé.";
    } catch (...) {
        result.status = SettingsLoadStatus::IoError;
        result.error = L"Impossible de charger les réglages.";
    }
    return result;
}

bool SettingsStore::Save(const AppSettings& settings) { return Save(settings, Path()); }

bool SettingsStore::Save(const AppSettings& settings, const std::filesystem::path& path, std::wstring* error) {
    if (error != nullptr) { error->clear(); }
    const auto fail = [error](std::wstring message) {
        if (error != nullptr) { *error = std::move(message); }
        return false;
    };
    try {
        AppSettings sanitized = settings;
        if (settings.framePacing != FramePacingMode::VSync && settings.framePacing != FramePacingMode::Uncapped) {
            return fail(L"Le mode d'affichage est invalide.");
        }
        for (const auto& field : kEffectFields) {
            const float value = settings.effects.*(field.member);
            if (!std::isfinite(value)) { return fail(L"Un réglage contient un nombre invalide."); }
            sanitized.effects.*(field.member) = std::clamp(value, field.minimum, field.maximum);
        }
        const SettingsLoadResult existing = LoadWithStatus(path);
        if (existing.status != SettingsLoadStatus::Loaded && existing.status != SettingsLoadStatus::NotFound) {
            return fail(existing.error);
        }
        if (!path.parent_path().empty()) { std::filesystem::create_directories(path.parent_path()); }
        const std::string document = ToJson(sanitized);
        static std::atomic<std::uint64_t> serial{0};
        // A unique CREATE_NEW file prevents concurrent saves sharing/truncating the same temporary file.
        for (unsigned int attempt = 0; attempt < 16; ++attempt) {
            PendingFile temporary(path.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                                  std::to_wstring(serial.fetch_add(1)) + L".tmp");
            temporary.handle = CreateFileW(temporary.Path().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);
            if (temporary.handle == INVALID_HANDLE_VALUE) {
                const DWORD code = GetLastError();
                if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) { continue; }
                return fail(L"Impossible de préparer l'enregistrement : " + WindowsError(code));
            }
            temporary.created = true;
            DWORD written = 0;
            if (!WriteFile(temporary.handle, document.data(), static_cast<DWORD>(document.size()), &written, nullptr)) {
                return fail(L"Impossible d'écrire les réglages : " + WindowsError(GetLastError()));
            }
            if (written != document.size()) { return fail(L"L'écriture des réglages est incomplète."); }
            if (!FlushFileBuffers(temporary.handle)) {
                return fail(L"Impossible de terminer l'enregistrement : " + WindowsError(GetLastError()));
            }
            const BOOL closed = CloseHandle(temporary.handle);
            temporary.handle = INVALID_HANDLE_VALUE;
            if (!closed) { return fail(L"Impossible de fermer le fichier de réglages : " + WindowsError(GetLastError())); }
            if (!MoveFileExW(temporary.Path().c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return fail(L"Impossible de remplacer le fichier de réglages : " + WindowsError(GetLastError()));
            }
            temporary.committed = true;
            return true;
        }
        return fail(L"Impossible de créer un fichier temporaire pour les réglages.");
    } catch (...) {
        return fail(L"Impossible d'enregistrer les réglages.");
    }
}

} // namespace screenfx::core
