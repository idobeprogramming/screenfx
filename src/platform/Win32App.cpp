#include "Win32App.h"

#include <shellapi.h>

#include <algorithm>
#include <array>

namespace screenfx::platform {
namespace {

constexpr wchar_t kWindowClass[] = L"ScreenFX.ControlWindow";
constexpr wchar_t kWindowTitle[] = L"ScreenFX";
constexpr wchar_t kPanelTitle[] = L"ScreenFX — réglages";

int MakeControlId(int base) {
    return base;
}

} // namespace

Win32App::Win32App(HINSTANCE instance, int showCommand)
    : instance_(instance), settings_(core::SettingsStore::Load()), capture_(graphics_), renderer_(graphics_) {
    if (CreateControlWindow(showCommand) && settings_.enabled) {
        SetEnabled(true);
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
    WNDCLASSEXW classDescription{};
    classDescription.cbSize = sizeof(classDescription);
    classDescription.hInstance = instance_;
    classDescription.lpfnWndProc = &Win32App::WindowProc;
    classDescription.lpszClassName = kWindowClass;
    classDescription.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    classDescription.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (RegisterClassExW(&classDescription) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClass,
        kPanelTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        980,
        820,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        return false;
    }

    statusLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Prêt — activez le filtre pour démarrer la capture.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24,
        32,
        390,
        48,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(MakeControlId(100))),
        instance_,
        nullptr);

    RefreshMonitors();
    EnsurePanel();
    UpdateStatus(L"Prêt — activez le filtre pour démarrer la capture.");
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
    const bool toggle = RegisterHotKey(window_, kHotkeyToggle, modifiers, VK_F10) != FALSE;
    const bool panel = RegisterHotKey(window_, kHotkeyPanel, modifiers, VK_F11) != FALSE;
    const bool stop = RegisterHotKey(window_, kHotkeyStop, modifiers, VK_F12) != FALSE;
    hotkeysRegistered_ = toggle && panel && stop;

    if (!hotkeysRegistered_) {
        if (toggle) UnregisterHotKey(window_, kHotkeyToggle);
        if (panel) UnregisterHotKey(window_, kHotkeyPanel);
        if (stop) UnregisterHotKey(window_, kHotkeyStop);
        MessageBoxW(
            window_,
            L"Les raccourcis Ctrl+Alt+F10/F11/F12 ne sont pas tous disponibles.\n\nLe bouton d’arrêt du panneau reste utilisable.",
            kWindowTitle,
            MB_ICONWARNING | MB_OK);
    }
    return hotkeysRegistered_;
}

void Win32App::UnregisterHotkeys() {
    if (window_ == nullptr || !hotkeysRegistered_) {
        return;
    }
    UnregisterHotKey(window_, kHotkeyToggle);
    UnregisterHotKey(window_, kHotkeyPanel);
    UnregisterHotKey(window_, kHotkeyStop);
    hotkeysRegistered_ = false;
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
    Shell_NotifyIconW(NIM_ADD, &trayIcon_);
}

void Win32App::RemoveTrayIcon() {
    if (trayIcon_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayIcon_ = {};
    }
}

void Win32App::ShowPanel() {
    if (window_ == nullptr) {
        return;
    }
    EnsurePanel();
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
}

void Win32App::HidePanel() {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
}

void Win32App::UpdateStatus(const std::wstring& text) {
    statusText_ = text;
    if (statusLabel_ != nullptr) {
        SetWindowTextW(statusLabel_, text.c_str());
    }
}

void Win32App::OnToggleRequested() {
    SetEnabled(!enabled_);
}

void Win32App::OnStopRequested() {
    SetEnabled(false);
    ShutdownGraphics();
    settings_.enabled = false;
    core::SettingsStore::Save(settings_);
    UpdateStatus(L"Filtre arrêté.");
}

bool Win32App::InitializeGraphics() {
    if (graphicsInitialized_) {
        return true;
    }
    if (monitors_.empty()) {
        RefreshMonitors();
    }
    if (monitors_.empty()) {
        UpdateStatus(L"Aucun moniteur actif n’a été détecté.");
        return false;
    }
    if (graphics_.Device() == nullptr && !graphics_.Initialize()) {
        UpdateStatus(L"Direct3D 11 n’est pas disponible sur cette machine.");
        return false;
    }
    if (!overlay_.Create(instance_)) {
        ShutdownGraphics();
        UpdateStatus(L"Impossible de créer la superposition.");
        return false;
    }
    const auto& monitor = monitors_[std::min<std::size_t>(settings_.monitorIndex, monitors_.size() - 1)];
    overlay_.SetBounds(monitor.bounds);
    if (!renderer_.Initialize(overlay_.Handle(), monitor.bounds)) {
        ShutdownGraphics();
        UpdateStatus(L"Impossible d’initialiser le rendu GPU ou les shaders.");
        return false;
    }
    if (!StartCaptureForSelectedMonitor()) {
        ShutdownGraphics();
        UpdateStatus(L"Impossible de capturer le moniteur sélectionné.");
        return false;
    }
    graphicsInitialized_ = true;
    renderRequested_ = true;
    const std::wstring mode = settings_.framePacing == core::FramePacingMode::Uncapped
                                  ? L"sans plafond logiciel"
                                  : L"synchronisé à l’écran";
    UpdateStatus(L"Moteur prêt — mode " + mode + L".");
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
    capture_.Stop();
    overlay_.Hide();
    renderer_.Shutdown();
    overlay_.Destroy();
    graphicsInitialized_ = false;
    latestFrame_ = {};
    haveFrame_ = false;
    renderRequested_ = false;
    lastRenderedSequence_ = 0;
}

