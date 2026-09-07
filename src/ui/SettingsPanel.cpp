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
constexpr ULONGLONG kStatsIntervalMs = 250;

void SetPosition(HWND control, int x, int y, int width, int height) {
    if (control != nullptr) {
        SetWindowPos(control, nullptr, x, y, std::max(1, width), std::max(1, height), SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

std::wstring MonitorLabel(const platform::MonitorInfo& monitor) {
    return monitor.deviceName + L"  " +
           std::to_wstring(monitor.bounds.right - monitor.bounds.left) + L" × " +
           std::to_wstring(monitor.bounds.bottom - monitor.bounds.top) +
           (monitor.primary ? L"  (primary)" : L"");
}

bool MonitorLabelsEqual(const std::vector<platform::MonitorInfo>& left, const std::vector<platform::MonitorInfo>& right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [](const platform::MonitorInfo& a, const platform::MonitorInfo& b) {
            return a.deviceName == b.deviceName && a.primary == b.primary &&
                   a.bounds.right - a.bounds.left == b.bounds.right - b.bounds.left &&
                   a.bounds.bottom - a.bounds.top == b.bounds.bottom - b.bounds.top;
        });
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
               left.grainSize,
               left.tintRed, left.tintGreen, left.tintBlue, left.tintIntensity) ==
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
               right.grainSize,
               right.tintRed, right.tintGreen, right.tintBlue, right.tintIntensity);
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
        L"Enable filter",
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
        L"Stop immediately",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        150,
        kControlHeight,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStop)),
        module,
        nullptr);
    monitorLabel_ = CreateLabel(L"Monitor", 0, 0, 1, 1);
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
    pacingLabel_ = CreateLabel(L"Presentation mode", 0, 0, 1, 1);
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
        SendMessageW(pacingCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Uncapped"));
        SendMessageW(pacingCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"VSync"));
    }

    presetLabel_ = CreateLabel(L"Preset name", 0, 0, 1, 1);
    presetCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
        0, 0, 280, 220, window_, reinterpret_cast<HMENU>(kPreset), module, nullptr);
    SendMessageW(presetCombo_, CB_LIMITTEXT, 80, 0);
    auto button = [&](int id, const wchar_t* label) {
        return CreateWindowExW(0, WC_BUTTONW, label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 150, kControlHeight, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
    };
    applyPreset_ = button(kApplyPreset, L"Apply preset");
    savePreset_ = button(kSavePreset, L"Save preset");
    deletePreset_ = button(kDeletePreset, L"Delete preset");
    tintColor_ = button(kTintColor, L"Tint color: #FFFFFF");
    tintSwatch_ = CreateWindowExW(0, WC_STATICW, L"Tint preview", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 40, 24, window_, reinterpret_cast<HMENU>(kTintSwatch), module, nullptr);

    status_ = CreateLabel(L"", 0, 0, 1, 1);
    capturedStats_ = CreateLabel(L"", 0, 0, 1, 1);
    presentedStats_ = CreateLabel(L"", 0, 0, 1, 1);
    droppedStats_ = CreateLabel(L"", 0, 0, 1, 1);
    captureState_ = CreateLabel(L"", 0, 0, 1, 1);
    colorsHeader_ = CreateLabel(L"Colors", 0, 0, 1, 1);
    crtHeader_ = CreateLabel(L"CRT", 0, 0, 1, 1);
    imageHeader_ = CreateLabel(L"Image", 0, 0, 1, 1);
    generalHeader_ = CreateLabel(L"Intensity and tint", 0, 0, 1, 1);

    sliders_.clear();
    CreateSlider(kBrightness, L"Brightness", -1.0F, 1.0F);
    CreateSlider(kContrast, L"Contrast", 0.0F, 4.0F);
    CreateSlider(kSaturation, L"Saturation", 0.0F, 4.0F);
    CreateSlider(kGamma, L"Gamma", 0.1F, 4.0F);
    CreateSlider(kGrayscale, L"Grayscale", 0.0F, 1.0F);
    CreateSlider(kSepia, L"Sepia", 0.0F, 1.0F);
    CreateSlider(kScanline, L"Scanlines", 0.0F, 1.0F);
    CreateSlider(kSpacing, L"Scanline spacing", 1.0F, 8.0F);
    CreateSlider(kThickness, L"Scanline thickness", 0.05F, 1.0F);
    CreateSlider(kPhosphor, L"RGB phosphor mask", 0.0F, 1.0F);
    CreateSlider(kPixelSize, L"Pixelation", 1.0F, 32.0F);
    CreateSlider(kSharpen, L"Sharpness", 0.0F, 2.0F);
    CreateSlider(kChromatic, L"Chromatic aberration", 0.0F, 8.0F);
    CreateSlider(kBloom, L"Bloom", 0.0F, 2.0F);
    CreateSlider(kBloomThreshold, L"Bloom threshold", 0.0F, 1.0F);
    CreateSlider(kBloomRadius, L"Bloom radius", 0.5F, 8.0F);
    CreateSlider(kVignette, L"Vignette", 0.0F, 1.0F);
    CreateSlider(kVignetteWidth, L"Vignette width", 0.1F, 1.0F);
    CreateSlider(kGrain, L"Grain", 0.0F, 1.0F);
    CreateSlider(kGrainSize, L"Grain size", 0.25F, 8.0F);
    CreateSlider(kGlobalIntensity, L"Global intensity", 0.0F, 1.0F);
    CreateSlider(kTintIntensity, L"Tint strength", 0.0F, 1.0F);
    // Keep keyboard navigation in the same order as the visible effect controls.
    SetWindowPos(tintColor_, FindControl(kGlobalIntensity), 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    reset_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"Reset effects",
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
        L"Save settings",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        140,
        kControlHeight,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSave)),
        module,
        nullptr);

    const std::array ownedControls{enabled_, stop_, monitorCombo_, pacingCombo_, reset_, save_,
        presetCombo_, applyPreset_, savePreset_, deletePreset_, tintColor_, tintSwatch_};
    for (HWND control : ownedControls) {
        TrackControl(control);
        SetControlFont(control);
    }

    const std::array<HWND, 8> controls{
        enabled_, stop_, monitorCombo_, pacingCombo_, reset_, save_, status_, captureState_};
    for (HWND control : controls) {
        SetControlFont(control);
    }
    return enabled_ != nullptr && stop_ != nullptr && monitorCombo_ != nullptr && pacingCombo_ != nullptr &&
           std::all_of(sliders_.begin(), sliders_.end(), [](const SliderBinding& binding) {
               return binding.control != nullptr && binding.labelControl != nullptr;
           }) && std::all_of(ownedControls.begin(), ownedControls.end(), [](HWND control) { return control != nullptr; });
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
    syncedMonitors_.clear();
    if (font_ != nullptr) DeleteObject(font_);
    font_ = nullptr;
    scrollX_ = 0;
    scrollY_ = 0;
    lastCapturedFrames_ = 0;
    lastDroppedFrames_ = 0;
    lastPresentedFrames_ = 0;
    lastStatus_.clear();
    lastStatsUpdate_ = 0;
    statsInitialized_ = false;
    hasSyncedSettings_ = false;
    wasVisible_ = false;
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
    presetLabel_ = presetCombo_ = applyPreset_ = savePreset_ = deletePreset_ = tintColor_ = tintSwatch_ = nullptr;
}

