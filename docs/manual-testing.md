# Manual testing

These are manual acceptance checks for the KDE Wayland desktop hint prototype. They are test procedures, not claims that the current build passes them. Record the date, KDE/Qt versions, monitor layout, and outcome for each run.

## Test setup

- Run in a KDE Plasma Wayland session.
- Have Dolphin and System Settings open, along with at least one other visible application and a panel.
- For multi-monitor checks, use two displays with different scale factors if available.
- Confirm the app's global shortcuts are available and note any shortcut conflicts before testing.
- Test both the accessibility hint mode (`Ctrl+Alt+F`) and the grid fallback (`Ctrl+Alt+G`).

## Checks

- [ ] **Dolphin accessibility hints:** Open a Dolphin window with the file view, toolbar, location bar, and side panel visible. Press `Ctrl+Alt+F`. Check that visible accessible controls receive usable hints. Select a toolbar action and a file-view item by typing their hints; confirm the intended target activates.
- [ ] **System Settings accessibility hints:** Open KDE System Settings on a page with controls and a navigation sidebar. Press `Ctrl+Alt+F`; select a sidebar entry and a visible control. Confirm each hint maps to the displayed target and activation performs the expected action.
- [ ] **All visible windows and panels:** Arrange two or more partially visible application windows and keep the KDE panel visible. Start accessibility hint mode and inspect the whole desktop. Confirm candidates include visible targets from every exposed window and any accessible panel controls, and exclude fully occluded content where it cannot be clicked. Panel coverage is not established; record which panel controls appear and whether selection works.
- [ ] **Overlapping windows:** Place a small window above controls in a larger window. In both modes, choose a target in the front window and then a visible target in the exposed part of the rear window. Confirm selection uses the actual topmost visible surface and never activates a covered target through the front window.
- [ ] **Grid fallback:** In an application area with no useful AT-SPI candidates, press `Ctrl+Alt+G`. First choose the numbered display shown on its overlay, then narrow with `1`–`9` cells and press Enter. Confirm the final point is where expected and produces one left click. Repeat near window edges and on a panel.
- [ ] **Multi-monitor placement:** With two monitors arranged side by side, start each mode and select targets on both monitors, including near the shared edge and outer corners. In grid mode, choose each display by its displayed number before narrowing. Confirm overlays and pointer/click coordinates use the correct display and do not jump or mirror across displays.
- [ ] **Display gaps and grid selection:** Arrange displays with a gap or offset. In grid mode, confirm the initial display choices correspond only to real displays, Enter does nothing before a display is chosen, and subsequent grid cells stay within the chosen display. Confirm no cell or click path targets the gap, and every successful Enter click lands on a real display.
- [ ] **Mixed display scaling:** Set different scale factors on two displays, log out/in if KDE requires it, then repeat hint and grid selection on both displays. Confirm displayed markers remain aligned with their targets at each scale.
- [ ] **Backspace behavior:** In accessibility mode, type part of a multi-character hint and press Backspace; confirm only the last typed character is removed and candidates update. In grid mode, choose a display, narrow at least two levels, and press Backspace; confirm it returns exactly one level. Backspace at the first grid level should return to display selection. Check Backspace with no input/history as well.
- [ ] **Escape cancellation:** Start each mode, make a partial selection, then press Esc. Confirm overlays disappear, no target activates or click occurs, and the desktop returns to its prior state. Repeat after a completed activation to check the next invocation starts cleanly.
- [ ] **Portal first consent and session reuse:** On a fresh portal session, press either global shortcut. Confirm it starts the permission flow without activating a target. Grant RemoteDesktop and screen capture access, then press the shortcut again and complete a selection. Repeat selections in both modes without restarting; confirm the existing consent/session is reused and each action produces only one click.
- [ ] **Portal denial:** On a fresh portal session, deny the request. Confirm the app displays an error, performs no selection/click, and leaves the desktop usable. Invoke the shortcut again and, if the portal allows it, grant access; confirm the documented retry flow can proceed.
- [ ] **Focus restoration:** Start a mode from Dolphin and then from System Settings. Cancel with Esc and complete one selection in each mode. Confirm keyboard focus returns to the appropriate originating window or selected target, and subsequent typing goes to that expected destination.
- [ ] **Repeated invocation and shortcut conflicts:** Invoke each shortcut, cancel, and invoke it again several times. Confirm no stale overlays remain, the two modes do not remain active together, and an already focused text field does not receive the shortcut's characters.
- [ ] **Configured shortcuts:** Change the QSettings keys `shortcuts/hints` and `shortcuts/grid` to non-default combinations, restart Vimwhere, and confirm the configured combinations invoke the matching modes while the defaults no longer invoke them. Check for collisions with KDE shortcuts.
- [ ] **KWin restart and cleanup:** Start Vimwhere, open a mode, cancel it, then restart KWin while the app is running. Check that the app does not use stale window geometry and record whether it recovers or needs a relaunch. Quit Vimwhere and check that its portal session closes, overlays disappear, and no orphaned KWin script keeps attempting callbacks; record any cleanup behavior that needs a follow-up fix.

## Hint latency

In a Plasma Wayland test session, quit any existing Vimwhere instance to free its shortcuts. Launch the build from the repository root with performance logging enabled.

```sh
QT_LOGGING_RULES='vimwhere.hints.performance.debug=true' ./build/vimwhere 2>&1 | tee /tmp/vimwhere-hint-latency.log
```

Grant portal consent first. Use the same visible windows, monitor layout, and focus for each comparison. Test a Dolphin window, a large Chromium page, and an Electron app with known interactive controls. For each workload, record an invocation after reopening the target application and several repeated invocations with the application still open. Press `Ctrl+Alt+F` for each sample, verify the expected controls have hints, and press Esc before the next sample.

- [ ] Group log entries by their `hint` invocation number. Record `elapsed_ms` for `phase=overlay-request` and `phase=first-frame` separately. Compare median and slow samples separately for first and repeated invocations. If the baseline lacks performance logs, film the shortcut press and overlay appearance together for both builds and count the frames between them.
- [ ] Record `phase=accessibility-startup` once per launch and `phase=accessibility`, `phase=probe`, and `phase=final-scan` timings for each invocation. Record matched windows, visited nodes, target count, and traversal limits. A cold run should show up to five shallow probes and a full scan. Repeat within ten seconds on the same layout and check for an immediate full scan. A drop in per-window target count should trigger hydration and another full scan. Compare hint coverage against the baseline and look for any late-loading or newly missing controls.
- [ ] Press `Ctrl+Alt+G` during hydration. Confirm grid mode stays visible without late hints. Press Esc to close it, then invoke hints again and verify the expected targets. Repeat with two quick presses of `Ctrl+Alt+F` and check that only the second invocation shows hints.

## Run record

| Date | Plasma / KWin | Qt | Displays / scale | Check | Result / notes |
| --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |
