#include "Win32App.h"

#include <shellapi.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cwchar>

namespace screenfx::platform {
namespace {

constexpr wchar_t kWindowClass[] = L"ScreenFX.ControlWindow";
constexpr wchar_t kWindowTitle[] = L"ScreenFX";
constexpr wchar_t kPanelTitle[] = L"ScreenFX — Settings";
constexpr ULONGLONG kFirstFrameTimeoutMs = 5000;

int MakeControlId(int base) {
    return base;
}

void ShowStartupError(const wchar_t* operation, DWORD error) {
    const std::wstring message = std::wstring(L"ScreenFX cannot start.\n\n") + operation +
                                 L"\nWindows error code: " + std::to_wstring(error);
    MessageBoxW(nullptr, message.c_str(), L"ScreenFX", MB_ICONERROR | MB_OK);
}

std::wstring RenderError(HRESULT result) {
    wchar_t code[32]{};
    swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(result));
    return L"GPU rendering stopped (" + std::wstring(code) + L").";
}

} // namespace

Win32App::Win32App(HINSTANCE instance, int showCommand)
    : instance_(instance), capture_(graphics_), renderer_(graphics_) {
    const auto loaded = core::SettingsStore::LoadWithStatus(core::SettingsStore::Path());
    settings_ = loaded.settings;
    const auto loadedPresets = core::SettingsStore::LoadPresets(core::SettingsStore::PresetsPath());
    presets_ = loadedPresets.presets;
    std::fill(std::begin(customColors_), std::end(customColors_), RGB(255, 255, 255));
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (CreateControlWindow(showCommand)) {
        RefreshPresetNames();
        if (settings_.enabled) SetEnabled(true);
        if (!loaded.error.empty()) UpdateStatus(loaded.error);
        if (!loadedPresets.error.empty()) UpdateStatus(loadedPresets.error);
    }
}

Win32App::~Win32App() {
    shuttingDown_ = true;
    core::SettingsStore::Save(settings_);
    panel_.Shutdown();
    ShutdownGraphics();
    UnregisterHotkeys();
    RemoveTrayIcon();
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
}

bool Win32App::CreateControlWindow(int showCommand) {
    const HINSTANCE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        ShowStartupError(L"Could not retrieve the application module.", GetLastError());
        return false;
    }
    WNDCLASSEXW classDescription{};
    classDescription.cbSize = sizeof(classDescription);
    classDescription.hInstance = module;
    classDescription.lpfnWndProc = &Win32App::WindowProc;
    classDescription.lpszClassName = kWindowClass;
    classDescription.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    classDescription.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    const ATOM registeredClass = RegisterClassExW(&classDescription);
    const DWORD classError = registeredClass == 0 ? GetLastError() : ERROR_SUCCESS;
    if (registeredClass == 0 && classError != ERROR_CLASS_ALREADY_EXISTS) {
        ShowStartupError(L"Could not register the window class.", classError);
        return false;
    }

    const UINT dpi = GetDpiForSystem();
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int initialWidth = std::min(MulDiv(980, dpi, 96), static_cast<int>(workArea.right - workArea.left));
    const int initialHeight = std::min(MulDiv(940, dpi, 96), static_cast<int>(workArea.bottom - workArea.top));
    window_ = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        kWindowClass,
        kPanelTitle,
        WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        std::max(400, initialWidth),
        std::max(300, initialHeight),
        nullptr,
        nullptr,
        module,
        this);
    if (window_ == nullptr) {
        ShowStartupError(L"Could not create the settings window.", GetLastError());
        return false;
    }

    statusLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Ready — enable the filter to start capturing.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24,
        32,
        390,
        48,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(MakeControlId(100))),
        module,
        nullptr);

    RefreshMonitors();
    EnsurePanel();
    UpdateStatus(L"Ready — enable the filter to start capturing.");
    RegisterHotkeys();
    AddTrayIcon();
    ShowWindow(window_, showCommand == SW_HIDE ? SW_HIDE : SW_SHOW);
    UpdateWindow(window_);
    return true;
}