std::wstring SettingsPanel::PresetName() const {
    const int length = GetWindowTextLengthW(presetCombo_);
    std::wstring name(static_cast<std::size_t>(length) + 1, L'\0');
    name.resize(GetWindowTextW(presetCombo_, name.data(), length + 1));
    return name;
}

void SettingsPanel::SetPresetNames(const std::vector<std::wstring>& names) {
    if (!presetCombo_) return;
    const auto currentName = PresetName();
    const bool wasSyncing = syncing_;
    syncing_ = true;
    SendMessageW(presetCombo_, CB_RESETCONTENT, 0, 0);
    for (const auto& name : names) SendMessageW(presetCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SetWindowTextW(presetCombo_, currentName.c_str());
    syncing_ = wasSyncing;
}

void SettingsPanel::ClearPresetName() {
    if (!presetCombo_) return;
    const bool wasSyncing = syncing_;
    syncing_ = true;
    SendMessageW(presetCombo_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SetWindowTextW(presetCombo_, L"");
    syncing_ = wasSyncing;
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

void SettingsPanel::SyncControls(const core::AppSettings& settings, const std::vector<platform::MonitorInfo>& monitors, bool monitorsChanged) {
    if (syncing_) {
        return;
    }
    syncing_ = true;
    if (enabled_ != nullptr && (!hasSyncedSettings_ || settings.enabled != syncedSettings_.enabled)) {
        SendMessageW(enabled_, BM_SETCHECK, settings.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (pacingCombo_ != nullptr && (!hasSyncedSettings_ || settings.framePacing != syncedSettings_.framePacing)) {
        SendMessageW(pacingCombo_, CB_SETCURSEL, settings.framePacing == core::FramePacingMode::VSync ? 1 : 0, 0);
    }
    if (monitorCombo_ != nullptr && monitorsChanged) {
        SendMessageW(monitorCombo_, CB_RESETCONTENT, 0, 0);
        for (const platform::MonitorInfo& monitor : monitors) {
            const std::wstring label = MonitorLabel(monitor);
            SendMessageW(monitorCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        syncedMonitors_ = monitors;
    }
    if (monitorCombo_ != nullptr && (monitorsChanged || !hasSyncedSettings_ || settings.monitorIndex != syncedSettings_.monitorIndex)) {
        if (monitors.empty()) {
            SendMessageW(monitorCombo_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        } else {
            const auto selected = std::min<std::size_t>(settings.monitorIndex, monitors.size() - 1);
            SendMessageW(monitorCombo_, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        }
    }

    const std::array<std::pair<int, float>, 22> values{
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
        std::pair{kTintIntensity, settings.effects.tintIntensity},
    };
    for (const auto& [id, value] : values) {
        for (SliderBinding& binding : sliders_) {
            if (binding.id == id && binding.control != nullptr) {
                const int position = ToTrackbar(value, binding.minimum, binding.maximum);
                if (binding.position != position) {
                    SendMessageW(binding.control, TBM_SETPOS, TRUE, position);
                    binding.position = position;
                }
                break;
            }
        }
    }
    const auto channel = [](float value) { return static_cast<BYTE>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F); };
    const COLORREF tint = RGB(channel(settings.effects.tintRed), channel(settings.effects.tintGreen), channel(settings.effects.tintBlue));
    if (!hasSyncedSettings_ || tintColorValue_ != tint) {
        tintColorValue_ = tint;
        wchar_t label[48]{};
        swprintf_s(label, L"Tint color: #%02X%02X%02X", GetRValue(tint), GetGValue(tint), GetBValue(tint));
        SetWindowTextW(tintColor_, label);
        InvalidateRect(tintSwatch_, nullptr, TRUE);
    }
    syncing_ = false;
}

void SettingsPanel::UpdateStats(
    bool captureExcluded,
    std::uint64_t capturedFrames,
    std::uint64_t droppedFrames,
    std::uint64_t presentedFrames,
    std::wstring_view status,
    ULONGLONG now) {
    // Errors and capture state must remain immediate even between counter refreshes.
    if ((!statsInitialized_ || status != lastStatus_) && status_ != nullptr) {
        lastStatus_ = status;
        SetWindowTextW(status_, lastStatus_.c_str());
    }
    if ((!statsInitialized_ || captureExcluded != lastCaptureExcluded_) && captureState_ != nullptr) {
        lastCaptureExcluded_ = captureExcluded;
        SetWindowTextW(captureState_, captureExcluded ? L"Capture exclusion: active" : L"Capture exclusion: unavailable");
    }
    if (statsInitialized_ && (now - lastStatsUpdate_ < kStatsIntervalMs ||
        (capturedFrames == lastCapturedFrames_ && droppedFrames == lastDroppedFrames_ && presentedFrames == lastPresentedFrames_))) {
        return;
    }
    if ((!statsInitialized_ || capturedFrames != lastCapturedFrames_) && capturedStats_ != nullptr) {
        SetWindowTextW(capturedStats_, (L"Captured frames: " + std::to_wstring(capturedFrames)).c_str());
    }
    if ((!statsInitialized_ || presentedFrames != lastPresentedFrames_) && presentedStats_ != nullptr) {
        SetWindowTextW(presentedStats_, (L"Presented frames: " + std::to_wstring(presentedFrames)).c_str());
    }
    if ((!statsInitialized_ || droppedFrames != lastDroppedFrames_) && droppedStats_ != nullptr) {
        SetWindowTextW(droppedStats_, (L"Dropped frames: " + std::to_wstring(droppedFrames)).c_str());
    }
    lastCapturedFrames_ = capturedFrames;
    lastDroppedFrames_ = droppedFrames;
    lastPresentedFrames_ = presentedFrames;
    lastStatsUpdate_ = now;
    statsInitialized_ = true;
}

void SettingsPanel::LayoutControls(int width, int height) {
    if (!initialized_ || window_ == nullptr) {
        return;
    }
    const int logicalWidth = MulDiv(width, 96, dpi_);
    const int logicalHeight = MulDiv(height, 96, dpi_);
    width = std::max(900, logicalWidth);
    height = std::max(830, logicalHeight);
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
    place(presetLabel_, kMargin, 92, 90, kControlHeight);
    place(presetCombo_, kMargin + 92, 89, comboWidth, 220);
    const int presetButtonWidth = (columnWidth - 2 * 8) / 3;
    place(applyPreset_, rightX, 88, presetButtonWidth, 28);
    place(savePreset_, rightX + presetButtonWidth + 8, 88, presetButtonWidth, 28);
    place(deletePreset_, rightX + 2 * (presetButtonWidth + 8), 88, presetButtonWidth, 28);
    place(status_, kMargin, 128, usableWidth, 42);

    place(colorsHeader_, kMargin, 174, columnWidth, kHeaderHeight);
    place(crtHeader_, kMargin, 174 + kHeaderHeight + 6 * kRowHeight + 8, columnWidth, kHeaderHeight);
    place(imageHeader_, rightX, 174, columnWidth, kHeaderHeight);
    place(generalHeader_, rightX, 174 + kHeaderHeight + 9 * kRowHeight + 8, columnWidth, kHeaderHeight);

    auto placeColumn = [&](int firstId, int count, int x, int startY) {
        int row = 0;
        for (SliderBinding& binding : sliders_) {
            if ((firstId == kBrightness && binding.id > kSepia) ||
                (firstId == kScanline && (binding.id < kScanline || binding.id > kPixelSize)) ||
                (firstId == kSharpen && (binding.id < kSharpen || binding.id > kGrainSize)) ||
                (firstId == kGlobalIntensity && binding.id != kGlobalIntensity) ||
                (firstId == kTintIntensity && binding.id != kTintIntensity)) {
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
    placeColumn(kBrightness, 6, kMargin, 198);
    placeColumn(kScanline, 5, kMargin, 198 + kHeaderHeight + 6 * kRowHeight + 8);
    placeColumn(kSharpen, 9, rightX, 198);
    placeColumn(kGlobalIntensity, 1, rightX, 198 + kHeaderHeight + 9 * kRowHeight + 8);
    place(tintSwatch_, rightX, 622, 36, 28);
    place(tintColor_, rightX + 48, 622, columnWidth - 48, 28);
    placeColumn(kTintIntensity, 1, rightX, 662);

    const int bottomY = std::max(734, height - 96);
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
    if (message == WM_SHOWWINDOW && wParam == FALSE) {
        wasVisible_ = false;
    }
    if (message == WM_DRAWITEM && wParam == kTintSwatch) {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (draw != nullptr) {
            HBRUSH brush = CreateSolidBrush(tintColorValue_);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            FrameRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_WINDOWFRAME));
        }
        return true;
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
        if (wParam == SIZE_MINIMIZED) wasVisible_ = false;
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
        case kSavePreset:
        case kApplyPreset:
        case kDeletePreset:
            if (code == BN_CLICKED) {
                pendingActions_.presetName = PresetName();
                pendingActions_.savePresetRequested = id == kSavePreset;
                pendingActions_.applyPresetRequested = id == kApplyPreset;
                pendingActions_.deletePresetRequested = id == kDeletePreset;
                return true;
            }
            break;
        case kTintColor:
            if (code == BN_CLICKED) {
                pendingActions_.pickColorRequested = true;
                return true;
            }
            break;
        case kEnabled:
            if (code == BN_CLICKED) {
                pendingActions_.toggleRequested = true;
                pendingActions_.desiredEnabled = SendMessageW(enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                syncedSettings_.enabled = pendingActions_.desiredEnabled;
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
                    syncedSettings_.monitorIndex = settings_->monitorIndex;
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
                syncedSettings_.framePacing = settings_->framePacing;
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
        float value = 0.0F;
        for (SliderBinding& binding : sliders_) {
            if (binding.control == source) {
                id = binding.id;
                binding.position = static_cast<int>(SendMessageW(source, TBM_GETPOS, 0, 0));
                value = FromTrackbar(binding.position, binding.minimum, binding.maximum);
                break;
            }
        }
        if (id != 0) {
            // The snapshot describes the controls, including edits that happen before
            // the next Render. An immediate reset must compare against the new value.
            const auto setValue = [&](float core::EffectSettings::* member) {
                settings_->effects.*member = value;
                syncedSettings_.effects.*member = value;
            };
            switch (id) {
            case kBrightness: setValue(&core::EffectSettings::brightness); break;
            case kContrast: setValue(&core::EffectSettings::contrast); break;
            case kSaturation: setValue(&core::EffectSettings::saturation); break;
            case kGamma: setValue(&core::EffectSettings::gamma); break;
            case kGrayscale: setValue(&core::EffectSettings::grayscale); break;
            case kSepia: setValue(&core::EffectSettings::sepia); break;
            case kScanline: setValue(&core::EffectSettings::scanlineIntensity); break;
            case kSpacing: setValue(&core::EffectSettings::scanlineSpacing); break;
            case kThickness: setValue(&core::EffectSettings::scanlineThickness); break;
            case kPhosphor: setValue(&core::EffectSettings::phosphorIntensity); break;
            case kPixelSize: setValue(&core::EffectSettings::pixelSize); break;
            case kSharpen: setValue(&core::EffectSettings::sharpen); break;
            case kChromatic: setValue(&core::EffectSettings::chromaticAberration); break;
            case kBloom: setValue(&core::EffectSettings::bloomIntensity); break;
            case kBloomThreshold: setValue(&core::EffectSettings::bloomThreshold); break;
            case kBloomRadius: setValue(&core::EffectSettings::bloomRadius); break;
            case kVignette: setValue(&core::EffectSettings::vignetteIntensity); break;
            case kVignetteWidth: setValue(&core::EffectSettings::vignetteWidth); break;
            case kGrain: setValue(&core::EffectSettings::grainIntensity); break;
            case kGrainSize: setValue(&core::EffectSettings::grainSize); break;
            case kGlobalIntensity: setValue(&core::EffectSettings::globalIntensity); break;
            case kTintIntensity: setValue(&core::EffectSettings::tintIntensity); break;
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
    if (!initialized_ || window_ == nullptr || !IsWindowVisible(window_) || IsIconic(window_)) {
        wasVisible_ = false;
        return;
    }
    if (!wasVisible_) statsInitialized_ = false;
    wasVisible_ = true;
    settings_ = &settings;
    const bool monitorsChanged = !MonitorLabelsEqual(monitors, syncedMonitors_);
    if (!hasSyncedSettings_ || !SettingsEqual(settings, syncedSettings_) || monitorsChanged) {
        SyncControls(settings, monitors, monitorsChanged);
        syncedSettings_ = settings;
        hasSyncedSettings_ = true;
    }
    UpdateStats(captureExcluded, capturedFrames, droppedFrames, presentedFrames, status, GetTickCount64());
}

} // namespace screenfx::ui
