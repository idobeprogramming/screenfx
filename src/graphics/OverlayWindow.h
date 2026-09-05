#pragma once

#include <windows.h>

namespace screenfx::graphics {

class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create(HINSTANCE instance);
    void Destroy();
    void SetBounds(const RECT& bounds);
    void Show();
    void Hide();
    bool IsVisible() const noexcept;
    HWND Handle() const noexcept { return window_; }
    bool CaptureExcluded() const noexcept { return captureExcluded_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    bool captureExcluded_ = false;
};

} // namespace screenfx::graphics
