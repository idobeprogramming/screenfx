# ScreenFX on Linux

ScreenFX applies its effects inside the desktop compositor on Linux. The Qt 6 panel controls a native KWin effect on KDE Plasma, or Hyprland's screen shader. There is no screen-capture overlay and no extra capture/presentation queue.

The Linux application and KWin module build successfully, and all four automated Linux test suites pass. This is an experimental Linux port: a physical KDE or Hyprland desktop has **not** been validated yet. Rendering targets SDR; HDR, color-managed output, mixed-DPI monitors and variable refresh rate still need desktop testing.

## Supported desktop paths

| Desktop | Requirements and scope |
| --- | --- |
| KDE Plasma | KWin 6.6 or later development files, with an OpenGL compositor that supports framebuffer blits. Build the native module against the **same KWin version** installed on the target desktop; rebuild after KWin updates. Later releases may require source adjustments. |
| Hyprland | A running Hyprland session and its matching `hyprctl`, exposing `decoration:screen_shader` and `debug:damage_tracking`. ScreenFX detects the Lua provider through `hyprctl -j status` and uses `eval`; older releases use `keyword`. Both command paths are covered by IPC fixtures. |
| Other desktops | No backend is implemented for GNOME, other Wayland compositors, or a standalone X11 desktop. The panel reports that a supported compositor is required. |

Effects apply to the compositor's outputs; the Linux panel currently has no monitor selector. Display synchronization and refresh rate remain controlled by the compositor. There is no ScreenFX VSync switch or software FPS cap on Linux. Enabling the KWin effect prevents direct scanout while the effect is active. Hyprland temporarily requests full repainting so spatial filters and animated grain update correctly; this can increase GPU use.

## Build

Use a C++20 compiler, CMake 3.26 or later, Ninja, and Qt 6.4 or later with Core, Gui, Widgets, DBus and OpenGL development support. A Wayland Qt platform plugin is required to open the panel in a Wayland session. KWin builds also require the KWin 6.6 development package, Extra CMake Modules and KF6 GlobalAccel, including their development dependencies.

The tested Ubuntu 26.04 environment used these package names:

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-wayland \
    kwin-dev extra-cmake-modules libkf6globalaccel-dev libepoxy-dev libdrm-dev \
    xvfb xauth dbus-x11
```

This list describes Ubuntu 26.04; package names and available KWin versions differ on other distributions. Xvfb and Xauth provide an isolated display for the shader tests, and `dbus-run-session` is needed for the backend tests. For a Hyprland-only build, the KWin, ECM and GlobalAccel dependencies can be omitted; Qt OpenGL support and the test dependencies are still needed.

From the project folder:

```sh
sh build.sh
```

The script configures Release, builds, then runs CTest. It produces:

- `build/linux-release/screenfx`: settings panel and compositor controller.
- `build/linux-release/kwin/effects/plugins/screenfx.so`: native KWin module.

For Hyprland without the KWin module:

```sh
sh build.sh -DSCREENFX_BUILD_KWIN=OFF
```

CMake remembers that option in the build folder. Use `sh build.sh -DSCREENFX_BUILD_KWIN=ON` to enable KWin again.

## Install

For Hyprland, running `build/linux-release/screenfx` directly is sufficient. KDE also needs its module installed where the running KWin can find it.

The default install prefix is `/usr/local`. The default KWin module destination in the tested build was `/usr/local/lib/plugins/kwin/effects/plugins`, which is not necessarily in KWin's plugin search path. Configure the plugin destination from the Qt installation used by your desktop before installing:

```sh
sh build.sh -DSCREENFX_BUILD_KWIN=ON \
    -DKDE_INSTALL_PLUGINDIR="$(qtpaths6 --query QT_INSTALL_PLUGINS)"