void Win32App::SetEnabled(bool enabled) {
    if (enabled == enabled_) {
        return;
    }
    if (enabled && !InitializeGraphics()) {
        enabled_ = false;
        settings_.enabled = false;
        core::SettingsStore::Save(settings_);
        return;
    }
    enabled_ = enabled;
    settings_.enabled = enabled_;
    if (enabled_) {
        overlay_.Show();
        UpdateStatus(L"Filtre actif — capture en cours.");
    } else {
        ShutdownGraphics();
        overlay_.Hide();
        UpdateStatus(L"Filtre désactivé.");
    }
    core::SettingsStore::Save(settings_);
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
    } else {
        // Wait for a fresh capture before retrying. This avoids a tight retry loop
        // when a device or swap chain has just reported an error.
        renderRequested_ = false;
    }
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
        UpdateStatus(L"Aucun moniteur actif n’a été détecté.");
        return;
    }
    const auto& monitor = monitors_[std::min<std::size_t>(settings_.monitorIndex, monitors_.size() - 1)];
    overlay_.SetBounds(monitor.bounds);
    if (!renderer_.Resize(monitor.bounds)) {
        renderer_.Shutdown();
        if (!renderer_.Initialize(overlay_.Handle(), monitor.bounds)) {
            OnStopRequested();
            UpdateStatus(L"Impossible de redimensionner le rendu GPU.");
            return;
        }
    }
    capture_.Stop();
    if (!StartCaptureForSelectedMonitor()) {
        OnStopRequested();
        UpdateStatus(L"Impossible de redémarrer la capture du moniteur.");
        return;
    }
    renderRequested_ = true;
}

void Win32App::RenderPanel() {
    if (!panel_.IsInitialized() || !IsWindowVisible(window_)) {
        return;
    }
    const ui::PanelActions actions = panel_.Render(
        settings_,
        monitors_,
        overlay_.CaptureExcluded(),
        capture_.CapturedFrames(),
        capture_.DroppedFrames(),
        renderer_.PresentedFrames(),
        statusText_);
    if (actions.toggleRequested) {
        SetEnabled(actions.desiredEnabled);
    }
    if (actions.stopRequested) {
        OnStopRequested();
    }
    if (actions.monitorChanged) {
        RestartCaptureIfRunning();
    }
    if (actions.pacingChanged) {
        RestartCaptureIfRunning();
    }
    if (actions.resetRequested) {
        settings_.effects = {};
        renderRequested_ = true;
    }
    if (actions.settingsChanged) {
        renderRequested_ = true;
    }
    if (actions.saveRequested || actions.monitorChanged || actions.pacingChanged) {
        core::SettingsStore::Save(settings_);
    }
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
    InsertMenuW(menu, kAppend, MF_BYPOSITION | MF_STRING, kMenuToggle, enabled_ ? L"Désactiver" : L"Activer");
    InsertMenuW(menu, kAppend, MF_BYPOSITION | MF_STRING, kMenuPanel, L"Afficher les réglages");
    InsertMenuW(menu, kAppend, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(menu, kAppend, MF_BYPOSITION | MF_STRING, kMenuQuit, L"Quitter");

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
    return app != nullptr ? app->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Win32App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (panel_.HandleMessage(message, wParam, lParam)) {
        return 0;
    }
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* metrics = reinterpret_cast<MINMAXINFO*>(lParam);
        if (metrics != nullptr) {
            metrics->ptMinTrackSize.x = 900;
            metrics->ptMinTrackSize.y = 700;
        }
        return 0;
    }
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
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

int Win32App::Run() {
    MSG message{};
    while (!shuttingDown_) {
        bool processedMessage = false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            processedMessage = true;
            if (message.message == WM_QUIT) {
                shuttingDown_ = true;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (shuttingDown_) {
            break;
        }

        if (enabled_ && graphicsInitialized_) {
            RenderPanel();
            RenderAvailableFrame();
            if (!processedMessage && capture_.FrameEvent() != nullptr) {
                HANDLE event = capture_.FrameEvent();
                MsgWaitForMultipleObjectsEx(1, &event, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        } else {
            RenderPanel();
            WaitMessage();
        }
    }
    return static_cast<int>(message.wParam);
}

} // namespace screenfx::platform
