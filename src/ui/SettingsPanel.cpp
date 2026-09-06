#include "SettingsPanel.h"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <string>
#include <tuple>
#include <utility>

namespace screenfx::ui {
namespace {

constexpr int kMargin = 24;
constexpr int kColumnGap = 24;
constexpr int kHeaderHeight = 24;
constexpr int kRowHeight = 38;
constexpr int kControlHeight = 24;

void SetPosition(HWND control, int x, int y, int width, int height) {
    if (control != nullptr) {
        SetWindowPos(control, nullptr, x, y, std::max(1, width), std::max(1, height), SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

std::wstring MonitorLabel(const platform::MonitorInfo& monitor) {
    return monitor.deviceName + L"  " +
           std::to_wstring(monitor.bounds.right - monitor.bounds.left) + L" × " +
           std::to_wstring(monitor.bounds.bottom - monitor.bounds.top) +
           (monitor.primary ? L"  (principal)" : L"");
}

bool EffectsEqual(const core::EffectSettings& left, const core::EffectSettings& right) {
    return std::tie(
               left.globalIntensity,
               left.brightness,
               left.contrast,
               left.saturation,
               left.gamma,
               left.grayscale,
               left.sepia,
               left.scanlineIntensity,
               left.scanlineSpacing,
               left.scanlineThickness,
               left.phosphorIntensity,
               left.pixelSize,
               left.sharpen,
               left.chromaticAberration,
               left.bloomIntensity,
               left.bloomThreshold,
               left.bloomRadius,
               left.vignetteIntensity,
               left.vignetteWidth,
               left.grainIntensity,
               left.grainSize) ==
           std::tie(
               right.globalIntensity,
               right.brightness,
               right.contrast,
               right.saturation,
               right.gamma,
               right.grayscale,
               right.sepia,
               right.scanlineIntensity,
               right.scanlineSpacing,
               right.scanlineThickness,
               right.phosphorIntensity,
               right.pixelSize,
               right.sharpen,
               right.chromaticAberration,
               right.bloomIntensity,
               right.bloomThreshold,
               right.bloomRadius,
               right.vignetteIntensity,
               right.vignetteWidth,
               right.grainIntensity,
               right.grainSize);
}

bool SettingsEqual(const core::AppSettings& left, const core::AppSettings& right) {
    return left.enabled == right.enabled && left.monitorIndex == right.monitorIndex &&
           left.framePacing == right.framePacing && EffectsEqual(left.effects, right.effects);
}

} // namespace

SettingsPanel::~SettingsPanel() {
    Shutdown();
}

void SettingsPanel::TrackControl(HWND control) {
    if (control != nullptr) {
        controls_.push_back(control);
    }
}

void SettingsPanel::SetControlFont(HWND control) {
    if (control != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
}

void SettingsPanel::UpdateFont() {
    dpi_ = GetDpiForWindow(window_);
    if (dpi_ == 0) dpi_ = 96;
    HFONT replacement = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT previous = font_;
    font_ = replacement;
    for (HWND control : controls_) SetControlFont(control);
    if (previous != nullptr) DeleteObject(previous);
}

HWND SettingsPanel::CreateLabel(const wchar_t* text, int x, int y, int width, int height) {
    HWND label = CreateWindowExW(
        0,
        WC_STATICW,
        text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x,
        y,
        std::max(1, width),
        std::max(1, height),
        window_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    TrackControl(label);
    SetControlFont(label);
    return label;
}

HWND SettingsPanel::CreateSlider(int id, const wchar_t* label, float minimum, float maximum) {
    HWND labelControl = CreateLabel(label, 0, 0, 1, 1);
    HWND slider = CreateWindowExW(
        0,
        TRACKBAR_CLASSW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
        0,
        0,
        100,
        kControlHeight,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    if (slider != nullptr) {
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
        SendMessageW(slider, TBM_SETPAGESIZE, 0, 50);
        SendMessageW(slider, TBM_SETTICFREQ, 250, 0);
    }
    TrackControl(slider);
    SetControlFont(slider);
    sliders_.push_back(SliderBinding{id, slider, labelControl, minimum, maximum, label});
    return slider;
}

bool SettingsPanel::CreateControls() {
    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    const HINSTANCE module = GetModuleHandleW(nullptr);
    enabled_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"Filtre actif",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0,
        0,
        150,
        kControlHeight,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEnabled)),
        module,
        nullptr);
    stop_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"Arrêt immédiat",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        150,
        kControlHeight,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStop)),
        module,
        nullptr);
    monitorLabel_ = CreateLabel(L"Moniteur", 0, 0, 1, 1);
    monitorCombo_ = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0,
        0,
        260,
        220,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMonitor)),
        module,
        nullptr);
    pacingLabel_ = CreateLabel(L"Fréquence de présentation", 0, 0, 1, 1);
    pacingCombo_ = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0,
        0,
        260,
        120,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPacing)),
        module,
        nullptr);
    if (pacingCombo_ != nullptr) {
        SendMessageW(pacingCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Sans plafond logiciel"));
        SendMessageW(pacingCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Synchronisé à l’écran"));
    }

    status_ = CreateLabel(L"", 0, 0, 1, 1);
    capturedStats_ = CreateLabel(L"", 0, 0, 1, 1);
    presentedStats_ = CreateLabel(L"", 0, 0, 1, 1);
    droppedStats_ = CreateLabel(L"", 0, 0, 1, 1);
    captureState_ = CreateLabel(L"", 0, 0, 1, 1);
    colorsHeader_ = CreateLabel(L"Couleurs", 0, 0, 1, 1);
    crtHeader_ = CreateLabel(L"CRT", 0, 0, 1, 1);
    imageHeader_ = CreateLabel(L"Image", 0, 0, 1, 1);
    generalHeader_ = CreateLabel(L"Intensité", 0, 0, 1, 1);

    sliders_.clear();
    CreateSlider(kBrightness, L"Luminosité", -1.0F, 1.0F);
    CreateSlider(kContrast, L"Contraste", 0.0F, 4.0F);
    CreateSlider(kSaturation, L"Saturation", 0.0F, 4.0F);
    CreateSlider(kGamma, L"Gamma", 0.1F, 4.0F);
    CreateSlider(kGrayscale, L"Niveaux de gris", 0.0F, 1.0F);
    CreateSlider(kSepia, L"Sépia", 0.0F, 1.0F);
    CreateSlider(kScanline, L"Scanlines", 0.0F, 1.0F);
    CreateSlider(kSpacing, L"Espacement des scanlines", 1.0F, 8.0F);
    CreateSlider(kThickness, L"Épaisseur des scanlines", 0.05F, 1.0F);
    CreateSlider(kPhosphor, L"Masque phosphore RGB", 0.0F, 1.0F);
    CreateSlider(kPixelSize, L"Pixellisation", 1.0F, 32.0F);
    CreateSlider(kSharpen, L"Netteté", 0.0F, 2.0F);
    CreateSlider(kChromatic, L"Aberration chromatique", 0.0F, 8.0F);
    CreateSlider(kBloom, L"Halo", 0.0F, 2.0F);
    CreateSlider(kBloomThreshold, L"Seuil du halo", 0.0F, 1.0F);
    CreateSlider(kBloomRadius, L"Rayon du halo", 0.5F, 8.0F);
    CreateSlider(kVignette, L"Vignette", 0.0F, 1.0F);
    CreateSlider(kVignetteWidth, L"Largeur vignette", 0.1F, 1.0F);
    CreateSlider(kGrain, L"Grain", 0.0F, 1.0F);
    CreateSlider(kGrainSize, L"Taille du grain", 0.25F, 8.0F);
    CreateSlider(kGlobalIntensity, L"Intensité globale", 0.0F, 1.0F);

    reset_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"Réinitialiser les effets",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        220,
        kControlHeight,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReset)),
        module,
        nullptr);
    save_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"Enregistrer",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        140,
        kControlHeight,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSave)),
        module,
        nullptr);

    const std::array<HWND, 6> ownedControls{enabled_, stop_, monitorCombo_, pacingCombo_, reset_, save_};
    for (HWND control : ownedControls) {
        TrackControl(control);
    }

    const std::array<HWND, 8> controls{
        enabled_, stop_, monitorCombo_, pacingCombo_, reset_, save_, status_, captureState_};
    for (HWND control : controls) {
        SetControlFont(control);
    }
    return enabled_ != nullptr && stop_ != nullptr && monitorCombo_ != nullptr && pacingCombo_ != nullptr &&
           std::all_of(sliders_.begin(), sliders_.end(), [](const SliderBinding& binding) {
               return binding.control != nullptr && binding.labelControl != nullptr;
           }) && reset_ != nullptr && save_ != nullptr;
}

