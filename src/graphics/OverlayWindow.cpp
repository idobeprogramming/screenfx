#include "OverlayWindow.h"

namespace screenfx::graphics {
namespace {

constexpr wchar_t kOverlayClass[] = L"ScreenFX.OutputOverlay";

} // namespace

OverlayWindow::~OverlayWindow() {
    Destroy();
}

bool OverlayWindow::Create(HINSTANCE instance) {
    if (window_ != nullptr) {
        return true;
    }
    instance_ = instance;

    WNDCLASSEXW classDescription{};
    classDescription.cbSize = sizeof(classDescription);
    classDescription.hInstance = instance_;
    classDescription.lpfnWndProc = &OverlayWindow::WindowProc;
    classDescription.lpszClassName = kOverlayClass;
    classDescription.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassExW(&classDescription) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    // LAYERED + TRANSPARENT makes Windows pass mouse input through to windows
    // in other processes. HTTRANSPARENT alone only traverses our own thread.
    constexpr DWORD extendedStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                    WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP;
    window_ = CreateWindowExW(
        extendedStyle,
        kOverlayClass,
        L"ScreenFX output",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        return false;
    }
    if (!SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA)) {
        Destroy();
        return false;
    }

    captureExcluded_ = SetWindowDisplayAffinity(window_, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    return true;
}

void OverlayWindow::Destroy() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    captureExcluded_ = false;
}

void OverlayWindow::SetBounds(const RECT& bounds) {
    if (window_ == nullptr) {
        return;
    }
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void OverlayWindow::Show() {
    if (window_ != nullptr && !IsVisible()) {
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void OverlayWindow::Hide() {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
}

bool OverlayWindow::IsVisible() const noexcept {
    return window_ != nullptr && IsWindowVisible(window_) != FALSE;
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace screenfx::graphics
