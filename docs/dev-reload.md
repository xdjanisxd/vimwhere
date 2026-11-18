# Development live reload

Opt in on Plasma Wayland to have `make -C build -j2` replace the running Vimwhere after a successful rebuild. The reload uses KDE's existing `vimwhere.desktop` autostart. It also requires the `libsystemd` development files and a running systemd user manager.

## Set up

Start with the ordinary `build/vimwhere` executable and an active desktop autostart. The desktop entry at `~/.config/autostart/vimwhere.desktop` should point `Exec` and `TryExec` at the absolute path to `build/vimwhere`. Set `Terminal=false` and `OnlyShowIn=KDE;`. Confirm KDE has started `app-vimwhere@autostart.service` before running setup.

From the repository root:

```sh
mkdir -p build
(cd build && qmake6 ../vimwhere.pro 'CONFIG+=vimwhere_live_reload')
make -C build -j2
tools/vimwhere-dev-reload install
```

Setup switches the existing autostart service to the development version. If that version fails to start, setup restores the original desktop autostart. Setup and reload run without opening a terminal window. The portal may ask for permission the next time you invoke a shortcut after a restart.

## Build and check status

Keep using `make -C build -j2`. A successful link publishes a complete executable atomically. It embeds the overlay QML and KWin JavaScript, so edits to either file also publish a version. A failed build or a build with nothing to do leaves the current application and its assets in place. Setup keeps the opt-in active when qmake regenerates the Makefile in `build`.

While Plasma Wayland is active, the user path unit picks up the latest queued build. It waits up to 15 seconds for the new process to register both shortcuts and receive its first valid KWin snapshot. If startup fails, the updater restores and starts the last working version with its embedded assets. For diagnostics:

```sh
systemctl --user status vimwhere-dev-reload.path app-vimwhere@autostart.service
journalctl --user -u vimwhere-dev-reload.service -u app-vimwhere@autostart.service -n 50
```

The opt-in build output is `build/.vimwhere-live/build/vimwhere`. The running version is stored in `build/.vimwhere-live/current/vimwhere`. Keep the `build` directory while reload is installed. `make -C build clean` leaves both the running version and the original `build/vimwhere` in place.

## Remove

From the same checkout, remove the watcher and return to the original desktop autostart:

```sh
tools/vimwhere-dev-reload remove
(cd build && qmake6 ../vimwhere.pro)
```

The next ordinary build updates `build/vimwhere`. The reload files under `build/.vimwhere-live` can be deleted after removal.