bool SettingsPanel::Initialize(HWND window) {
    if (initialized_) {
        return true;
    }
    if (window == nullptr || !IsWindow(window)) {
        return false;
    }
    window_ = window;
    UpdateFont();
    if (!CreateControls()) {
        Shutdown();
        return false;
    }
    initialized_ = true;
    RECT client{};
    GetClientRect(window_, &client);
    LayoutControls(std::max(1L, client.right - client.left), std::max(1L, client.bottom - client.top));
    return true;
}

void SettingsPanel::Shutdown() {
    for (auto it = controls_.rbegin(); it != controls_.rend(); ++it) {
        if (*it != nullptr && IsWindow(*it)) {
            DestroyWindow(*it);
        }
    }
    controls_.clear();
    initialized_ = false;
    settings_ = nullptr;
    syncedSettings_ = {};
    sliders_.clear();
    pendingActions_ = {};
    monitorLabels_.clear();
    if (font_ != nullptr) DeleteObject(font_);
    font_ = nullptr;
    scrollX_ = 0;
    scrollY_ = 0;
    lastCapturedFrames_ = 0;
    lastDroppedFrames_ = 0;
    lastPresentedFrames_ = 0;
    lastStatus_.clear();
    statsInitialized_ = false;
    hasSyncedSettings_ = false;
    window_ = nullptr;
    enabled_ = nullptr;
    stop_ = nullptr;
    monitorCombo_ = nullptr;
    pacingCombo_ = nullptr;
    monitorLabel_ = nullptr;
    pacingLabel_ = nullptr;
    status_ = nullptr;
    capturedStats_ = nullptr;
    presentedStats_ = nullptr;
    droppedStats_ = nullptr;
    captureState_ = nullptr;
    save_ = nullptr;
    reset_ = nullptr;
    colorsHeader_ = nullptr;
    crtHeader_ = nullptr;
    imageHeader_ = nullptr;
    generalHeader_ = nullptr;
}

