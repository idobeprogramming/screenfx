#include "platform/Win32App.h"
#include "platform/Monitors.h"

#include <windows.h>
#include <winrt/base.h>

#include <string_view>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    if (commandLine != nullptr && std::wstring_view(commandLine) == L"--self-test") {
        const auto monitors = screenfx::platform::EnumerateMonitors();
        return monitors.empty() ? 1 : 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    screenfx::platform::Win32App app(instance, showCommand);
    const int result = app.Run();
    winrt::uninit_apartment();
    return result;
}
