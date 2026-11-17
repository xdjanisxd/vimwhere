# Vimwhere

Vimwhere is a KDE Wayland desktop navigation prototype. It uses AT-SPI accessible targets for letter hints (`Ctrl+Alt+F`) and a narrowing screen grid for other UI (`Ctrl+Alt+G`). Global pointer clicks use the RemoteDesktop and ScreenCast desktop portal APIs. Live end-to-end behavior has not yet been verified in this environment.

## Build requirements

- KDE Plasma on Wayland with KWin scripting enabled
- Qt 6.5 or newer, including Core, Gui, Quick, Qml, and DBus
- KF6GlobalAccel and LayerShellQt
- AT-SPI development files discoverable by pkg-config as `atspi-2`
- qmake6 and a C++20 compiler (the repository also contains a CMake build definition)

Build out of tree with:

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

## Use

Run `./build/vimwhere` from the repository root in a native KDE Wayland session. The shortcuts default to `Ctrl+Alt+F` for hints and `Ctrl+Alt+G` for the grid. On first use, the selected shortcut starts the portal permission flow and does not select a target; grant access, then press the shortcut again. Hint mode schedules six AT-SPI scans with 80 ms event-loop intervals, giving accessibility trees about 400 ms to hydrate. It uses only the final scan's targets, so it never mixes coordinates from different snapshots. If access is denied, the app shows an error overlay. Both shortcuts can be configured through the QSettings keys `shortcuts/hints` and `shortcuts/grid` (defaults: `Ctrl+Alt+F` and `Ctrl+Alt+G`).

Run `./build/vimwhere --diagnose` to print the Qt platform, screen geometry and scale, session D-Bus state, KWin and portal service availability, AT-SPI initialization and desktop count, plus a reminder about portal consent. Its success exit code checks for a Wayland Qt platform, connected session D-Bus, and successful AT-SPI initialization; an already-initialized AT-SPI client is also considered available. It does not verify that KWin scripting or portal pointer control is usable.

The current implementation has limits: hint scanning only includes actionable AT-SPI roles, matches accessibility windows to KWin windows by process and title, and skips targets it considers covered or outside a screen. Grid mode supports up to nine displays: it first asks for a display number (1–9), then narrows with numbered 3×3 cells on that display. It has no click path for display gaps; Enter is ignored until a display has been chosen. Portal pointer control requires consent and positioned monitor streams. Panel coverage, accessibility coordinate alignment, mixed-scale monitor behavior, portal session reuse, focus restoration, and recovery after KWin restarts still need manual verification.

## Validation status

- qmake build and link: passed; binary is `build/vimwhere`.
- Navigation geometry checks: passed (900 cases, 26-to-27 hint label transition, 3×3 subdivision, and display-gap/intersection behavior).
- QML offscreen smoke run: passed.
- `qmllint qml/Overlay.qml` and `node --check kwin/main.js`: passed.
- `QT_QPA_PLATFORM=offscreen ./build/vimwhere --help`: passed.
- `--diagnose` under Qt's offscreen platform: exited 1; session D-Bus access was not permitted and AT-SPI was unavailable in this sandbox.

CMake is not available in the current environment. These build and smoke checks do not establish live KWin bridge, portal permission, or pointer injection behavior; end-to-end behavior on a KDE Wayland desktop remains unverified.

Use [the manual test checklist](docs/manual-testing.md) to record acceptance results on a running KDE Wayland desktop. Do not mark a check complete until it has been performed against the built application.
