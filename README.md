# ScreenFX

ScreenFX applies CRT and color effects to your desktop. On Windows, it captures a monitor and displays a GPU-filtered overlay over the desktop and windowed or borderless games. On Linux, a Qt panel controls an effect inside KDE Plasma's KWin or Hyprland.

Linux support is experimental. For dependencies, building, installation and validation limits, see [Linux setup](docs/LINUX.md). The Linux version requires a supported Wayland compositor; the Windows executable cannot provide that integration through Wine.

## Stack

- C++20, Win32, and C++/WinRT
- Windows Graphics Capture
- Direct3D 11, DXGI, and DirectComposition
- HLSL Shader Model 5
- Native Win32 controls for the settings panel
- CMake + Ninja + MSVC

The default frame pacing mode has no software FPS cap. Actual presentation speed depends on Windows, the GPU, and the monitor.

## Run on Windows

Double-click `build/release/ScreenFX.exe`. Keep the `shaders` folder next to the executable: it contains the two compiled shaders required for rendering. Launching an existing build does not require a terminal or CMake.

In the settings panel, check **Enable filter**. Effects start neutral on the first launch: increase **Scanlines** and **RGB phosphor mask** to create a CRT appearance. Use **Tint color** to choose a color and **Tint strength** to control how much it affects the picture. Choosing a color enables tint if its strength was zero. A strength of zero leaves the image color unchanged.

Closing the panel hides it. Press **Ctrl+Alt+F11**, or double-click the executable again, to bring it back. To quit, use the ScreenFX tray icon menu near the clock.

Target: Windows 10 version 2004 or later, x64, with a Direct3D 11 GPU. Rendering is SDR; HDR and exclusive full-screen games have not been validated. Captured frames remain in GPU memory during normal application use.

## Custom presets

Adjust the effects, enter a preset name, then click **Save preset**. Choose a saved preset and click **Apply preset** to restore its effects. A preset includes all effect values, including the tint color and strength; it does not change the selected monitor, frame pacing, or whether the filter is enabled.

Saving an existing name updates that preset. Names ignore letter case and can contain up to 80 characters; you can save up to 64 presets. **Save settings** separately saves the current application settings for the next launch.

Choose a saved preset and click **Delete preset** to remove it from `presets.json`. The current effect values stay as they are. Deletion is saved immediately; it does not require **Save settings**.

Presets are saved in `%LOCALAPPDATA%\ScreenFX\presets.json`. **Save preset** and **Delete preset** explicitly update this library. Moving sliders, applying a preset, stopping the filter, and closing the application do not save or overwrite presets.

## Development

Install Visual Studio with **Desktop development with C++**, **CMake tools for Windows**, and the **Windows SDK**. From a regular PowerShell in the project folder:

```powershell
.\build.ps1
```

The script locates Visual Studio, configures and builds Release, then runs the tests. For Debug, use `.\build.ps1 -Configuration Debug`. It does not modify the PATH of your session.

From a Visual Studio developer terminal, you can also run CMake directly:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

The settings panel does not download any external dependencies.

The renderer reuses capture texture views and skips expensive shader work for disabled effects. Panel counters refresh up to four times per second; effect controls and error messages remain immediate. **Uncapped** keeps the existing presentation mode without an added FPS limit. **VSync** limits the presentation queue to one frame and waits before acquiring the latest captured image, reducing stale-frame queueing while keeping capture and controls responsive.

For reproducible GPU measurements, run `.\build\release\screenfx_performance_tests.exe --benchmark` after building Release. See [performance results and methodology](docs/PERFORMANCE.md) for the tested gains and tradeoffs.

To produce an installation folder:

```powershell
.\build.ps1 -Package
```

The installed application and its two shader files are placed together under `dist/bin`. The C++ runtime is linked statically.

To test capture and presentation in your Windows session:

```powershell
.\build\release\screenfx_graphics_tests.exe --desktop
```

This test checks shader pixels, three capture restarts, both presentation modes, and resizing. A small color pattern then checks visible rendering, click-through behavior, and capture exclusion. It closes automatically; no images are saved. This test requires an unlocked desktop session. Ordinary CTest runs do not display this pattern.

## Troubleshooting

The overlay stays hidden until the first successful presentation. If no frame is presented within five seconds, or a capture/rendering error occurs, the filter stops and the panel reports the cause. **Ctrl+Alt+F12** also stops the filter immediately.

If the panel reports `0x80070424`, launch the application from your interactive Windows session. This code was observed under the isolated test account while capture worked in the user session; the code alone is not a reason to change Windows services.

Application settings are stored in `%LOCALAPPDATA%\ScreenFX\settings.json`. Invalid files and files with an unsupported version are preserved, and saving is refused with an error message. To restore default settings, quit ScreenFX and rename the file so you retain a backup.

## Shortcuts

- `Ctrl+Alt+F10`: enable or disable the filter
- `Ctrl+Alt+F11`: show the settings panel
- `Ctrl+Alt+F12`: stop the filter immediately
