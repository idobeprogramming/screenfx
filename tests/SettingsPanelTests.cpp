#include "ui/SettingsPanel.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

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
}
int main() {
    try {
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
        std::cout << "PASS: explicit preset actions, name preservation, tint UI synchronization\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
