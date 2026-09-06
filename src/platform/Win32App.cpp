#include "Win32App.h"

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cwchar>

namespace screenfx::platform {
namespace {

constexpr wchar_t kWindowClass[] = L"ScreenFX.ControlWindow";
constexpr wchar_t kWindowTitle[] = L"ScreenFX";
constexpr wchar_t kPanelTitle[] = L"ScreenFX — réglages";
constexpr ULONGLONG kFirstFrameTimeoutMs = 5000;

int MakeControlId(int base) {
    return base;
}

void ShowStartupError(const wchar_t* operation, DWORD error) {
    const std::wstring message = std::wstring(L"ScreenFX ne peut pas démarrer.\n\n") + operation +
                                 L"\nCode Windows : " + std::to_wstring(error);
    MessageBoxW(nullptr, message.c_str(), L"ScreenFX", MB_ICONERROR | MB_OK);
}

std::wstring RenderError(HRESULT result) {
    wchar_t code[32]{};
    swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(result));
    return L"Rendu GPU interrompu (" + std::wstring(code) + L").";
}

} // namespace

Win32App::Win32App(HINSTANCE instance, int showCommand)
    : instance_(instance), capture_(graphics_), renderer_(graphics_) {
    const auto loaded = core::SettingsStore::LoadWithStatus(core::SettingsStore::Path());
    settings_ = loaded.settings;
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (CreateControlWindow(showCommand)) {
        if (settings_.enabled) SetEnabled(true);
        if (!loaded.error.empty()) UpdateStatus(loaded.error);
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
        ShowStartupError(L"Récupération du module de l’application impossible.", GetLastError());
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
        ShowStartupError(L"Enregistrement de la classe de fenêtre impossible.", classError);
        return false;
    }

    const UINT dpi = GetDpiForSystem();
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int initialWidth = std::min(MulDiv(980, dpi, 96), static_cast<int>(workArea.right - workArea.left));
    const int initialHeight = std::min(MulDiv(820, dpi, 96), static_cast<int>(workArea.bottom - workArea.top));
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
        ShowStartupError(L"Création de la fenêtre de contrôle impossible.", GetLastError());
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
        module,
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
    for (UINT id = kHotkeyToggle; id <= kHotkeyStop; ++id) {
        const UINT bit = 1u << id;
        if ((registeredHotkeys_ & bit) == 0 && RegisterHotKey(window_, id, modifiers, VK_F10 + id - 1) != FALSE) {
            registeredHotkeys_ |= bit;
        }
    }
    if ((registeredHotkeys_ & (1u << kHotkeyStop)) == 0) {
        UpdateStatus(L"Ctrl+Alt+F12 est déjà utilisé. Libérez ce raccourci d’arrêt avant d’activer le filtre.");
    } else if (registeredHotkeys_ != 14u) {
        UpdateStatus(L"Certains raccourcis sont déjà utilisés. Le panneau et Ctrl+Alt+F12 restent disponibles.");
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
            UpdateStatus(L"Le panneau reste ouvert : l’icône et le raccourci pour le rouvrir sont indisponibles.");
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
    if (!saved) UpdateStatus(L"Réglages non enregistrés — " + error);
    else if (announce) UpdateStatus(L"Réglages enregistrés.");
    return saved;
}

void Win32App::OnToggleRequested() {
    SetEnabled(!enabled_);
}

void Win32App::OnStopRequested() {
    SetEnabled(false);
    ShutdownGraphics();
    settings_.enabled = false;
    UpdateStatus(L"Filtre arrêté.");
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
        UpdateStatus(L"Aucun moniteur actif n’a été détecté.");
        return false;
    }
    if ((graphics_.Device() == nullptr || FAILED(graphics_.Device()->GetDeviceRemovedReason())) && !graphics_.Initialize()) {
        UpdateStatus(L"Direct3D 11 n’est pas disponible sur cette machine.");
        return false;
    }
    if (!overlay_.Create(instance_)) {
        ShutdownGraphics();
        UpdateStatus(L"Impossible de créer la superposition.");
        return false;
    }
    if (!overlay_.CaptureExcluded()) {
        ShutdownGraphics();
        UpdateStatus(L"Windows ne peut pas exclure la superposition de la capture. Le filtre reste arrêté pour éviter une boucle d’image.");
        return false;
    }
    const auto& monitor = monitors_[std::min<std::size_t>(settings_.monitorIndex, monitors_.size() - 1)];
    overlay_.SetBounds(monitor.bounds);
    if (!renderer_.Initialize(overlay_.Handle(), monitor.bounds)) {
        const auto error = renderer_.LastError();
        ShutdownGraphics();
        UpdateStatus(L"Impossible d’initialiser le rendu GPU ou les shaders. " + RenderError(error));
        return false;
    }
    if (!StartCaptureForSelectedMonitor()) {
        const std::wstring detail = capture_.LastError();
        ShutdownGraphics();
        UpdateStatus(detail.empty() ? L"Impossible de capturer le moniteur sélectionné."
                                    : L"Impossible de capturer le moniteur sélectionné — " + detail);
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
        UpdateStatus(L"Filtre actif — attente de la première image.");
    } else {
        ShutdownGraphics();
        overlay_.Hide();
        UpdateStatus(L"Filtre désactivé.");
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
        FailCapture(captureError.empty() ? L"La capture du moniteur s’est arrêtée." : captureError);
        return;
    }
    if (firstFrameDeadline_ != 0 && GetTickCount64() >= firstFrameDeadline_) {
        FailCapture(L"Aucune image présentée après 5 secondes. Vérifiez la disponibilité de la capture Windows puis réactivez le filtre.");
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
            UpdateStatus(L"Filtre actif — capture en cours.");
        }
    } else {
        FailCapture(RenderError(renderer_.LastError()));
    }
}

void Win32App::FailCapture(const std::wstring& detail) {
    SetEnabled(false);
    UpdateStatus(L"Filtre arrêté — " + detail);
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
        UpdateStatus(L"Aucun moniteur actif n’a été détecté.");
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
            UpdateStatus(L"Impossible de redimensionner le rendu GPU.");
            return;
        }
    }
    if (!StartCaptureForSelectedMonitor()) {
        const std::wstring detail = capture_.LastError();
        FailCapture(L"Impossible de redémarrer la capture du moniteur. " + detail);
        return;
    }
    renderRequested_ = true;
    firstFrameDeadline_ = GetTickCount64() + kFirstFrameTimeoutMs;
    UpdateStatus(L"Filtre actif — attente de la première image.");
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
    if (actions.settingsChanged) {
        renderRequested_ = true;
    }
    if (actions.saveRequested || actions.resetRequested || actions.monitorChanged || actions.pacingChanged) {
        SaveSettings(actions.saveRequested);
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
                FailCapture(L"Impossible d’attendre les images de capture.");
                RenderPanel();
            } else {
                return 1;
            }
        }
    }
    return static_cast<int>(message.wParam);
}

} // namespace screenfx::platform
