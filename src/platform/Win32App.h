#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace screenfx::platform {

class Win32App {
public:
    Win32App(HINSTANCE instance, int showCommand);
    ~Win32App();

    Win32App(const Win32App&) = delete;
    Win32App& operator=(const Win32App&) = delete;

    int Run();

private:
    static constexpr UINT kTrayMessage = WM_APP + 1;
    static constexpr UINT kHotkeyToggle = 1;
    static constexpr UINT kHotkeyPanel = 2;
    static constexpr UINT kHotkeyStop = 3;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateControlWindow(int showCommand);
    bool RegisterHotkeys();
    void UnregisterHotkeys();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowPanel();
    void HidePanel();
    void UpdateStatus(const std::wstring& text);
    void OnToggleRequested();
    void OnStopRequested();
    void ShowTrayMenu(POINT screenPoint);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND statusLabel_ = nullptr;
    bool enabled_ = false;
    bool hotkeysRegistered_ = false;
    NOTIFYICONDATAW trayIcon_{};
};

} // namespace screenfx::platform
