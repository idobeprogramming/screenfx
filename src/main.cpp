#include "platform/Win32App.h"
#include "platform/Monitors.h"

#include <windows.h>
#include <winrt/base.h>

#include <string_view>
#include <string>

namespace {
struct Apartment {
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
};
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    if (commandLine != nullptr && std::wstring_view(commandLine) == L"--self-test") {
        const auto monitors = screenfx::platform::EnumerateMonitors();
        return monitors.empty() ? 1 : 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        winrt::handle instanceMutex{CreateMutexW(nullptr, FALSE, L"Local\\ScreenFX.SingleInstance")};
        const DWORD mutexError = GetLastError();
        if (!instanceMutex) winrt::throw_last_error();
        if (mutexError == ERROR_ALREADY_EXISTS) {
            // A second double-click brings back the existing settings panel.
            const auto deadline = GetTickCount64() + 2000;
            do {
                if (HWND existing = FindWindowW(L"ScreenFX.ControlWindow", nullptr)) {
                    DWORD process = 0;
                    GetWindowThreadProcessId(existing, &process);
                    AllowSetForegroundWindow(process);
                    PostMessageW(existing, WM_APP + 2, 0, 0);
                    return 0;
                }
                Sleep(20);
            } while (GetTickCount64() < deadline);
            MessageBoxW(nullptr, L"ScreenFX is already starting. Please try again in a moment.",
                        L"ScreenFX", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        Apartment apartment;
        screenfx::platform::Win32App app(instance, showCommand);
        // app (and every WinRT object it owns) is destroyed before the apartment.
        return app.Run();
    } catch (const winrt::hresult_error& error) {
        wchar_t code[32]{};
        swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(error.code()));
        const std::wstring message = L"ScreenFX stopped.\n\n" + std::wstring(code) + L": " + error.message().c_str();
        MessageBoxW(nullptr, message.c_str(), L"ScreenFX", MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(nullptr, L"ScreenFX stopped because of an unexpected error.", L"ScreenFX", MB_OK | MB_ICONERROR);
    }
    return 1;
}