HWND SettingsPanel::FindControl(int id) const {
    switch (id) {
    case kEnabled: return enabled_;
    case kStop: return stop_;
    case kMonitor: return monitorCombo_;
    case kPacing: return pacingCombo_;
    case kSave: return save_;
    case kReset: return reset_;
    default: break;
    }
    for (const SliderBinding& binding : sliders_) {
        if (binding.id == id) {
            return binding.control;
        }
    }
    return nullptr;
}

int SettingsPanel::ToTrackbar(float value, float minimum, float maximum) {
    if (maximum <= minimum) {
        return 0;
    }
    const float normalized = std::clamp((value - minimum) / (maximum - minimum), 0.0F, 1.0F);
    return static_cast<int>(normalized * 1000.0F + 0.5F);
}

float SettingsPanel::FromTrackbar(int position, float minimum, float maximum) {
    const float normalized = std::clamp(static_cast<float>(position) / 1000.0F, 0.0F, 1.0F);
    return minimum + normalized * (maximum - minimum);
}

float SettingsPanel::ReadSlider(int id) const {
    for (const SliderBinding& binding : sliders_) {
        if (binding.id == id && binding.control != nullptr) {
            const int position = static_cast<int>(SendMessageW(binding.control, TBM_GETPOS, 0, 0));
            return FromTrackbar(position, binding.minimum, binding.maximum);
        }
    }
    return 0.0F;
}

void SettingsPanel::SyncControls(const core::AppSettings& settings, const std::vector<platform::MonitorInfo>& monitors) {
    if (syncing_) {
        return;
    }
    syncing_ = true;
    if (enabled_ != nullptr) {
        SendMessageW(enabled_, BM_SETCHECK, settings.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (pacingCombo_ != nullptr) {
        SendMessageW(pacingCombo_, CB_SETCURSEL, settings.framePacing == core::FramePacingMode::VSync ? 1 : 0, 0);
    }
    std::vector<std::wstring> labels;
    labels.reserve(monitors.size());
    for (const auto& monitor : monitors) labels.push_back(MonitorLabel(monitor));
    if (monitorCombo_ != nullptr && monitorLabels_ != labels) {
        SendMessageW(monitorCombo_, CB_RESETCONTENT, 0, 0);
        for (const platform::MonitorInfo& monitor : monitors) {
            const std::wstring label = MonitorLabel(monitor);
            SendMessageW(monitorCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        monitorLabels_ = std::move(labels);
    }
    if (monitorCombo_ != nullptr) {
        if (monitors.empty()) {
            SendMessageW(monitorCombo_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        } else {
            const auto selected = std::min<std::size_t>(settings.monitorIndex, monitors.size() - 1);
            SendMessageW(monitorCombo_, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        }
    }

    const std::array<std::pair<int, float>, 21> values{
        std::pair{kBrightness, settings.effects.brightness},
        std::pair{kContrast, settings.effects.contrast},
        std::pair{kSaturation, settings.effects.saturation},
        std::pair{kGamma, settings.effects.gamma},
        std::pair{kGrayscale, settings.effects.grayscale},
        std::pair{kSepia, settings.effects.sepia},
        std::pair{kScanline, settings.effects.scanlineIntensity},
        std::pair{kSpacing, settings.effects.scanlineSpacing},
        std::pair{kThickness, settings.effects.scanlineThickness},
        std::pair{kPhosphor, settings.effects.phosphorIntensity},
        std::pair{kPixelSize, settings.effects.pixelSize},
        std::pair{kSharpen, settings.effects.sharpen},
        std::pair{kChromatic, settings.effects.chromaticAberration},
        std::pair{kBloom, settings.effects.bloomIntensity},
        std::pair{kBloomThreshold, settings.effects.bloomThreshold},
        std::pair{kBloomRadius, settings.effects.bloomRadius},
        std::pair{kVignette, settings.effects.vignetteIntensity},
        std::pair{kVignetteWidth, settings.effects.vignetteWidth},
        std::pair{kGrain, settings.effects.grainIntensity},
        std::pair{kGrainSize, settings.effects.grainSize},
        std::pair{kGlobalIntensity, settings.effects.globalIntensity},
    };
    for (const auto& [id, value] : values) {
        for (const SliderBinding& binding : sliders_) {
            if (binding.id == id && binding.control != nullptr) {
                SendMessageW(binding.control, TBM_SETPOS, TRUE, ToTrackbar(value, binding.minimum, binding.maximum));
                break;
            }
        }
    }
    syncing_ = false;
}

void SettingsPanel::UpdateStats(
    bool captureExcluded,
    std::uint64_t capturedFrames,
    std::uint64_t droppedFrames,
    std::uint64_t presentedFrames,
    std::wstring_view status) {
    if (statsInitialized_ && capturedFrames == lastCapturedFrames_ && droppedFrames == lastDroppedFrames_ &&
        presentedFrames == lastPresentedFrames_ && status == lastStatus_ && captureExcluded == lastCaptureExcluded_) {
        return;
    }
    statsInitialized_ = true;
    lastCapturedFrames_ = capturedFrames;
    lastDroppedFrames_ = droppedFrames;
    lastPresentedFrames_ = presentedFrames;
    lastStatus_ = status;
    lastCaptureExcluded_ = captureExcluded;
    if (status_ != nullptr) {
        SetWindowTextW(status_, std::wstring(status).c_str());
    }
    if (capturedStats_ != nullptr) {
        SetWindowTextW(capturedStats_, (L"Frames capturées : " + std::to_wstring(capturedFrames)).c_str());
    }
    if (presentedStats_ != nullptr) {
        SetWindowTextW(presentedStats_, (L"Frames présentées : " + std::to_wstring(presentedFrames)).c_str());
    }
    if (droppedStats_ != nullptr) {
        SetWindowTextW(droppedStats_, (L"Frames perdues : " + std::to_wstring(droppedFrames)).c_str());
    }
    if (captureState_ != nullptr) {
        SetWindowTextW(captureState_, captureExcluded ? L"Exclusion de capture : active" : L"Exclusion de capture : indisponible");
    }
}

void SettingsPanel::LayoutControls(int width, int height) {
    if (!initialized_ || window_ == nullptr) {
        return;
    }
    const int logicalWidth = MulDiv(width, 96, dpi_);
    const int logicalHeight = MulDiv(height, 96, dpi_);
    width = std::max(900, logicalWidth);
    height = std::max(730, logicalHeight);
    // Keep every control reachable on small displays and when the DPI grows.
    for (int bar : {SB_HORZ, SB_VERT}) {
        SCROLLINFO scroll{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
        scroll.nMax = (bar == SB_HORZ ? width : height) - 1;
        scroll.nPage = static_cast<UINT>(std::max(1, bar == SB_HORZ ? logicalWidth : logicalHeight));
        scroll.nPos = bar == SB_HORZ ? scrollX_ : scrollY_;
        SetScrollInfo(window_, bar, &scroll, TRUE);
        if (bar == SB_HORZ) scrollX_ = GetScrollPos(window_, bar);
        else scrollY_ = GetScrollPos(window_, bar);
    }
    auto place = [this](HWND control, int x, int y, int w, int h) {
        SetPosition(control, MulDiv(x - scrollX_, dpi_, 96), MulDiv(y - scrollY_, dpi_, 96),
                    MulDiv(w, dpi_, 96), MulDiv(h, dpi_, 96));
    };
    const int usableWidth = width - 2 * kMargin;
    const int columnWidth = std::max(260, (usableWidth - kColumnGap) / 2);
    const int rightX = kMargin + columnWidth + kColumnGap;
    const int comboWidth = std::max(180, columnWidth - 120);

    place(enabled_, kMargin, 16, 160, kControlHeight);
    place(stop_, kMargin + 175, 16, 150, kControlHeight);
    place(monitorLabel_, kMargin, 54, 90, kControlHeight);
    place(monitorCombo_, kMargin + 92, 51, comboWidth, 220);
    place(pacingLabel_, rightX, 54, 150, kControlHeight);
    place(pacingCombo_, rightX + 154, 51, std::max(160, columnWidth - 154), 120);
    place(status_, kMargin, 86, usableWidth, 42);

    place(colorsHeader_, kMargin, 132, columnWidth, kHeaderHeight);
    place(crtHeader_, kMargin, 132 + kHeaderHeight + 6 * kRowHeight + 8, columnWidth, kHeaderHeight);
    place(imageHeader_, rightX, 132, columnWidth, kHeaderHeight);
    place(generalHeader_, rightX, 132 + kHeaderHeight + 9 * kRowHeight + 8, columnWidth, kHeaderHeight);

    auto placeColumn = [&](int firstId, int count, int x, int startY) {
        int row = 0;
        for (SliderBinding& binding : sliders_) {
            if ((firstId == kBrightness && binding.id > kSepia) ||
                (firstId == kScanline && (binding.id < kScanline || binding.id > kPixelSize)) ||
                (firstId == kSharpen && (binding.id < kSharpen || binding.id > kGrainSize)) ||
                (firstId == kGlobalIntensity && binding.id != kGlobalIntensity)) {
                continue;
            }
            if (row >= count) {
                break;
            }
            place(binding.labelControl, x, startY + row * kRowHeight, columnWidth, 16);
            place(binding.control, x, startY + row * kRowHeight + 14, columnWidth, kControlHeight);
            ++row;
        }
    };
    placeColumn(kBrightness, 6, kMargin, 156);
    placeColumn(kScanline, 5, kMargin, 156 + kHeaderHeight + 6 * kRowHeight + 8);
    placeColumn(kSharpen, 9, rightX, 156);
    placeColumn(kGlobalIntensity, 1, rightX, 156 + kHeaderHeight + 9 * kRowHeight + 8);

    const int bottomY = std::max(624, height - 96);
    place(reset_, kMargin, bottomY, 220, kControlHeight + 4);
    place(save_, kMargin + 232, bottomY, 140, kControlHeight + 4);
    place(capturedStats_, rightX, bottomY - 2, columnWidth, 18);
    place(presentedStats_, rightX, bottomY + 18, columnWidth, 18);
    place(droppedStats_, rightX, bottomY + 38, columnWidth, 18);
    place(captureState_, rightX, bottomY + 58, columnWidth, 18);
}

void SettingsPanel::Scroll(int bar, int position) {
    SetScrollPos(window_, bar, position, TRUE);
    if (bar == SB_HORZ) scrollX_ = GetScrollPos(window_, bar);
    else scrollY_ = GetScrollPos(window_, bar);
    RECT client{};
    GetClientRect(window_, &client);
    LayoutControls(client.right, client.bottom);
    InvalidateRect(window_, nullptr, TRUE);
}

void SettingsPanel::EnsureFocusVisible() {
    HWND focused = GetFocus();
    if (!initialized_ || focused == nullptr || !IsChild(window_, focused)) return;
    RECT bounds{}, client{};
    GetWindowRect(focused, &bounds);
    MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&bounds), 2);
    GetClientRect(window_, &client);
    if (bounds.left < 0 || bounds.right > client.right) {
        const int delta = bounds.left < 0 ? bounds.left : bounds.right - client.right;
        Scroll(SB_HORZ, scrollX_ + MulDiv(delta, 96, dpi_));
    }
    if (bounds.top < 0 || bounds.bottom > client.bottom) {
        const int delta = bounds.top < 0 ? bounds.top : bounds.bottom - client.bottom;
        Scroll(SB_VERT, scrollY_ + MulDiv(delta, 96, dpi_));
    }
}

bool SettingsPanel::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (!initialized_) {
        return false;
    }
    if (message == WM_DPICHANGED) {
        UpdateFont();
        return false;
    }
    if ((message == WM_VSCROLL || message == WM_HSCROLL) && lParam == 0) {
        const int bar = message == WM_VSCROLL ? SB_VERT : SB_HORZ;
        SCROLLINFO info{sizeof(SCROLLINFO), SIF_ALL};
        GetScrollInfo(window_, bar, &info);
        int position = info.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: position -= kRowHeight; break;
        case SB_LINEDOWN: position += kRowHeight; break;
        case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
        case SB_TOP: position = info.nMin; break;
        case SB_BOTTOM: position = info.nMax; break;
        default: return true;
        }
        Scroll(bar, position);
        return true;
    }
    if (message == WM_MOUSEWHEEL) {
        Scroll(SB_VERT, scrollY_ - GET_WHEEL_DELTA_WPARAM(wParam) * kRowHeight * 3 / WHEEL_DELTA);
        return true;
    }
    if (message == WM_SIZE) {
        if (wParam != SIZE_MINIMIZED) {
            LayoutControls(static_cast<int>(LOWORD(lParam)), static_cast<int>(HIWORD(lParam)));
        }
        return false;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (syncing_) {
            return false;
        }
        switch (id) {
        case kEnabled:
            if (code == BN_CLICKED) {
                pendingActions_.toggleRequested = true;
                pendingActions_.desiredEnabled = SendMessageW(enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                return true;
            }
            break;
        case kStop:
            if (code == BN_CLICKED) {
                pendingActions_.stopRequested = true;
                return true;
            }
            break;
        case kMonitor:
            if (code == CBN_SELCHANGE && settings_ != nullptr) {
                const LRESULT selected = SendMessageW(monitorCombo_, CB_GETCURSEL, 0, 0);
                if (selected >= 0) {
                    settings_->monitorIndex = static_cast<std::uint32_t>(selected);
                    pendingActions_.monitorChanged = true;
                    pendingActions_.settingsChanged = true;
                }
                return true;
            }
            break;
        case kPacing:
            if (code == CBN_SELCHANGE && settings_ != nullptr) {
                const LRESULT selected = SendMessageW(pacingCombo_, CB_GETCURSEL, 0, 0);
                settings_->framePacing = selected == 1 ? core::FramePacingMode::VSync : core::FramePacingMode::Uncapped;
                pendingActions_.pacingChanged = true;
                pendingActions_.settingsChanged = true;
                return true;
            }
            break;
        case kSave:
            if (code == BN_CLICKED) {
                pendingActions_.saveRequested = true;
                return true;
            }
            break;
        case kReset:
            if (code == BN_CLICKED) {
                pendingActions_.resetRequested = true;
                pendingActions_.settingsChanged = true;
                return true;
            }
            break;
        default:
            break;
        }
    }
    if (message == WM_HSCROLL && !syncing_ && settings_ != nullptr) {
        const HWND source = reinterpret_cast<HWND>(lParam);
        int id = 0;
        for (const SliderBinding& binding : sliders_) {
            if (binding.control == source) {
                id = binding.id;
                break;
            }
        }
        if (id != 0) {
            const float value = ReadSlider(id);
            switch (id) {
            case kBrightness: settings_->effects.brightness = value; break;
            case kContrast: settings_->effects.contrast = value; break;
            case kSaturation: settings_->effects.saturation = value; break;
            case kGamma: settings_->effects.gamma = value; break;
            case kGrayscale: settings_->effects.grayscale = value; break;
            case kSepia: settings_->effects.sepia = value; break;
            case kScanline: settings_->effects.scanlineIntensity = value; break;
            case kSpacing: settings_->effects.scanlineSpacing = value; break;
            case kThickness: settings_->effects.scanlineThickness = value; break;
            case kPhosphor: settings_->effects.phosphorIntensity = value; break;
            case kPixelSize: settings_->effects.pixelSize = value; break;
            case kSharpen: settings_->effects.sharpen = value; break;
            case kChromatic: settings_->effects.chromaticAberration = value; break;
            case kBloom: settings_->effects.bloomIntensity = value; break;
            case kBloomThreshold: settings_->effects.bloomThreshold = value; break;
            case kBloomRadius: settings_->effects.bloomRadius = value; break;
            case kVignette: settings_->effects.vignetteIntensity = value; break;
            case kVignetteWidth: settings_->effects.vignetteWidth = value; break;
            case kGrain: settings_->effects.grainIntensity = value; break;
            case kGrainSize: settings_->effects.grainSize = value; break;
            case kGlobalIntensity: settings_->effects.globalIntensity = value; break;
            default: break;
            }
            pendingActions_.settingsChanged = true;
            return true;
        }
    }
    return false;
}

PanelActions SettingsPanel::TakeActions() {
    const PanelActions actions = pendingActions_;
    pendingActions_ = {};
    return actions;
}

void SettingsPanel::Render(
    core::AppSettings& settings,
    const std::vector<platform::MonitorInfo>& monitors,
    bool captureExcluded,
    std::uint64_t capturedFrames,
    std::uint64_t droppedFrames,
    std::uint64_t presentedFrames,
    std::wstring_view status) {
    if (!initialized_ || window_ == nullptr || !IsWindowVisible(window_)) {
        return;
    }
    settings_ = &settings;
    std::vector<std::wstring> labels;
    labels.reserve(monitors.size());
    for (const auto& monitor : monitors) labels.push_back(MonitorLabel(monitor));
    if (!hasSyncedSettings_ || !SettingsEqual(settings, syncedSettings_) || monitorLabels_ != labels) {
        SyncControls(settings, monitors);
        syncedSettings_ = settings;
        hasSyncedSettings_ = true;
    }
    UpdateStats(captureExcluded, capturedFrames, droppedFrames, presentedFrames, status);
}

} // namespace screenfx::ui
