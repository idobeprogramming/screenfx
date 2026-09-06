#include "ui/SettingsPanel.h"
#include <commctrl.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace screenfx::ui {
struct SettingsPanelTestAccess {
    static std::array<HWND, 5> Stats(const SettingsPanel& panel) {
        return {panel.capturedStats_, panel.presentedStats_, panel.droppedStats_, panel.status_, panel.captureState_};
    }
    static void UpdateStats(SettingsPanel& panel, ULONGLONG now, bool excluded,
                            std::uint64_t captured, std::uint64_t dropped, std::uint64_t presented,
                            std::wstring_view status) {
        panel.UpdateStats(excluded, captured, dropped, presented, status, now);
    }
};
}

using namespace screenfx;
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Window {
    HWND handle = CreateWindowExW(0, L"STATIC", L"ScreenFX panel test", WS_POPUP,
        -32000, -32000, 1000, 940, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (handle) DestroyWindow(handle); }
};
std::wstring Text(HWND window) {
    wchar_t value[256]{}; GetWindowTextW(window, value, 256); return value;
}

struct MessageProbe {
    HWND window;
    UINT message;
    unsigned count = 0;
    MessageProbe(HWND target, UINT observed) : window(target), message(observed) {
        Check(SetWindowSubclass(window, Observe, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Install control message probe");
    }
    ~MessageProbe() { RemoveWindowSubclass(window, Observe, 1); }
    MessageProbe(const MessageProbe&) = delete;
    MessageProbe& operator=(const MessageProbe&) = delete;
    static LRESULT CALLBACK Observe(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto& probe = *reinterpret_cast<MessageProbe*>(data);
        if (message == probe.message) ++probe.count;
        return DefSubclassProc(window, message, wParam, lParam);
    }
};

struct PanelVisibilityEvents {
    HWND window;
    PanelVisibilityEvents(HWND target, ui::SettingsPanel& panel) : window(target) {
        Check(SetWindowSubclass(window, Forward, 1, reinterpret_cast<DWORD_PTR>(&panel)) != FALSE, "Install panel visibility forwarding");
    }
    ~PanelVisibilityEvents() { RemoveWindowSubclass(window, Forward, 1); }
    static LRESULT CALLBACK Forward(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        if (message == WM_SHOWWINDOW || message == WM_SIZE) {
            reinterpret_cast<ui::SettingsPanel*>(data)->HandleMessage(message, wParam, lParam);
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
};

void TestStatistics() {
    Window window;
    ui::SettingsPanel panel;
    Check(panel.Initialize(window.handle), "Statistics panel initialization");
    const auto controls = ui::SettingsPanelTestAccess::Stats(panel);
    MessageProbe captured(controls[0], WM_SETTEXT), presented(controls[1], WM_SETTEXT), dropped(controls[2], WM_SETTEXT);
    MessageProbe status(controls[3], WM_SETTEXT), exclusion(controls[4], WM_SETTEXT);
    auto update = [&](ULONGLONG now, bool excluded, std::uint64_t cap, std::uint64_t drop,
                      std::uint64_t present, std::wstring_view text) {
        ui::SettingsPanelTestAccess::UpdateStats(panel, now, excluded, cap, drop, present, text);
    };
    update(1000, true, 10, 1, 9, L"Ready");
    Check(captured.count == 1 && presented.count == 1 && dropped.count == 1, "Initial counters render once");
    for (ULONGLONG offset = 1; offset < 250; ++offset) update(1000 + offset, true, 10 + offset, 1, 9 + offset, L"Ready");
    Check(captured.count == 1 && presented.count == 1 && dropped.count == 1, "Counter changes must not repaint before 250 ms");
    Check(status.count == 1 && exclusion.count == 1, "Counter changes must not repaint status or exclusion");
    Check(Text(controls[0]) == L"Captured frames: 10", "Throttled counter caption remains stable");
    update(1250, true, 300, 2, 299, L"Ready");
    Check(captured.count == 2 && presented.count == 2 && dropped.count == 2, "Counters refresh at the 250 ms boundary");
    Check(Text(controls[0]) == L"Captured frames: 300", "Counter refresh uses latest value");
    update(1251, false, 400, 3, 399, L"Capture failed");
    Check(status.count == 2 && Text(controls[3]) == L"Capture failed", "Errors display immediately between counter refreshes");
    Check(exclusion.count == 2 && Text(controls[4]) == L"Capture exclusion: unavailable", "Exclusion state displays immediately");
    Check(captured.count == 2 && presented.count == 2 && dropped.count == 2, "Status changes do not bypass counter throttling");
    update(1500, false, 300, 2, 333, L"Capture failed");
    Check(captured.count == 2 && dropped.count == 2 && presented.count == 3, "Only changed counters repaint");
    update(2000, false, 300, 2, 333, L"Capture failed");
    Check(captured.count == 2 && presented.count == 3 && dropped.count == 2 && status.count == 2 && exclusion.count == 2,
          "Unchanged statistics must not repaint after the interval");
}

void TestControlCaching(ui::SettingsPanel& panel, HWND window, core::AppSettings& settings) {
    const HWND strength = GetDlgItem(window, 1402);
    const HWND enabled = GetDlgItem(window, 1001);
    const HWND pacing = GetDlgItem(window, 1004);
    const HWND monitor = GetDlgItem(window, 1003);
    MessageProbe sliderUpdates(strength, TBM_SETPOS), tintUpdates(GetDlgItem(window, 1010), WM_SETTEXT);
    MessageProbe monitorRebuilds(monitor, CB_RESETCONTENT);
    std::vector<platform::MonitorInfo> monitors(2);
    monitors[0].deviceName = L"Display A";
    monitors[0].bounds = {0, 0, 1920, 1080};
    monitors[0].primary = true;
    monitors[1].deviceName = L"Display B";
    monitors[1].bounds = {1920, 0, 3840, 1080};
    panel.Render(settings, monitors, true, 0, 0, 0, L"Ready");
    Check(monitorRebuilds.count == 1, "Monitor list populated once");
    for (unsigned frame = 1; frame <= 100; ++frame) panel.Render(settings, monitors, true, frame, 0, frame, L"Ready");
    Check(sliderUpdates.count == 0 && tintUpdates.count == 0 && monitorRebuilds.count == 1,
          "Changing frame counts must not resynchronize effect controls or monitor labels");
    const float previousStrength = settings.effects.tintIntensity;
    const auto previousPosition = SendMessageW(strength, TBM_GETPOS, 0, 0);
    SendMessageW(strength, TBM_SETPOS, TRUE, 123);
    panel.HandleMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 0), reinterpret_cast<LPARAM>(strength));
    settings.effects.tintIntensity = previousStrength;
    panel.Render(settings, monitors, true, 0, 0, 0, L"Ready");
    Check(SendMessageW(strength, TBM_GETPOS, 0, 0) == previousPosition,
          "Reset before the next render restores a user-moved slider");
    const auto previousPacing = settings.framePacing;
    const auto previousSelection = SendMessageW(pacing, CB_GETCURSEL, 0, 0);
    SendMessageW(pacing, CB_SETCURSEL, previousSelection == 0 ? 1 : 0, 0);
    panel.HandleMessage(WM_COMMAND, MAKEWPARAM(1004, CBN_SELCHANGE), reinterpret_cast<LPARAM>(pacing));
    settings.framePacing = previousPacing;
    panel.Render(settings, monitors, true, 0, 0, 0, L"Ready");
    Check(SendMessageW(pacing, CB_GETCURSEL, 0, 0) == previousSelection, "Rejected pacing change restores the control immediately");
    const auto previousMonitor = settings.monitorIndex;
    SendMessageW(monitor, CB_SETCURSEL, 1, 0);
    panel.HandleMessage(WM_COMMAND, MAKEWPARAM(1003, CBN_SELCHANGE), reinterpret_cast<LPARAM>(monitor));
    settings.monitorIndex = previousMonitor;
    panel.Render(settings, monitors, true, 0, 0, 0, L"Ready");
    Check(SendMessageW(monitor, CB_GETCURSEL, 0, 0) == static_cast<LRESULT>(previousMonitor), "Rejected monitor change restores the control immediately");
    SendMessageW(enabled, BM_SETCHECK, settings.enabled ? BST_UNCHECKED : BST_CHECKED, 0);
    panel.HandleMessage(WM_COMMAND, MAKEWPARAM(1001, BN_CLICKED), reinterpret_cast<LPARAM>(enabled));
    panel.Render(settings, monitors, true, 0, 0, 0, L"Ready");
    Check(SendMessageW(enabled, BM_GETCHECK, 0, 0) == (settings.enabled ? BST_CHECKED : BST_UNCHECKED),
          "Rejected filter toggle restores the checkbox immediately");
    monitors[0].bounds.right = 2560;
    panel.Render(settings, monitors, true, 0, 0, 0, L"Ready");
    Check(monitorRebuilds.count == 2, "Resolution change refreshes monitor labels");
    const auto actions = panel.TakeActions();
    Check(!actions.savePresetRequested && !actions.saveRequested, "Synchronizing changed controls must not save settings or presets");
}

void TestVisibility(ui::SettingsPanel& panel, HWND window, core::AppSettings& settings) {
    PanelVisibilityEvents forwarding(window, panel);
    const auto controls = ui::SettingsPanelTestAccess::Stats(panel);
    panel.Render(settings, {}, true, 0, 0, 0, L"Before hiding");
    MessageProbe captured(controls[0], WM_SETTEXT), status(controls[3], WM_SETTEXT);
    ShowWindow(window, SW_HIDE);
    panel.Render(settings, {}, false, 800, 0, 799, L"Hidden status");
    Check(captured.count == 0 && status.count == 0, "Hidden panel performs no statistics writes");
    ShowWindow(window, SW_SHOWNOACTIVATE);
    panel.Render(settings, {}, false, 900, 0, 899, L"Shown status");
    Check(Text(controls[0]) == L"Captured frames: 900" && Text(controls[3]) == L"Shown status", "Showing the panel refreshes current statistics");
    // The application skips Render entirely while hidden; window messages must still invalidate the cache.
    ShowWindow(window, SW_HIDE);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    panel.Render(settings, {}, true, 1000, 0, 999, L"Shown again");
    Check(Text(controls[0]) == L"Captured frames: 1000", "Hide/show refreshes even without a hidden Render call");
    ShowWindow(window, SW_MINIMIZE);
    Check(IsIconic(window) != FALSE, "Panel minimized for idle-work test");
    const auto capturedBefore = captured.count, statusBefore = status.count;
    panel.Render(settings, {}, false, 1100, 0, 1099, L"Minimized status");
    Check(captured.count == capturedBefore && status.count == statusBefore, "Minimized panel performs no statistics writes");
    ShowWindow(window, SW_SHOWNOACTIVATE);
    panel.Render(settings, {}, true, 1200, 0, 1199, L"Restored status");
    Check(Text(controls[0]) == L"Captured frames: 1200" && Text(controls[3]) == L"Restored status", "Restoring the panel refreshes current statistics");
}
}
int main() {
    try {
        TestStatistics();
        Window window;
        Check(window.handle != nullptr, "Test window creation");
        ui::SettingsPanel panel;
        Check(panel.Initialize(window.handle), "Panel controls initialization");
        ShowWindow(window.handle, SW_SHOWNOACTIVATE);
        core::AppSettings settings;
        panel.Render(settings, {}, false, 0, 0, 0, L"Ready");
        panel.SetPresetNames({L"Amber", L"Green CRT"});
        const HWND combo = GetDlgItem(window.handle, 1007);
        Check(combo && SendMessageW(combo, CB_GETCOUNT, 0, 0) == 2, "Preset list");
        SetWindowTextW(combo, L"My custom preset");
        panel.HandleMessage(WM_COMMAND, MAKEWPARAM(1007, CBN_EDITCHANGE), reinterpret_cast<LPARAM>(combo));
        auto actions = panel.TakeActions();
        Check(!actions.savePresetRequested && !actions.applyPresetRequested, "Typing must not save or apply a preset");
        panel.SetPresetNames({L"Amber", L"Green CRT", L"New entry"});
        Check(Text(combo) == L"My custom preset", "Refreshing list must preserve typed name");
        panel.HandleMessage(WM_COMMAND, MAKEWPARAM(1009, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(window.handle, 1009)));
        actions = panel.TakeActions();
        Check(actions.savePresetRequested && !actions.applyPresetRequested && actions.presetName == L"My custom preset",
              "Save preset must explicitly submit chosen name");
        Check(!panel.TakeActions().savePresetRequested, "Save action must be consumed only once");
        SendMessageW(combo, CB_SETCURSEL, 1, 0);
        panel.HandleMessage(WM_COMMAND, MAKEWPARAM(1007, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
        actions = panel.TakeActions();
        Check(!actions.savePresetRequested && !actions.applyPresetRequested, "Selection alone must not save or apply");
        panel.HandleMessage(WM_COMMAND, MAKEWPARAM(1008, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(window.handle, 1008)));
        actions = panel.TakeActions();
        Check(actions.applyPresetRequested && !actions.savePresetRequested && actions.presetName == L"Green CRT", "Apply chosen preset");
        panel.HandleMessage(WM_COMMAND, MAKEWPARAM(1010, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(window.handle, 1010)));
        Check(panel.TakeActions().pickColorRequested, "Color picker request");
        const HWND strength = GetDlgItem(window.handle, 1402);
        Check(strength != nullptr, "Tint strength slider exists");
        SendMessageW(strength, TBM_SETPOS, TRUE, 375);
        panel.HandleMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 0), reinterpret_cast<LPARAM>(strength));
        actions = panel.TakeActions();
        Check(actions.settingsChanged && !actions.savePresetRequested && settings.effects.tintIntensity == 0.375F,
              "Strength change updates effects without saving a preset");
        settings.effects.tintRed = 1; settings.effects.tintGreen = 0.5F; settings.effects.tintBlue = 0;
        settings.effects.tintIntensity = 0.8F;
        panel.Render(settings, {}, false, 0, 0, 0, L"Preset applied");
        Check(Text(GetDlgItem(window.handle, 1010)) == L"Tint color: #FF8000", "Applied tint refreshes picker hex label");
        Check(SendMessageW(strength, TBM_GETPOS, 0, 0) == 800, "Applied preset refreshes tint strength");
        Check(Text(GetDlgItem(window.handle, 1009)) == L"Save preset", "English preset action label");
        TestControlCaching(panel, window.handle, settings);
        TestVisibility(panel, window.handle, settings);
        std::cout << "PASS: explicit preset actions, tint/control caching, throttled statistics, immediate errors, hidden/minimized refresh\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
