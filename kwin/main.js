// Loaded by the application through org.kde.kwin.Scripting. All coordinates
// below are KWin logical compositor coordinates, never physical buffer pixels.
function rect(r) { return [r.x, r.y, r.width, r.height]; }
function isOnVisibleDesktop(w) {
    if (w.minimized || w.hidden || w.deleted) return false;
    if (w.desktops.length) {
        const current = w.output ? workspace.currentDesktopForScreen(w.output) : workspace.currentDesktop;
        let found = false;
        for (const desktop of w.desktops) if (desktop === current) found = true;
        if (!found) return false;
    }
    if (w.activities.length) {
        let found = false;
        for (const activity of w.activities) if (activity === workspace.currentActivity) found = true;
        if (!found) return false;
    }
    return true;
}
function sendSnapshot() {
    const windows = [];
    const stack = workspace.stackingOrder;
    for (let i = 0; i < stack.length; ++i) {
        const w = stack[i];
        if (!w || !isOnVisibleDesktop(w)) continue;
        windows.push({
            id: String(w.internalId), pid: Number(w.pid), title: String(w.caption),
            frame: rect(w.frameGeometry), client: rect(w.clientGeometry),
            popup: Boolean(w.popupWindow), special: Boolean(w.specialWindow),
            dock: Boolean(w.dock), desktop: Boolean(w.desktopWindow), opacity: Number(w.opacity)
        });
    }
    const screens = [];
    for (const output of workspace.screens)
        screens.push({name: String(output.name), geometry: rect(output.geometry), scale: Number(output.devicePixelRatio)});
    callDBus("__VIMWHERE_BUS_NAME__", "/Vimwhere", "org.kde.Vimwhere", "Snapshot",
             JSON.stringify({windows: windows, screens: screens,
                 activeScreen: workspace.activeScreen ? String(workspace.activeScreen.name) : ""}));
}
sendSnapshot();
function connectIfAvailable(signal, handler) {
    if (signal && typeof signal.connect === "function") signal.connect(handler);
}
function watchWindow(w) {
    if (!w) return;
    connectIfAvailable(w.frameGeometryChanged, sendSnapshot);
    connectIfAvailable(w.clientGeometryChanged, sendSnapshot);
    connectIfAvailable(w.stackingOrderChanged, sendSnapshot);
    connectIfAvailable(w.windowShown, sendSnapshot);
    connectIfAvailable(w.windowHidden, sendSnapshot);
    connectIfAvailable(w.minimizedChanged, sendSnapshot);
}
for (const w of workspace.stackingOrder) watchWindow(w);
connectIfAvailable(workspace.windowAdded, function(w) { watchWindow(w); sendSnapshot(); });
connectIfAvailable(workspace.windowRemoved, sendSnapshot);
connectIfAvailable(workspace.windowActivated, sendSnapshot);
connectIfAvailable(workspace.virtualScreenGeometryChanged, sendSnapshot);
connectIfAvailable(workspace.currentDesktopChanged, sendSnapshot);
connectIfAvailable(workspace.currentActivityChanged, sendSnapshot);
connectIfAvailable(workspace.screensChanged, sendSnapshot);
