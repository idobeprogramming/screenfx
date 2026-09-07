#pragma once

#include <windows.h>
#include <shellapi.h>

#include "../core/Settings.h"
#include "../core/SettingsStore.h"
#include "../graphics/CaptureSession.h"
#include "../graphics/D3D11Context.h"
#include "../graphics/OverlayWindow.h"
#include "../graphics/RenderEngine.h"
#include "../ui/SettingsPanel.h"
#include "Monitors.h"

#include <cstdint>
#include <string>
#include <vector>

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
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

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
    bool InitializeGraphics();
    void ShutdownGraphics();
    void RefreshMonitors();
    bool StartCaptureForSelectedMonitor();
    void SetEnabled(bool enabled);
    void RenderAvailableFrame();
    void EnsurePanel();
    void RenderPanel();
    void ProcessPanelActions();
    void RestartCaptureIfRunning();
    void FailCapture(const std::wstring& detail);
    void KeepPanelAboveOverlay();
    bool SaveSettings(bool announce = false);
    void RefreshPresetNames();
    void SavePreset(std::wstring name);
    void ApplyPreset(std::wstring name);
    void DeletePreset(const std::wstring& name);
    void PickTintColor();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND statusLabel_ = nullptr;
    bool enabled_ = false;
    UINT registeredHotkeys_ = 0;
    bool graphicsInitialized_ = false;
    bool shuttingDown_ = false;
    std::wstring statusText_;
    core::AppSettings settings_{};
    std::vector<core::Preset> presets_;
    COLORREF customColors_[16]{};
    std::vector<MonitorInfo> monitors_;
    graphics::D3D11Context graphics_;
    graphics::OverlayWindow overlay_;
    graphics::CaptureSession capture_;
    graphics::RenderEngine renderer_;
    ui::SettingsPanel panel_;
    graphics::CapturedFrame latestFrame_;
    bool haveFrame_ = false;
    bool renderRequested_ = false;
    std::uint64_t lastRenderedSequence_ = 0;
    ULONGLONG firstFrameDeadline_ = 0;
    NOTIFYICONDATAW trayIcon_{};
    bool trayIconAdded_ = false;
    UINT taskbarCreatedMessage_ = 0;
};

} // namespace screenfx::platform