bool Win32App::RegisterHotkeys() {
    if (window_ == nullptr) {
        return false;
    }

    const UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    for (UINT id = kHotkeyToggle; id <= kHotkeyStop; ++id) {
        const UINT bit = 1u << id;
        if ((registeredHotkeys_ & bit) == 0 && RegisterHotKey(window_, id, modifiers, VK_F10 + id - 1) != FALSE) {
            registeredHotkeys_ |= bit;
        }
    }
    if ((registeredHotkeys_ & (1u << kHotkeyStop)) == 0) {
        UpdateStatus(L"Ctrl+Alt+F12 is already in use. Free this emergency stop shortcut before enabling the filter.");
    } else if (registeredHotkeys_ != 14u) {
        UpdateStatus(L"Some shortcuts are already in use. The settings panel and Ctrl+Alt+F12 remain available.");
    }
    return registeredHotkeys_ == 14u;
}

void Win32App::UnregisterHotkeys() {
    if (window_ == nullptr) {
        return;
    }
    UnregisterHotKey(window_, kHotkeyToggle);
    UnregisterHotKey(window_, kHotkeyPanel);
    UnregisterHotKey(window_, kHotkeyStop);
    registeredHotkeys_ = 0;
}

void Win32App::AddTrayIcon() {
    if (window_ == nullptr) {
        return;
    }
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = window_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    trayIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(trayIcon_.szTip, L"ScreenFX", _TRUNCATE);
    trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &trayIcon_) != FALSE;
}

void Win32App::RemoveTrayIcon() {
    if (trayIcon_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayIcon_ = {};
    }
    trayIconAdded_ = false;
}

void Win32App::ShowPanel() {
    if (window_ == nullptr) {
        return;
    }
    EnsurePanel();
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    KeepPanelAboveOverlay();
    SetForegroundWindow(window_);
}

void Win32App::HidePanel() {
    if (window_ != nullptr) {
        if (!trayIconAdded_ && (registeredHotkeys_ & (1u << kHotkeyPanel)) == 0) {
            UpdateStatus(L"The panel must stay open because its tray icon and reopen shortcut are unavailable.");
            return;
        }
        ShowWindow(window_, SW_HIDE);
    }
}

