#pragma once

#include "../core/Settings.h"
#include "../platform/Monitors.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace screenfx::ui {

struct PanelActions {
    bool settingsChanged = false;
    bool toggleRequested = false;
    bool desiredEnabled = false;
    bool stopRequested = false;
    bool saveRequested = false;
    bool resetRequested = false;
    bool monitorChanged = false;
    bool pacingChanged = false;
    bool savePresetRequested = false;
    bool applyPresetRequested = false;
    bool deletePresetRequested = false;
    bool pickColorRequested = false;
    std::wstring presetName;
};

class SettingsPanel {
public:
    SettingsPanel() = default;
    ~SettingsPanel();

    SettingsPanel(const SettingsPanel&) = delete;
    SettingsPanel& operator=(const SettingsPanel&) = delete;

    bool Initialize(HWND window);
    void Shutdown();
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    PanelActions TakeActions();
    void EnsureFocusVisible();
    void SetPresetNames(const std::vector<std::wstring>& names);
    void ClearPresetName();
    void Render(
        core::AppSettings& settings,
        const std::vector<platform::MonitorInfo>& monitors,
        bool captureExcluded,
        std::uint64_t capturedFrames,
        std::uint64_t droppedFrames,
        std::uint64_t presentedFrames,
        std::wstring_view status);
    bool IsInitialized() const noexcept { return initialized_; }

private:
    enum ControlId : int {
        kEnabled = 1001,
        kStop = 1002,
        kMonitor = 1003,
        kPacing = 1004,
        kSave = 1005,
        kReset = 1006,
        kPreset = 1007,
        kApplyPreset = 1008,
        kSavePreset = 1009,
        kTintColor = 1010,
        kTintSwatch = 1011,
        kDeletePreset = 1012,
        kBrightness = 1101,
        kContrast = 1102,
        kSaturation = 1103,
        kGamma = 1104,
        kGrayscale = 1105,
        kSepia = 1106,
        kScanline = 1201,
        kSpacing = 1202,
        kThickness = 1203,
        kPhosphor = 1204,
        kPixelSize = 1205,
        kSharpen = 1301,
        kChromatic = 1302,
        kBloom = 1303,
        kBloomThreshold = 1304,
        kBloomRadius = 1305,
        kVignette = 1306,
        kVignetteWidth = 1307,
        kGrain = 1308,
        kGrainSize = 1309,
        kGlobalIntensity = 1401,
        kTintIntensity = 1402,
    };

    struct SliderBinding {
        int id = 0;
        HWND control = nullptr;
        HWND labelControl = nullptr;
        float minimum = 0.0F;
        float maximum = 1.0F;
        const wchar_t* label = nullptr;
        int position = -1;
    };

    bool CreateControls();
    void TrackControl(HWND control);
    HWND CreateLabel(const wchar_t* text, int x, int y, int width, int height);
    HWND CreateSlider(int id, const wchar_t* label, float minimum, float maximum);
    void LayoutControls(int width, int height);
    void SyncControls(const core::AppSettings& settings, const std::vector<platform::MonitorInfo>& monitors, bool monitorsChanged);
    void UpdateStats(
        bool captureExcluded,
        std::uint64_t capturedFrames,
        std::uint64_t droppedFrames,
        std::uint64_t presentedFrames,
        std::wstring_view status,
        ULONGLONG now);
    friend struct SettingsPanelTestAccess;
    HWND FindControl(int id) const;
    static int ToTrackbar(float value, float minimum, float maximum);
    static float FromTrackbar(int position, float minimum, float maximum);
    void SetControlFont(HWND control);
    void UpdateFont();
    void Scroll(int bar, int position);
    std::wstring PresetName() const;

    HWND window_ = nullptr;
    HWND enabled_ = nullptr;
    HWND stop_ = nullptr;
    HWND monitorCombo_ = nullptr;
    HWND pacingCombo_ = nullptr;
    HWND monitorLabel_ = nullptr;
    HWND pacingLabel_ = nullptr;
    HWND colorsHeader_ = nullptr;
    HWND crtHeader_ = nullptr;
    HWND imageHeader_ = nullptr;
    HWND generalHeader_ = nullptr;
    HWND status_ = nullptr;
    HWND capturedStats_ = nullptr;
    HWND presentedStats_ = nullptr;
    HWND droppedStats_ = nullptr;
    HWND captureState_ = nullptr;
    HWND save_ = nullptr;
    HWND reset_ = nullptr;
    HWND presetLabel_ = nullptr;
    HWND presetCombo_ = nullptr;
    HWND applyPreset_ = nullptr;
    HWND savePreset_ = nullptr;
    HWND deletePreset_ = nullptr;
    HWND tintColor_ = nullptr;
    HWND tintSwatch_ = nullptr;
    COLORREF tintColorValue_ = RGB(255, 255, 255);
    std::vector<HWND> controls_;
    std::vector<SliderBinding> sliders_;
    core::AppSettings* settings_ = nullptr;
    core::AppSettings syncedSettings_{};
    PanelActions pendingActions_{};
    std::vector<platform::MonitorInfo> syncedMonitors_;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    int scrollX_ = 0;
    int scrollY_ = 0;
    std::uint64_t lastCapturedFrames_ = 0;
    std::uint64_t lastDroppedFrames_ = 0;
    std::uint64_t lastPresentedFrames_ = 0;
    std::wstring lastStatus_;
    ULONGLONG lastStatsUpdate_ = 0;
    bool statsInitialized_ = false;
    bool lastCaptureExcluded_ = false;
    bool hasSyncedSettings_ = false;
    bool initialized_ = false;
    bool syncing_ = false;
    bool wasVisible_ = false;
};

} // namespace screenfx::ui
