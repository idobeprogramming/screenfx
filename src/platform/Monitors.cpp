#include "Monitors.h"

#include <algorithm>

namespace screenfx::platform {
namespace {

struct EnumerationContext {
    std::vector<MonitorInfo>* monitors = nullptr;
};

BOOL CALLBACK EnumerateMonitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* context = reinterpret_cast<EnumerationContext*>(data);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (context == nullptr || context->monitors == nullptr || GetMonitorInfoW(monitor, &monitorInfo) == FALSE) {
        return TRUE;
    }

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    double refreshRate = 0.0;
    if (EnumDisplaySettingsExW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode, 0) != FALSE) {
        if (mode.dmDisplayFrequency > 1 && mode.dmDisplayFrequency != 0xFFFFFFFFu) {
            refreshRate = static_cast<double>(mode.dmDisplayFrequency);
        }
    }

    MonitorInfo entry;
    entry.handle = monitor;
    entry.deviceName = monitorInfo.szDevice;
    entry.bounds = monitorInfo.rcMonitor;
    entry.primary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
    entry.refreshRate = refreshRate;
    context->monitors->push_back(std::move(entry));
    return TRUE;
}

} // namespace

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumerationContext context{&monitors};
    EnumDisplayMonitors(nullptr, nullptr, &EnumerateMonitorCallback, reinterpret_cast<LPARAM>(&context));
    std::sort(monitors.begin(), monitors.end(), [](const MonitorInfo& left, const MonitorInfo& right) {
        if (left.primary != right.primary) {
            return left.primary;
        }
        if (left.bounds.top != right.bounds.top) {
            return left.bounds.top < right.bounds.top;
        }
        return left.bounds.left < right.bounds.left;
    });
    return monitors;
}

} // namespace screenfx::platform
