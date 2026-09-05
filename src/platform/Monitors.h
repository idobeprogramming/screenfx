#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace screenfx::platform {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    std::wstring deviceName;
    RECT bounds{};
    bool primary = false;
    bool hdr = false;
    double refreshRate = 0.0;
};

std::vector<MonitorInfo> EnumerateMonitors();

} // namespace screenfx::platform
