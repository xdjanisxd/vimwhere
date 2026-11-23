#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJSEngine>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QFile script(QStringLiteral(VIMWHERE_SOURCE_DIR "/kwin/main.js"));
    if (!script.open(QIODevice::ReadOnly)) {
        qCritical() << script.errorString();
        return 1;
    }

    QJSEngine engine;
    auto evaluate = [&engine](const QString &source, const QString &fileName) {
        const QJSValue result = engine.evaluate(source, fileName);
        if (result.isError())
            qCritical().noquote() << result.property(QStringLiteral("stack")).toString() << result.toString();
        return !result.isError();
    };
    if (!evaluate(QStringLiteral(R"js(
        function signal() {
            let listener;
            return {
                connect: function(handler) { listener = handler; },
                emit: function() { if (listener) listener(); }
            };
        }
        const geometry = {x: 0, y: 0, width: 640, height: 480};
        const currentDesktop = {};
        var otherDesktop = {};
        var currentOutput = {name: "display", geometry: geometry, devicePixelRatio: 1};
        var otherOutput = {name: "second", geometry: geometry, devicePixelRatio: 1};
        var window = {
            internalId: "id", pid: 42, caption: "Original", opacity: 1,
            desktops: [currentDesktop], activities: [], output: currentOutput,
            frameGeometry: geometry, clientGeometry: geometry,
            captionChanged: signal(), opacityChanged: signal(), desktopsChanged: signal(),
            activitiesChanged: signal(), hiddenChanged: signal(), outputChanged: signal()
        };
        var overlay = {
            internalId: "vimwhere-overlay", pid: 43, caption: "", opacity: 1,
            desktops: [currentDesktop], activities: [], output: currentOutput,
            frameGeometry: geometry, clientGeometry: geometry, captionChanged: signal()
        };
        var workspace = {
            stackingOrder: [window, overlay], screens: [currentOutput, otherOutput], cursorPos: {x: 0, y: 0},
            currentActivity: "active",
            currentDesktopForScreen: function(output) {
                return output === currentOutput ? currentDesktop : otherDesktop;
            }
        };
        var windows;
        function callDBus(service, path, iface, method, json) {
            if (method === "Snapshot") windows = JSON.parse(json).windows;
        }
        function check(condition, message) {
            if (!condition) throw new Error(message);
        }
    )js"), QStringLiteral("KWin test fixture")) ||
        !evaluate(QString::fromUtf8(script.readAll()).replace(QStringLiteral("__VIMWHERE_PID__"), QStringLiteral("43")), script.fileName()))
        return 1;

    return evaluate(QStringLiteral(R"js(
        check(windows.length === 1 && windows[0].title === "Original", "Missing initial snapshot or overlay was included");
        overlay.pid = 44;
        overlay.captionChanged.emit();
        check(windows.length === 2, "An untitled window from another process must still occlude targets");
        overlay.caption = "Vimwhere overlay";
        overlay.captionChanged.emit();
        check(windows.length === 2 && windows[1].title === "Vimwhere overlay",
              "A different process with the overlay title was hidden");
        overlay.pid = 43;
        overlay.captionChanged.emit();
        check(windows.length === 1, "Vimwhere's own overlay returned to the snapshot");
        window.caption = "Renamed";
        window.captionChanged.emit();
        check(windows[0].title === "Renamed", "Caption-only change left a stale window title");
        window.opacity = 0.5;
        window.opacityChanged.emit();
        check(windows[0].opacity === 0.5, "Opacity-only change left stale occlusion data");
        window.hidden = true;
        window.hiddenChanged.emit();
        check(windows.length === 0, "Hidden window stayed in the snapshot");
        window.hidden = false;
        window.hiddenChanged.emit();
        check(windows.length === 1, "Unhidden window was missing");
        window.desktops = [otherDesktop];
        window.desktopsChanged.emit();
        check(windows.length === 0, "Window on another desktop stayed in the snapshot");
        window.output = otherOutput;
        window.outputChanged.emit();
        check(windows.length === 1, "Window was missing on its new output's current desktop");
        window.activities = ["inactive"];
        window.activitiesChanged.emit();
        check(windows.length === 0, "Window on another activity stayed in the snapshot");
    )js"), QStringLiteral("KWin snapshot assertions")) ? 0 : 1;
}
