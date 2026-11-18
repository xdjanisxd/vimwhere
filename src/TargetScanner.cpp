#include "TargetScanner.h"
#include "WindowMatcher.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <atspi/atspi.h>
#include <memory>

namespace {
constexpr int MaximumApplications = 200;
constexpr int MaximumTopLevels = 100;
constexpr int MaximumDepth = 28;
constexpr int MaximumNodes = 9000;
constexpr int MaximumChildren = 500;
constexpr int MaximumProbeChildren = 16;
constexpr qint64 NanosecondsPerMillisecond = 1000000;

struct GObjectDeleter {
    void operator()(gpointer object) const { g_object_unref(object); }
};

template <typename T>
using GObjectPtr = std::unique_ptr<T, GObjectDeleter>;

enum class Pass { Probe, Scan };

struct ScanRun {
    TargetScanner::Result result;
    bool needsHydration = false;
    qint64 activationNs = 0;
    qint64 traversalNs = 0;
};

QRectF rectangle(const QJsonValue &value) {
    const auto a = value.toArray();
    if (a.size() != 4) return {};
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}

struct Window {
    QString id;
    QString title;
    QRectF client;
    QRectF frame;
    bool special;
    bool popup;
    bool occluding;
};

struct Scene {
    QVector<Window> windows;
    QVector<QRectF> screens;
    QHash<int, QVector<int>> windowsByPid;
};

Scene parseScene(const QJsonObject &snapshot) {
    Scene scene;
    for (const auto &value : snapshot.value(QStringLiteral("windows")).toArray()) {
        const auto window = value.toObject();
        const bool desktop = window.value(QStringLiteral("desktop")).toBool();
        const bool special = window.value(QStringLiteral("special")).toBool();
        const bool dock = window.value(QStringLiteral("dock")).toBool();
        const bool popup = window.value(QStringLiteral("popup")).toBool();
        if (!desktop && (!special || dock || popup))
            scene.windowsByPid[window.value(QStringLiteral("pid")).toInt()].push_back(scene.windows.size());
        scene.windows.push_back({
            window.value(QStringLiteral("id")).toString(),
            window.value(QStringLiteral("title")).toString(),
            rectangle(window.value(QStringLiteral("client"))),
            rectangle(window.value(QStringLiteral("frame"))),
            special, popup, !desktop && window.value(QStringLiteral("opacity")).toDouble(1.0) > 0.01
        });
    }
    for (const auto &value : snapshot.value(QStringLiteral("screens")).toArray())
        scene.screens.push_back(rectangle(value.toObject().value(QStringLiteral("geometry"))));
    return scene;
}

QRectF atspiRectangle(AtspiAccessible *accessible) {
    GObjectPtr<AtspiComponent> component(atspi_accessible_get_component(accessible));
    if (!component) return {};
    GError *error = nullptr;
    AtspiRect *bounds = atspi_component_get_extents(component.get(), ATSPI_COORD_TYPE_WINDOW, &error);
    QRectF result;
    if (bounds) {
        result = QRectF(bounds->x, bounds->y, bounds->width, bounds->height);
        g_free(bounds);
    }
    g_clear_error(&error);
    return result;
}

QString accessibleName(AtspiAccessible *accessible) {
    GError *error = nullptr;
    gchar *name = atspi_accessible_get_name(accessible, &error);
    const QString result = name ? QString::fromUtf8(name) : QString();
    g_free(name);
    g_clear_error(&error);
    return result;
}

int childCount(AtspiAccessible *accessible) {
    GError *error = nullptr;
    const int count = atspi_accessible_get_child_count(accessible, &error);
    if (!error) return count;
    g_error_free(error);
    return -1;
}

GObjectPtr<AtspiAccessible> childAt(AtspiAccessible *parent, int index) {
    GError *error = nullptr;
    GObjectPtr<AtspiAccessible> child(atspi_accessible_get_child_at_index(parent, index, &error));
    if (!error) return child;
    g_error_free(error);
    return {};
}

bool activateAccessibleTree(AtspiAccessible *top) {
    // Chromium/Electron may expose a frame with a null child until an AT
    // client requests extended properties. Reading names/roles alone does
    // not activate their accessibility tree.
    atspi_accessible_clear_cache(top);
    GError *error = nullptr;
    GHashTable *attributes = atspi_accessible_get_attributes(top, &error);
    const bool succeeded = attributes && !error;
    if (attributes) g_hash_table_unref(attributes);
    g_clear_error(&error);
    // Discard the placeholder child cached before activation.
    atspi_accessible_clear_cache(top);
    return succeeded;
}

bool probeChildren(AtspiAccessible *top) {
    const GObjectPtr<AtspiStateSet> states(atspi_accessible_get_state_set(top));
    const int children = childCount(top);
    if (children <= 0) return false;
    // A large shallow tree can require hundreds of calls. Leave its readiness
    // unknown and let the final scan traverse it under the existing limits.
    const int probeCount = qMin(children, MaximumProbeChildren);
    for (int i = 0; i < probeCount; ++i)
        if (!childAt(top, i)) return false;
    return children == probeCount && states &&
        !atspi_state_set_contains(states.get(), ATSPI_STATE_BUSY) &&
        !atspi_state_set_contains(states.get(), ATSPI_STATE_STALE) &&
        !atspi_state_set_contains(states.get(), ATSPI_STATE_DEFUNCT);
}

bool visible(AtspiAccessible *accessible) {
    const GObjectPtr<AtspiStateSet> states(atspi_accessible_get_state_set(accessible));
    return states && atspi_state_set_contains(states.get(), ATSPI_STATE_VISIBLE) &&
        atspi_state_set_contains(states.get(), ATSPI_STATE_SHOWING) &&
        atspi_state_set_contains(states.get(), ATSPI_STATE_SENSITIVE);
}

bool actionable(AtspiRole role) {
    switch (role) {
    case ATSPI_ROLE_PUSH_BUTTON: case ATSPI_ROLE_TOGGLE_BUTTON:
    case ATSPI_ROLE_CHECK_BOX: case ATSPI_ROLE_RADIO_BUTTON:
    case ATSPI_ROLE_LINK: case ATSPI_ROLE_MENU_ITEM:
    case ATSPI_ROLE_ICON: case ATSPI_ROLE_TABLE_CELL:
    case ATSPI_ROLE_COMBO_BOX: case ATSPI_ROLE_PAGE_TAB:
    case ATSPI_ROLE_LIST_ITEM: case ATSPI_ROLE_ENTRY:
        return true;
    default: return false;
    }
}

struct WalkContext {
    const Scene &scene;
    int windowIndex;
    QRectF topExtents;
    int remainingNodes = MaximumNodes;
    QSet<QString> seen = {};
};

bool onScreen(const QPointF &point, const Scene &scene) {
    for (const QRectF &screen : scene.screens)
        if (screen.contains(point)) return true;
    return false;
}

bool unobscured(const QPointF &point, const WalkContext &context) {
    for (int i = context.windowIndex + 1; i < context.scene.windows.size(); ++i) {
        const Window &window = context.scene.windows[i];
        if (window.occluding && window.frame.contains(point)) return false;
    }
    return true;
}

void walk(AtspiAccessible *node, WalkContext &context, ScanRun &run, int depth) {
    if (depth > MaximumDepth) {
        run.result.metrics.depthLimitReached = true;
        return;
    }
    if (context.remainingNodes <= 0) {
        run.result.metrics.nodeLimitReached = true;
        return;
    }
    --context.remainingNodes;
    ++run.result.metrics.visitedNodes;
    if (!visible(node)) return;
    GError *error = nullptr;
    const AtspiRole role = atspi_accessible_get_role(node, &error);
    if (error) { g_error_free(error); return; }
    if (actionable(role)) {
        const QRectF bounds = atspiRectangle(node);
        const Window &window = context.scene.windows[context.windowIndex];
        if (bounds.width() >= 5 && bounds.height() >= 5) {
            const QPointF point = window.client.topLeft() + bounds.center() - context.topExtents.topLeft();
            if (window.client.contains(point) && onScreen(point, context.scene) && unobscured(point, context)) {
                const QString key = QStringLiteral("%1:%2:%3").arg(qRound(point.x())).arg(qRound(point.y())).arg(role);
                if (!context.seen.contains(key)) {
                    context.seen.insert(key);
                    run.result.targets.push_back({point, window.id, {}, window.client});
                }
            }
        }
    }
    const int children = childCount(node);
    if (children > MaximumChildren) run.result.metrics.childLimitReached = true;
    const int boundedChildren = qMin(children, MaximumChildren);
    for (int i = 0; i < boundedChildren; ++i) {
        if (context.remainingNodes <= 0) {
            run.result.metrics.nodeLimitReached = true;
            break;
        }
        const auto child = childAt(node, i);
        if (child) walk(child.get(), context, run, depth + 1);
    }
}

void visitSurface(AtspiAccessible *top, const Scene &scene, int windowIndex,
                  const QRectF &topExtents, Pass pass, ScanRun &run) {
    QElapsedTimer timer;
    timer.start();
    const bool activated = activateAccessibleTree(top);
    run.activationNs += timer.nsecsElapsed();

    timer.restart();
    if (pass == Pass::Probe) {
        ++run.result.metrics.probedWindows;
        if (!probeChildren(top) || !activated) run.needsHydration = true;
    } else {
        WalkContext context{scene, windowIndex, topExtents};
        walk(top, context, run, 0);
    }
    run.traversalNs += timer.nsecsElapsed();
}

void visitApplication(AtspiAccessible *app, const Scene &scene, const QVector<int> &pidMatches,
                      Pass pass, ScanRun &run) {
    const int topCount = childCount(app);
    const int boundedTopCount = qMin(topCount, MaximumTopLevels);
    QVector<QRectF> topFootprints;
    int footprintSource = -1;
    auto ensureTopFootprints = [&](int topIndex, AtspiAccessible *top) {
        if (!topFootprints.isEmpty()) return;
        footprintSource = topIndex;
        topFootprints.reserve(boundedTopCount);
        for (int sibling = 0; sibling < boundedTopCount; ++sibling) {
            const auto other = sibling != topIndex ? childAt(app, sibling) : nullptr;
            AtspiAccessible *surface = sibling == topIndex ? top : other.get();
            topFootprints.push_back(surface ? atspiRectangle(surface) : QRectF());
        }
    };
    for (int t = 0; t < boundedTopCount; ++t) {
        const auto top = childAt(app, t);
        if (!top) continue;
        const QString title = accessibleName(top.get());
        QVector<int> titleMatches;
        for (int index : pidMatches) {
            const QString &caption = scene.windows[index].title;
            if (!title.isEmpty() && (caption == title || caption.startsWith(title + QStringLiteral(" —")) ||
                title.startsWith(caption + QStringLiteral(" —"))))
                titleMatches.push_back(index);
        }

        int match = -1;
        if (titleMatches.size() == 1) {
            ensureTopFootprints(t, top.get());
            const int candidate = titleMatches.first();
            if (WindowMatcher::uniqueTopMatch(t, topFootprints[t], scene.windows[candidate].client, topFootprints))
                match = candidate;
        } else if (pidMatches.size() == 1 && topCount == 1) {
            match = pidMatches.first();
        } else {
            ensureTopFootprints(t, top.get());
            QVector<WindowMatcher::Candidate> geometryCandidates;
            for (int index : pidMatches) {
                const Window &window = scene.windows[index];
                if (!window.special) geometryCandidates.push_back({index, window.client});
            }
            match = WindowMatcher::uniqueGeometryMatch(t, topFootprints[t], geometryCandidates, topFootprints);
        }
        // Panels and popups can share a process with many surfaces.
        // An ambiguous title must never become a clickable hint.
        if (match >= 0) {
            ++run.result.matchedWindows;
            const Window &window = scene.windows[match];
            if ((window.special || window.popup) && titleMatches.size() != 1 && pidMatches.size() != 1)
                match = -1;
        }
        if (match < 0) {
            ++run.result.unmatchedWindows;
            continue;
        }

        // Earlier sibling footprints serve matching. Recheck geometry on the
        // actual root before activating it or deriving target coordinates.
        const QRectF topExtents = pass == Pass::Probe && footprintSource == t
            ? topFootprints[t]
            : atspiRectangle(top.get());
        if (WindowMatcher::dimensionsMatch(topExtents, scene.windows[match].client))
            visitSurface(top.get(), scene, match, topExtents, pass, run);
    }
}

void visitDesktop(AtspiAccessible *desktop, const Scene &scene, Pass pass, ScanRun &run) {
    const int apps = qMin(childCount(desktop), MaximumApplications);
    for (int a = 0; a < apps; ++a) {
        const auto app = childAt(desktop, a);
        if (!app) continue;
        GError *error = nullptr;
        const uint pid = atspi_accessible_get_process_id(app.get(), &error);
        if (error) { g_error_free(error); continue; }
        const auto matches = scene.windowsByPid.constFind(int(pid));
        if (matches != scene.windowsByPid.cend()) visitApplication(app.get(), scene, *matches, pass, run);
    }
}

void visitScene(const QJsonObject &snapshot, Pass pass, ScanRun &run) {
    const Scene scene = parseScene(snapshot);
    if (scene.windows.isEmpty() || scene.screens.isEmpty()) {
        run.result.error = QStringLiteral("No KWin window or screen geometry is available");
        return;
    }

    // atspi_init() returns 1 when this process has already initialized it.
    const int initStatus = atspi_init();
    if (initStatus != 0 && initStatus != 1) {
        run.result.error = QStringLiteral("AT-SPI accessibility bus is unavailable");
        return;
    }
    const int desktopCount = atspi_get_desktop_count();
    if (desktopCount <= 0) {
        run.result.error = QStringLiteral("AT-SPI has no desktop; enable accessibility support");
        return;
    }
    for (int d = 0; d < desktopCount; ++d) {
        const GObjectPtr<AtspiAccessible> desktop(atspi_get_desktop(d));
        if (desktop) visitDesktop(desktop.get(), scene, pass, run);
    }
}

ScanRun runPass(const QJsonObject &snapshot, Pass pass) {
    ScanRun run;
    QElapsedTimer timer;
    timer.start();
    visitScene(snapshot, pass, run);
    const qint64 totalNs = timer.nsecsElapsed();
    auto &metrics = run.result.metrics;
    metrics.matchingMs = (totalNs - run.activationNs - run.traversalNs) / NanosecondsPerMillisecond;
    metrics.activationMs = run.activationNs / NanosecondsPerMillisecond;
    metrics.traversalMs = run.traversalNs / NanosecondsPerMillisecond;
    return run;
}
}

TargetScanner::ProbeResult TargetScanner::probe(const QJsonObject &snapshot) const {
    auto run = runPass(snapshot, Pass::Probe);
    const auto &result = run.result;
    ProbeStatus status = ProbeStatus::NeedsHydration;
    if (!result.error.isEmpty()) status = ProbeStatus::Failed;
    else if (result.metrics.probedWindows > 0 && !run.needsHydration &&
             result.metrics.probedWindows == result.matchedWindows && result.unmatchedWindows == 0)
        status = ProbeStatus::Ready;
    return {status, result.error, result.unmatchedWindows, result.matchedWindows, result.metrics};
}

TargetScanner::Result TargetScanner::scan(const QJsonObject &snapshot) const {
    auto result = runPass(snapshot, Pass::Scan).result;
    if (!result.error.isEmpty() || !result.targets.isEmpty()) return result;
    if (result.matchedWindows == 0 && result.unmatchedWindows == 0)
        result.error = QStringLiteral("AT-SPI exposed no matching application windows. The session accessibility bus is enabled; close and reopen the target application, then try again.");
    else
        result.error = QStringLiteral("No safely positioned accessible targets found (window matching, occlusion, or AT-SPI coordinates may differ)");
    return result;
}