void Win32App::KeepPanelAboveOverlay() {
    if (window_ != nullptr) {
        SetWindowPos(window_, enabled_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void Win32App::UpdateStatus(const std::wstring& text) {
    statusText_ = text;
    if (statusLabel_ != nullptr) {
        SetWindowTextW(statusLabel_, text.c_str());
    }
}

bool Win32App::SaveSettings(bool announce) {
    std::wstring error;
    const bool saved = core::SettingsStore::Save(settings_, core::SettingsStore::Path(), &error);
    if (!saved) UpdateStatus(L"Settings could not be saved — " + error);
    else if (announce) UpdateStatus(L"Settings saved.");
    return saved;
}

void Win32App::OnToggleRequested() {
    SetEnabled(!enabled_);
}

void Win32App::OnStopRequested() {
    SetEnabled(false);
    ShutdownGraphics();
    settings_.enabled = false;
    UpdateStatus(L"Filter stopped.");
    SaveSettings();
}

bool Win32App::InitializeGraphics() {
    if (graphicsInitialized_) {
        return true;
    }
    if (monitors_.empty()) {
        RefreshMonitors();
    }
    if (monitors_.empty()) {
        UpdateStatus(L"No active monitors were detected.");
        return false;
    }
    if ((graphics_.Device() == nullptr || FAILED(graphics_.Device()->GetDeviceRemovedReason())) && !graphics_.Initialize()) {
        UpdateStatus(L"Direct3D 11 is unavailable on this computer.");
        return false;
    }
    if (!overlay_.Create(instance_)) {
        ShutdownGraphics();
        UpdateStatus(L"Could not create the overlay.");
        return false;
    }
    if (!overlay_.CaptureExcluded()) {
        ShutdownGraphics();
        UpdateStatus(L"Windows cannot exclude the overlay from capture. The filter remains off to prevent image feedback.");
        return false;
    }
    const auto& monitor = monitors_[std::min<std::size_t>(settings_.monitorIndex, monitors_.size() - 1)];
    overlay_.SetBounds(monitor.bounds);
    if (!renderer_.Initialize(overlay_.Handle(), monitor.bounds)) {
        const auto error = renderer_.LastError();
        ShutdownGraphics();
        UpdateStatus(L"Could not initialize GPU rendering or load the shaders. " + RenderError(error));
        return false;
    }
    if (!StartCaptureForSelectedMonitor()) {
        const std::wstring detail = capture_.LastError();
        ShutdownGraphics();
        UpdateStatus(detail.empty() ? L"Could not capture the selected monitor."
                                    : L"Could not capture the selected monitor — " + detail);
        return false;
    }
    graphicsInitialized_ = true;
    renderRequested_ = true;
    const std::wstring mode = settings_.framePacing == core::FramePacingMode::Uncapped
                                  ? L"uncapped"
                                  : L"VSync";
    UpdateStatus(L"Renderer ready — " + mode + L" mode.");
    return true;
}

bool Win32App::StartCaptureForSelectedMonitor() {
    if (monitors_.empty()) {
        return false;
    }
    const auto& monitor = monitors_[std::min<std::size_t>(settings_.monitorIndex, monitors_.size() - 1)];
    const SIZE size{monitor.bounds.right - monitor.bounds.left, monitor.bounds.bottom - monitor.bounds.top};
    const bool uncapped = settings_.framePacing == core::FramePacingMode::Uncapped;
    return capture_.Start(monitor.handle, size, uncapped);
}

void Win32App::ShutdownGraphics() {
    // Restore the desktop before potentially blocking in capture shutdown.
    overlay_.Hide();
    capture_.Stop();
    renderer_.Shutdown();
    overlay_.Destroy();
    graphicsInitialized_ = false;
    latestFrame_ = {};
    haveFrame_ = false;
    renderRequested_ = false;
    lastRenderedSequence_ = 0;
    firstFrameDeadline_ = 0;
}

void Win32App::SetEnabled(bool enabled) {
    if (enabled == enabled_) {
        return;
    }
    if (enabled && (registeredHotkeys_ & (1u << kHotkeyStop)) == 0) {
        RegisterHotkeys();
        if ((registeredHotkeys_ & (1u << kHotkeyStop)) == 0) {
            settings_.enabled = false;
            ShowPanel();
            return;
        }
    }
    if (enabled && !InitializeGraphics()) {
        enabled_ = false;
        settings_.enabled = false;
        return;
    }
    enabled_ = enabled;
    settings_.enabled = enabled_;
    if (enabled_) {
        overlay_.Hide();
        firstFrameDeadline_ = GetTickCount64() + kFirstFrameTimeoutMs;
        UpdateStatus(L"Filter enabled — waiting for the first frame.");
    } else {
        ShutdownGraphics();
        overlay_.Hide();
        UpdateStatus(L"Filter disabled.");
    }
    KeepPanelAboveOverlay();
    SaveSettings();
}

void Win32App::RefreshMonitors() {
    monitors_ = EnumerateMonitors();
    if (settings_.monitorIndex >= monitors_.size() && !monitors_.empty()) {
        settings_.monitorIndex = 0;
    }
}

void Win32App::RenderAvailableFrame() {
    if (!enabled_ || !graphicsInitialized_) {
        return;
    }
    const std::wstring captureError = capture_.LastError();
    if (!captureError.empty() || !capture_.Running()) {
        FailCapture(captureError.empty() ? L"Monitor capture stopped." : captureError);
        return;
    }
    if (firstFrameDeadline_ != 0 && GetTickCount64() >= firstFrameDeadline_) {
        FailCapture(L"No frame was presented within 5 seconds. Check that Windows capture is available, then enable the filter again.");
        return;
    }
    graphics::CapturedFrame newFrame;
    if (capture_.TryAcquireLatest(newFrame)) {
        latestFrame_ = std::move(newFrame);
        haveFrame_ = true;
        renderRequested_ = true;
    }
    if (!haveFrame_ || !renderRequested_) {
        return;
    }
    if (renderer_.Render(latestFrame_, settings_.effects, settings_.framePacing)) {
        lastRenderedSequence_ = latestFrame_.sequence;
        renderRequested_ = false;
        if (!overlay_.IsVisible()) {
            overlay_.Show();
            KeepPanelAboveOverlay();
            firstFrameDeadline_ = 0;
            UpdateStatus(L"Filter enabled — capturing.");
        }
    } else {
        FailCapture(RenderError(renderer_.LastError()));
    }
}

void Win32App::FailCapture(const std::wstring& detail) {
    SetEnabled(false);
    UpdateStatus(L"Filter stopped — " + detail);
    ShowPanel();
}

void Win32App::EnsurePanel() {
    if (panel_.IsInitialized()) {
        return;
    }
    if (panel_.Initialize(window_)) {
        if (statusLabel_ != nullptr) {
            ShowWindow(statusLabel_, SW_HIDE);
        }
    }
}

void Win32App::RestartCaptureIfRunning() {
    if (!graphicsInitialized_) {
        renderRequested_ = true;
        return;
    }
    if (monitors_.empty()) {
        OnStopRequested();
        UpdateStatus(L"No active monitors were detected.");
        return;
    }
    overlay_.Hide();
    capture_.Stop();
    latestFrame_ = {};
    haveFrame_ = false;
    lastRenderedSequence_ = 0;
    const auto& monitor = monitors_[std::min<std::size_t>(settings_.monitorIndex, monitors_.size() - 1)];
    overlay_.SetBounds(monitor.bounds);
    if (!renderer_.Resize(monitor.bounds)) {
        renderer_.Shutdown();
        if (!renderer_.Initialize(overlay_.Handle(), monitor.bounds)) {
            OnStopRequested();
            UpdateStatus(L"Could not resize the GPU render target.");
            return;
        }
    }
    if (!StartCaptureForSelectedMonitor()) {
        const std::wstring detail = capture_.LastError();
        FailCapture(L"Could not restart monitor capture. " + detail);
        return;
    }
    renderRequested_ = true;
    firstFrameDeadline_ = GetTickCount64() + kFirstFrameTimeoutMs;
    UpdateStatus(L"Filter enabled — waiting for the first frame.");
}

void Win32App::RenderPanel() {
    if (!panel_.IsInitialized() || !IsWindowVisible(window_)) {
        return;
    }
    panel_.Render(
        settings_,
        monitors_,
        overlay_.CaptureExcluded(),
        capture_.CapturedFrames(),
        capture_.DroppedFrames(),
        renderer_.PresentedFrames(),
        statusText_);
}

void Win32App::ProcessPanelActions() {
    const ui::PanelActions actions = panel_.TakeActions();
    if (actions.toggleRequested) {
        SetEnabled(actions.desiredEnabled);
    }
    if (actions.stopRequested) {
        OnStopRequested();
    }
    if (actions.monitorChanged || actions.pacingChanged) {
        RestartCaptureIfRunning();
    }
    if (actions.resetRequested) {
        settings_.effects = {};
        renderRequested_ = true;
    }
    if (actions.pickColorRequested) PickTintColor();
    if (actions.applyPresetRequested) ApplyPreset(actions.presetName);
    if (actions.savePresetRequested) SavePreset(actions.presetName);
    if (actions.settingsChanged) {
        renderRequested_ = true;
    }
    if (actions.saveRequested || actions.resetRequested || actions.monitorChanged || actions.pacingChanged) {
        SaveSettings(actions.saveRequested);
    }
}

void Win32App::RefreshPresetNames() {
    std::vector<std::wstring> names;
    for (const auto& preset : presets_) names.push_back(preset.name);
    panel_.SetPresetNames(names);
}

namespace {
std::wstring TrimPresetName(std::wstring name) {
    const auto first = name.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return name.substr(first, name.find_last_not_of(L" \t\r\n") - first + 1);
}
bool SamePresetName(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
}

void Win32App::SavePreset(std::wstring name) {
    name = TrimPresetName(std::move(name));
    if (name.empty()) {
        UpdateStatus(L"Enter a preset name, then click Save preset.");
        return;
    }
    // Reload before an explicit save so external JSON edits are preserved.
    auto current = core::SettingsStore::LoadPresets(core::SettingsStore::PresetsPath());
    if (current.status != core::SettingsLoadStatus::Loaded && current.status != core::SettingsLoadStatus::NotFound) {
        UpdateStatus(L"Preset not saved — " + current.error);
        return;
    }
    auto found = std::find_if(current.presets.begin(), current.presets.end(),
        [&](const core::Preset& preset) { return SamePresetName(preset.name, name); });
    const bool updating = found != current.presets.end();
    if (updating) *found = core::Preset{name, settings_.effects};
    else current.presets.push_back(core::Preset{name, settings_.effects});
    std::wstring error;
    if (!core::SettingsStore::SavePresets(current.presets, core::SettingsStore::PresetsPath(), &error)) {
        UpdateStatus(L"Preset not saved — " + error);
        return;
    }
    presets_ = std::move(current.presets);
    RefreshPresetNames();
    UpdateStatus((updating ? L"Preset updated: " : L"Preset saved: ") + name);
}

void Win32App::ApplyPreset(std::wstring name) {
    name = TrimPresetName(std::move(name));
    const auto found = std::find_if(presets_.begin(), presets_.end(),
        [&](const core::Preset& preset) { return SamePresetName(preset.name, name); });
    if (found == presets_.end()) {
        UpdateStatus(L"Choose a saved preset, then click Apply preset.");
        return;
    }
    settings_.effects = found->effects;
    renderRequested_ = true;
    UpdateStatus(L"Preset applied: " + found->name);
}

void Win32App::PickTintColor() {
    auto channel = [](float value) { return static_cast<BYTE>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F); };
    CHOOSECOLORW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = window_;
    picker.rgbResult = RGB(channel(settings_.effects.tintRed), channel(settings_.effects.tintGreen), channel(settings_.effects.tintBlue));
    picker.lpCustColors = customColors_;
    picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    // The native modal dialog pauses this render loop. Show the live desktop
    // while it is open, then wait for a fresh presentation before revealing the overlay.
    overlay_.Hide();
    firstFrameDeadline_ = 0;
    const bool chosen = ChooseColorW(&picker) != FALSE;
    const DWORD error = chosen ? 0 : CommDlgExtendedError();
    if (chosen) {
        settings_.effects.tintRed = GetRValue(picker.rgbResult) / 255.0F;
        settings_.effects.tintGreen = GetGValue(picker.rgbResult) / 255.0F;
        settings_.effects.tintBlue = GetBValue(picker.rgbResult) / 255.0F;
        if (settings_.effects.tintIntensity == 0.0F) settings_.effects.tintIntensity = 1.0F;
        UpdateStatus(L"Tint color selected. Use Tint strength to adjust the effect.");
    } else if (error != 0) {
        UpdateStatus(L"The color picker could not open (Windows code " + std::to_wstring(error) + L").");
    }
    if (enabled_) firstFrameDeadline_ = GetTickCount64() + kFirstFrameTimeoutMs;
    renderRequested_ = true;
}

void Win32App::ShowTrayMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    constexpr UINT kMenuToggle = 200;
    constexpr UINT kMenuPanel = 201;
    constexpr UINT kMenuQuit = 202;
    constexpr UINT kAppend = static_cast<UINT>(-1);
    InsertMenuW(menu, kAppend, MF_BYPOSITION | MF_STRING, kMenuToggle, enabled_ ? L"Disable filter" : L"Enable filter");
    InsertMenuW(menu, kAppend, MF_BYPOSITION | MF_STRING, kMenuPanel, L"Show settings");
    InsertMenuW(menu, kAppend, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(menu, kAppend, MF_BYPOSITION | MF_STRING, kMenuQuit, L"Quit");

    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, screenPoint.x, screenPoint.y, 0, window_, nullptr);
    DestroyMenu(menu);

    switch (command) {
    case kMenuToggle:
        OnToggleRequested();
        break;
    case kMenuPanel:
        ShowPanel();
        break;
    case kMenuQuit:
        DestroyWindow(window_);
        break;
    default:
        break;
    }
}

LRESULT CALLBACK Win32App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Win32App* app = reinterpret_cast<Win32App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        app = static_cast<Win32App*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app != nullptr ? app->HandleMessage(window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Win32App::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        AddTrayIcon();
        return 0;
    }
    if (panel_.HandleMessage(message, wParam, lParam)) {
        return 0;
    }
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* metrics = reinterpret_cast<MINMAXINFO*>(lParam);
        if (metrics != nullptr) {
            const UINT dpi = GetDpiForWindow(window);
            MONITORINFO monitor{sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
            metrics->ptMinTrackSize.x = std::min(MulDiv(900, dpi, 96), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
            metrics->ptMinTrackSize.y = std::min(MulDiv(740, dpi, 96), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
        }
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT& suggested = *reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
                     suggested.bottom - suggested.top, SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    case WM_APP + 2:
        ShowPanel();
        return 0;
    case WM_HOTKEY:
        switch (static_cast<int>(wParam)) {
        case kHotkeyToggle:
            OnToggleRequested();
            break;
        case kHotkeyPanel:
            ShowPanel();
            break;
        case kHotkeyStop:
            OnStopRequested();
            break;
        default:
            break;
        }
        return 0;
    case kTrayMessage:
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowPanel();
        } else if (lParam == WM_RBUTTONUP) {
            POINT point{};
            GetCursorPos(&point);
            ShowTrayMenu(point);
        }
        return 0;
    case WM_CLOSE:
        HidePanel();
        return 0;
    case WM_DISPLAYCHANGE:
        RefreshMonitors();
        if (enabled_) {
            RestartCaptureIfRunning();
        }
        return 0;
    case WM_DESTROY:
        // Release registrations while the HWND is still valid.
        UnregisterHotkeys();
        RemoveTrayIcon();
        ShutdownGraphics();
        panel_.Shutdown();
        statusLabel_ = nullptr;
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int Win32App::Run() {
    if (window_ == nullptr) {
        return 1;
    }
    MSG message{};
    while (!shuttingDown_) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) {
                shuttingDown_ = true;
                break;
            }
            if (!IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            } else {
                panel_.EnsureFocusVisible();
            }
        }
        if (shuttingDown_) {
            break;
        }

        ProcessPanelActions();
        RenderAvailableFrame();
        RenderPanel();

        // Actions may have enabled capture during this iteration. Select the
        // wait source only now so that its first frame cannot be lost.
        HANDLE event = enabled_ && graphicsInitialized_ ? capture_.FrameEvent() : nullptr;
        DWORD timeout = INFINITE;
        if (firstFrameDeadline_ != 0) {
            const ULONGLONG now = GetTickCount64();
            timeout = now >= firstFrameDeadline_ ? 0 : static_cast<DWORD>(firstFrameDeadline_ - now);
        }
        if (MsgWaitForMultipleObjectsEx(event != nullptr ? 1 : 0, event != nullptr ? &event : nullptr,
                                      timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_FAILED) {
            if (enabled_) {
                FailCapture(L"Could not wait for captured frames.");
                RenderPanel();
            } else {
                return 1;
            }
        }
    }
    return static_cast<int>(message.wParam);
}

} // namespace screenfx::platform
