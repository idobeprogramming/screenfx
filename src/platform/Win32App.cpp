#include "Win32App.h"

#include <shellapi.h>

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

Win32App::Win32App(HINSTANCE instance, int showCommand) : instance_(instance) {
    CreateControlWindow(showCommand);
}

Win32App::~Win32App() {
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
        460,
        300,
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
        L"Prêt — le moteur graphique sera initialisé à l’étape suivante.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24,
        32,
        390,
        48,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(MakeControlId(100))),
        instance_,
        nullptr);

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
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
}

void Win32App::HidePanel() {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
}

void Win32App::UpdateStatus(const std::wstring& text) {
    if (statusLabel_ != nullptr) {
        SetWindowTextW(statusLabel_, text.c_str());
    }
}

void Win32App::OnToggleRequested() {
    enabled_ = !enabled_;
    UpdateStatus(enabled_ ? L"Filtre activé — moteur graphique en préparation." : L"Filtre désactivé.");
}

void Win32App::OnStopRequested() {
    enabled_ = false;
    UpdateStatus(L"Filtre arrêté.");
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
    switch (message) {
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
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

int Win32App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

} // namespace screenfx::platform