sudo cmake --install build/linux-release
```

On the tested Ubuntu system, `qtpaths6 --query QT_INSTALL_PLUGINS` returned `/usr/lib/x86_64-linux-gnu/qt6/plugins`, so the module destination becomes `/usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins/screenfx.so`. Use the Qt tools matching KWin if multiple Qt installations are present. The application and desktop launcher remain under the configured install prefix.

KWin loads the native module from its plugin paths. Setting `QT_PLUGIN_PATH` only when launching the ScreenFX panel does not change the environment of an already-running KWin. Qt documents how plugin discovery and version checks work in its [plugin deployment guide](https://doc.qt.io/qt-6/deployment-plugins.html); KDE documents the native effect namespace in its [KWin effects API](https://api.kde.org/kwin-effects.html).

After installation, launch ScreenFX from the application menu or run `screenfx`. If a newly installed module is not detected, log out and back in before retrying. After rebuilding an updated module, stop ScreenFX and start a fresh Plasma session to load the new binary.

For a user-local Hyprland installation:

```sh
sh build.sh -DSCREENFX_BUILD_KWIN=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --install build/linux-release
```

Ensure `~/.local/bin` is in your PATH for the desktop launcher. To uninstall, first stop and close ScreenFX. Review `build/linux-release/install_manifest.txt` from that installation and remove only its listed ScreenFX executable, desktop launcher and, if installed, `screenfx.so`. Keep the manifest if you intend to remove the build folder. Settings and custom presets are separate and can be retained. Restart the Plasma session after removing a loaded KWin module.

## Use the panel

Run this inside your desktop session:

```sh
./build/linux-release/screenfx
```

Check **Enable filter**, then adjust **Scanlines** and **RGB phosphor mask** for a CRT appearance. Effects start neutral when no settings have been saved. **Tint color** opens the color picker; selecting a color also enables tint if its strength was zero. **Reset effects** restores neutral values. Slider edits are grouped over 120 ms before updating the compositor shader; this delay applies to editing controls, not frame presentation.

**Stop filter** disables the effect. Closing the Linux panel stops the filter and exits when restoration succeeds. Launching a second instance brings the existing panel forward. The application starts stopped unless `--enable` is supplied.

| Command | Behavior |
| --- | --- |
| `screenfx --backend kde` | Select KDE explicitly instead of automatic detection. |
| `screenfx --backend hyprland` | Select Hyprland explicitly. |
| `screenfx --preset "My CRT" --enable` | Load an existing custom preset and enable it when the panel opens. |
| `screenfx --stop` | Stop the running filter through the desktop D-Bus session; a GUI connection is not required. |
| `screenfx --help` | Show the panel's command-line options. |

Use `--stop` as a separate command. If an instance is already running, a new normal launch only shows that panel; it does not apply new `--preset`, `--enable` or `--backend` arguments.

The KDE module registers **Ctrl+Alt+F12** as its default stop shortcut. On Hyprland, configure a compositor shortcut that runs `screenfx --stop`, or use the panel's stop button. The shortcut command must use the same desktop D-Bus session.

## Save settings and custom presets

Click **Save settings** to save current effects. To save a custom preset, enter its name and click **Save preset**. **Apply preset** restores saved effect values. Saving an existing name updates its effects, ignoring letter case. Up to 64 presets are supported, with names up to 80 UTF-16 code units.

Choose a saved preset and click **Delete preset** to remove it from the library immediately. This keeps the current effects and application settings unchanged. Deleting the last preset leaves an empty library.

Only explicit save or delete actions write their respective JSON files. Editing, applying, enabling, stopping and closing do not save settings or custom presets automatically.

The default paths are `~/.config/screenfx/settings.json` and `~/.config/screenfx/presets.json`; `XDG_CONFIG_HOME` overrides the configuration root. The versioned preset schema and effect names match the Windows files. Invalid JSON, duplicate keys, unsupported versions, invalid values and unsafe file paths are rejected; existing invalid files are preserved. To recover from a damaged file, close ScreenFX and rename it as a backup before launching again.

Hyprland also uses temporary shader files and a recovery journal under `$XDG_RUNTIME_DIR/screenfx`. These are runtime state, separate from custom presets. ScreenFX remembers the previous screen shader and damage setting, and restores them on stop. A small helper attempts restoration if the controller exits unexpectedly; a different shader selected after ScreenFX is preserved. `screenfx --stop` can also attempt recovery in the same Hyprland session. Permanent Hyprland configuration files are not edited.

## Validation and remaining desktop checks

The Linux Release application and native module were built under Ubuntu 26.04 in WSL with Qt 6.10.2 and KWin 6.6.6. CTest passed **4/4**:

- JSON settings and manual presets: roundtrips, validation, preservation and failed saves.
- Qt panel: real control edits, explicit saves, applying a preset, stop, close and loading saved settings on reopening.
- Mesa shader tests: GLES 3.0 for Hyprland and GLSL 1.40 for KWin, including identity, bypass, tint, CRT, spatial effects and animated grain pixels.
- Backend fixtures: D-Bus controller lifecycle, legacy and Lua Hyprland command arguments, escaped UTF-8 paths, shader reload, rollback, retrying a partial restoration, controller crash recovery and preservation of a newer shader. These use a simulated `hyprctl`, not a running Hyprland compositor. Provider detection and Lua syntax follow the official [hyprctl manual](https://github.com/hyprwm/Hyprland/blob/main/docs/hyprctl.1.rst) and [status response implementation](https://github.com/hyprwm/Hyprland/blob/main/src/helpers/SystemInfo.cpp).

An optional nested-KWin diagnostic harness is built with KWin support:

```sh
QT_QPA_PLATFORM=xcb sh tests/with-xvfb.sh dbus-run-session -- \
    build/linux-release/src/linux/screenfx_linux_desktop_tests "$PWD/build/linux-release"
```

The rendering checks require OpenGL composition and a parent display exposing a usable render device. Xvfb usually cannot provide that; this WSL environment also has no `/dev/dri`. Nested KWin used QPainter and the harness reported **SKIP**, native exit code 77. The command above therefore diagnoses the missing capability here; it is not a successful compositor rendering test or a substitute for testing the installed effect in a real session. Mesa's offscreen shader results do not establish that KWin's final output, output geometry or color handling is correct.

Before treating Linux desktop support as validated, run on physical KDE and Hyprland sessions and check output orientation and colors, enabling/stopping, emergency recovery, multiple monitors and display changes. No Linux end-to-end latency or GPU performance figures are claimed.
