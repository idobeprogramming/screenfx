#include "platform/Win32App.h"

#include <windows.h>

#include <string_view>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    if (commandLine != nullptr && std::wstring_view(commandLine) == L"--self-test") {
        return 0;
    }

    screenfx::platform::Win32App app(instance, showCommand);
    return app.Run();
}
