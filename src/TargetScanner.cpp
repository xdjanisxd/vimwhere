#include "TargetScanner.h"

#include <QJsonArray>
#include <QRectF>
#include <QSet>
#include <atspi/atspi.h>

namespace {
QRectF rectangle(const QJsonValue &value) {
    const auto a = value.toArray();
    if (a.size() != 4) return {};
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}

QRectF atspiRectangle(AtspiAccessible *accessible) {
    AtspiComponent *component = atspi_accessible_get_component(accessible);
    if (!component) return {};
    GError *error = nullptr;
    AtspiRect *r = atspi_component_get_extents(component, ATSPI_COORD_TYPE_WINDOW, &error);
    QRectF result;
    if (r) {
        result = QRectF(r->x, r->y, r->width, r->height);
        g_free(r);
    }
    if (error) g_error_free(error);
    g_object_unref(component);
    return result;
}

QString accessibleName(AtspiAccessible *accessible) {
    GError *error = nullptr;
    gchar *name = atspi_accessible_get_name(accessible, &error);
    const QString result = name ? QString::fromUtf8(name) : QString();
    g_free(name);
    if (error) g_error_free(error);
    return result;
}

bool visible(AtspiAccessible *accessible) {
    AtspiStateSet *states = atspi_accessible_get_state_set(accessible);
    if (!states) return false;
    const bool result = atspi_state_set_contains(states, ATSPI_STATE_VISIBLE) &&
        atspi_state_set_contains(states, ATSPI_STATE_SHOWING) &&
        atspi_state_set_contains(states, ATSPI_STATE_SENSITIVE);
    g_object_unref(states);
    return result;
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

struct ScanContext {
    const QJsonArray &windows;
    const QJsonArray &screens;
    int windowIndex = -1;
    QRectF topExtents;
    QRectF client;
    int budget = 9000;
    QVector<HintTarget> *targets = nullptr;
    QSet<QString> seen;
};

bool onScreen(const QPointF &point, const QJsonArray &screens) {
    for (const auto &value : screens)
        if (rectangle(value.toObject().value(QStringLiteral("geometry"))).contains(point)) return true;
    return false;
}

bool unobscured(const QPointF &point, const ScanContext &context) {
    for (int i = context.windowIndex + 1; i < context.windows.size(); ++i) {
        const auto w = context.windows[i].toObject();
        if (w.value(QStringLiteral("desktop")).toBool() || w.value(QStringLiteral("opacity")).toDouble(1.0) <= 0.01)
            continue;
        if (rectangle(w.value(QStringLiteral("frame"))).contains(point)) return false;
    }
    return true;
}

void walk(AtspiAccessible *node, ScanContext &context, int depth) {
    if (!node || depth > 28 || context.budget-- <= 0) return;
    if (!visible(node)) return;
    GError *error = nullptr;
    const AtspiRole role = atspi_accessible_get_role(node, &error);
    if (error) { g_error_free(error); return; }
    if (actionable(role)) {
        const QRectF bounds = atspiRectangle(node);
        if (bounds.width() >= 5 && bounds.height() >= 5) {
            const QPointF point = context.client.topLeft() + bounds.center() - context.topExtents.topLeft();
            if (context.client.contains(point) && onScreen(point, context.screens) && unobscured(point, context)) {
                const QString key = QStringLiteral("%1:%2:%3").arg(qRound(point.x())).arg(qRound(point.y())).arg(role);
                if (!context.seen.contains(key)) {
                    context.seen.insert(key);
                    HintTarget target;
                    target.globalPoint = point;
                    target.name = accessibleName(node);
                    target.windowId = context.windows[context.windowIndex].toObject().value(QStringLiteral("id")).toString();
                    target.clientRect = context.client;
                    context.targets->push_back(target);
                }
            }
        }
    }
    int children = atspi_accessible_get_child_count(node, &error);
    if (error) { g_error_free(error); return; }
    children = qMin(children, 500);
    for (int i = 0; i < children && context.budget > 0; ++i) {
        error = nullptr;
        AtspiAccessible *child = atspi_accessible_get_child_at_index(node, i, &error);
        if (error) g_error_free(error);
        if (child) { walk(child, context, depth + 1); g_object_unref(child); }
    }
}
}

TargetScanner::Result TargetScanner::scan(const QJsonObject &snapshot) const {
    Result result;
    const auto windows = snapshot.value(QStringLiteral("windows")).toArray();
    const auto screens = snapshot.value(QStringLiteral("screens")).toArray();
    if (windows.isEmpty() || screens.isEmpty()) {
        result.error = QStringLiteral("No KWin window or screen geometry is available");
        return result;
    }
    if (atspi_init() != 0) {
        result.error = QStringLiteral("AT-SPI accessibility bus is unavailable");
        return result;
    }
    const int desktopCount = atspi_get_desktop_count();
    if (desktopCount <= 0) {
        result.error = QStringLiteral("AT-SPI has no desktop; enable accessibility support");
        return result;
    }
    for (int d = 0; d < desktopCount; ++d) {
        AtspiAccessible *desktop = atspi_get_desktop(d);
        if (!desktop) continue;
        GError *error = nullptr;
        int apps = atspi_accessible_get_child_count(desktop, &error);
        if (error) { g_error_free(error); g_object_unref(desktop); continue; }
        for (int a = 0; a < qMin(apps, 200); ++a) {
            error = nullptr;
            AtspiAccessible *app = atspi_accessible_get_child_at_index(desktop, a, &error);
            if (error) g_error_free(error);
            if (!app) continue;
            error = nullptr;
            const uint pid = atspi_accessible_get_process_id(app, &error);
            if (error) { g_error_free(error); g_object_unref(app); continue; }
            int topCount = atspi_accessible_get_child_count(app, &error);
            if (error) { g_error_free(error); g_object_unref(app); continue; }
            for (int t = 0; t < qMin(topCount, 100); ++t) {
                error = nullptr;
                AtspiAccessible *top = atspi_accessible_get_child_at_index(app, t, &error);
                if (error) g_error_free(error);
                if (!top) continue;
                const QString title = accessibleName(top);
                QVector<int> pidMatches, titleMatches;
                for (int i = 0; i < windows.size(); ++i) {
                    const auto w = windows[i].toObject();
                    if (w.value(QStringLiteral("pid")).toInt() != int(pid) ||
                        w.value(QStringLiteral("desktop")).toBool() ||
                        (w.value(QStringLiteral("special")).toBool() &&
                         !w.value(QStringLiteral("dock")).toBool() &&
                         !w.value(QStringLiteral("popup")).toBool())) continue;
                    pidMatches.push_back(i);
                    const QString caption = w.value(QStringLiteral("title")).toString();
                    if (!title.isEmpty() && (caption == title || caption.startsWith(title + QStringLiteral(" —")) ||
                        title.startsWith(caption + QStringLiteral(" —")))) titleMatches.push_back(i);
                }
                int match = -1;
                if (titleMatches.size() == 1) match = titleMatches.first();
                else if (pidMatches.size() == 1 && topCount == 1) match = pidMatches.first();
                // Panels and popups can share a process with many surfaces.
                // An ambiguous title must never become a clickable hint.
                if (match >= 0) {
                    const auto chosen = windows[match].toObject();
                    if ((chosen.value(QStringLiteral("special")).toBool() ||
                         chosen.value(QStringLiteral("popup")).toBool()) &&
                        titleMatches.size() != 1 && pidMatches.size() != 1)
                        match = -1;
                }
                if (match >= 0) {
                    ScanContext context{windows, screens};
                    context.windowIndex = match;
                    context.topExtents = atspiRectangle(top);
                    context.client = rectangle(windows[match].toObject().value(QStringLiteral("client")));
                    context.targets = &result.targets;
                    // A PID/title match alone is insufficient for shared-process
                    // panels and dialogs. Reject implausible surface dimensions.
                    const qreal dx = qAbs(context.topExtents.width() - context.client.width());
                    const qreal dy = qAbs(context.topExtents.height() - context.client.height());
                    const qreal toleranceX = qMax<qreal>(24, context.client.width() * 0.08);
                    const qreal toleranceY = qMax<qreal>(24, context.client.height() * 0.08);
                    if (context.topExtents.isValid() && context.client.isValid() &&
                        dx <= toleranceX && dy <= toleranceY) walk(top, context, 0);
                } else if (!pidMatches.isEmpty()) ++result.unmatchedWindows;
                g_object_unref(top);
            }
            g_object_unref(app);
        }
        g_object_unref(desktop);
    }
    if (result.targets.isEmpty())
        result.error = QStringLiteral("No safely positioned accessible targets found (window matching or AT-SPI coordinates may differ)");
    return result;
}
