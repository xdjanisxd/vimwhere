# Vimwhere

Vimwhere is a KDE Wayland desktop navigation prototype. It uses AT-SPI accessible targets for letter hints (`Ctrl+Alt+F`) and a narrowing screen grid for other UI (`Ctrl+Alt+G`). Global pointer clicks use the RemoteDesktop and ScreenCast desktop portal APIs.

## Build requirements

- KDE Plasma on Wayland with KWin scripting enabled
- Qt 6.5 or newer, including Core, Gui, Quick, Qml, DBus, and Widgets
- KF6GlobalAccel and LayerShellQt
- AT-SPI development files discoverable by pkg-config as `atspi-2`
- CMake 3.20 or newer and a C++20 compiler (`qmake6` also works for development builds)

## Install for your user

From the checkout, run:

```sh
./tools/vimwhere-install
```

This builds and installs `~/.local/bin/vimwhere`, the overlay and KWin script under `${XDG_DATA_HOME:-$HOME/.local/share}/vimwhere`, and desktop entries for the Plasma application menu and login autostart. Both entries launch the installed executable by absolute path. The installed app runs independently of the checkout.

Open **Vimwhere** from the Plasma application menu to start it now. It starts automatically at subsequent Plasma logins. Choose **Çıkış** in the tray menu to stop Vimwhere for the current session. Use Plasma's Autostart settings to control whether it starts at login.

Rerun the installer after updating the checkout, then quit and reopen Vimwhere to use the newly installed version. Updates preserve your autostart preference. To uninstall, quit from the tray and run `./tools/vimwhere-install remove` from the checkout. This removes the installed files and desktop entries and preserves your Vimwhere settings.

## Development build

Build out of tree with qmake:

```sh
mkdir -p build
cd build
qmake6 ../vimwhere.pro
make -j2
```

This build and link completed successfully in the development workspace. If pkg-config cannot find `atspi-2`, add the directory containing its `.pc` file to `PKG_CONFIG_PATH` before building. For example:

```sh
export PKG_CONFIG_PATH="/usr/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
```

To restart the Plasma Wayland autostart after each successful development build, see [development live reload](docs/dev-reload.md).

## Use

Run Vimwhere in a native KDE Wayland session. Choose **İpuçlarını göster** in the tray menu or press `Ctrl+Alt+F` for hints. Choose **Grid’i aç** or press `Ctrl+Alt+G` for the grid. On first use, either action starts the portal permission flow. Grant access, then invoke the action again. On the first invocation, hint mode gives accessibility trees about 400 ms to populate. Invoking hints again within ten seconds on the same window layout starts a fresh scan immediately. If a previously covered window returns fewer targets, Vimwhere repeats the preparation and scans again. If access is denied, the app shows an error overlay. Both shortcuts can be configured through the QSettings keys `shortcuts/hints` and `shortcuts/grid` (defaults: `Ctrl+Alt+F` and `Ctrl+Alt+G`).

Run `~/.local/bin/vimwhere --diagnose` or `./build/vimwhere --diagnose` to print the Qt platform, screen geometry and scale, session D-Bus state, KWin and portal service availability, AT-SPI initialization and desktop count, plus a reminder about portal consent. Its success exit code checks for a Wayland Qt platform, connected session D-Bus, and successful AT-SPI initialization; an already-initialized AT-SPI client is also considered available. It does not verify that KWin scripting or portal pointer control is usable.

The current implementation has limits: hint scanning only includes actionable AT-SPI roles, matches accessibility windows to KWin windows by process and title, and skips targets it considers covered or outside a screen. Grid mode supports up to nine displays: it first asks for a display number (1–9), then narrows with numbered 3×3 cells on that display. It has no click path for display gaps; Enter is ignored until a display has been chosen. Portal pointer control requires consent and positioned monitor streams. Panel coverage, accessibility coordinate alignment, mixed-scale monitor behavior, portal session reuse, focus restoration, and recovery after KWin restarts still need manual verification.

## Validation status

- qmake build and link: passed; binary is `build/vimwhere`.
- Navigation geometry checks: passed (900 cases, 26-to-27 hint label transition, 3×3 subdivision, and display-gap/intersection behavior).
- QML offscreen smoke run: passed.
- `qmllint qml/Overlay.qml` and `node --check kwin/main.js`: passed.
- `QT_QPA_PLATFORM=offscreen ./build/vimwhere --help`: passed.
- `--diagnose` under Qt's offscreen platform: exited 1; session D-Bus access was not permitted and AT-SPI was unavailable in this sandbox.

Both build systems and all 10 CTest checks passed. On Plasma Wayland, the installed desktop entry launched Vimwhere, the tray exposed all three actions, and KDE registered both default shortcuts. A duplicate launch left the running instance in place. Quitting from the tray and reopening from the desktop entry worked. The systemd autostart generator produced a KDE login unit pointing to the installed executable. Interactive target selection and clicking still need manual verification.

Use [the manual test checklist](docs/manual-testing.md) to record acceptance results on a running KDE Wayland desktop. Do not mark a check complete until it has been performed against the built application.
