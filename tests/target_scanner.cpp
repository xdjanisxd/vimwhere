#include "../src/TargetScanner.h"
#include "../src/WindowMatcher.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QDebug>
#include <atspi/atspi.h>
#include <array>

// In-process AT-SPI provider: model Chromium's dormant tree and libatspi's
// cached attributes/null child without requiring a desktop or browser.
namespace {
std::array<GObject *, 7> objects;
bool active = false;
bool multiWindow = false;
bool sameSizeTop = false;
bool sameTitleTop = false;
bool cachedAttributes = true;
bool cachedNullChild = true;
bool attributeFailure = false;
bool delayedProvider = false;
bool zeroChildDelay = false;
bool partialTreeDelay = false;
bool busyRoot = false;
int activations = 0;
int attributeRequests = 0;
int cacheClears = 0;
int appTopListReads = 0;
int targetNameReads = 0;
int targetBoundsReads = 0;
int firstButtonX = 20;
int indexOf(const void *object) {
    for (int i = 0; i < int(objects.size()); ++i)
        if (objects[i] == object) return i;
    qFatal("Unexpected fixture object");
}
template<class T> T *ref(int i) { return reinterpret_cast<T *>(g_object_ref(objects[i])); }
QJsonObject snapshot(bool covered = false, bool wrongSize = false) {
    QJsonArray windows{QJsonObject{{"id", "fixture"}, {"pid", 42}, {"title", "Fixture"},
        {"client", QJsonArray{100, 100, wrongSize ? 900 : 400, 300}},
        {"frame", QJsonArray{100, 100, 400, 300}}}};
    if (covered) windows.append(QJsonObject{{"pid", 99}, {"frame", QJsonArray{100, 100, 400, 300}}});
    return {{"windows", windows}, {"screens", QJsonArray{QJsonObject{{"geometry", QJsonArray{0, 0, 1920, 1080}}}}}};
}
void check(bool condition, const char *message) { if (!condition) qFatal("%s", message); }
void reset(bool enabled = false) {
    active = enabled;
    multiWindow = false;
    sameSizeTop = false;
    sameTitleTop = false;
    cachedAttributes = true;
    cachedNullChild = !enabled;
    attributeFailure = false;
    delayedProvider = false;
    zeroChildDelay = false;
    partialTreeDelay = false;
    busyRoot = false;
    activations = 0;
    attributeRequests = 0;
    cacheClears = 0;
    appTopListReads = 0;
    targetNameReads = 0;
    targetBoundsReads = 0;
    firstButtonX = 20;
}
QJsonObject multiWindowSnapshot() {
    return {{"windows", QJsonArray{
        QJsonObject{{"id", "main"}, {"pid", 42}, {"title", "Different main title"},
            {"client", QJsonArray{100, 100, 400, 300}}, {"frame", QJsonArray{100, 100, 400, 300}}},
        QJsonObject{{"id", "popup"}, {"pid", 42}, {"title", "Different popup title"},
            {"special", true}, {"popup", true}, {"client", QJsonArray{600, 100, 470, 55}},
            {"frame", QJsonArray{600, 100, 470, 55}}}}},
        {"screens", QJsonArray{QJsonObject{{"geometry", QJsonArray{0, 0, 1920, 1080}}}}}};
}
}
extern "C" {
int atspi_init() { return 1; } // Already initialized is a successful scan.
gint atspi_get_desktop_count() { return 1; }
AtspiAccessible *atspi_get_desktop(gint) { return ref<AtspiAccessible>(0); }
gint atspi_accessible_get_child_count(AtspiAccessible *obj, GError **) {
    const int i = indexOf(obj);
    if (i == 0) return 1;
    if (i == 1) {
        ++appTopListReads;
        return multiWindow ? 2 : 1;
    }
    if (i == 2 && zeroChildDelay && attributeRequests < 2) return 0;
    if (i == 2 && partialTreeDelay && attributeRequests >= 3) return 2;
    if (i == 2 || i == 4) return 1;
    return 0;
}
AtspiAccessible *atspi_accessible_get_child_at_index(AtspiAccessible *obj, gint child, GError **) {
    const int i = indexOf(obj);
    if (child < 0) return nullptr;
    if (i == 0 && child == 0) return ref<AtspiAccessible>(1);
    if (i == 1) {
        if (child == 0) return ref<AtspiAccessible>(2);
        if (child == 1 && multiWindow) return ref<AtspiAccessible>(4);
        return nullptr;
    }
    if ((i == 2 || i == 4) && child == 0 && !(i == 2 && (!active || cachedNullChild)))
        return ref<AtspiAccessible>(i + 1);
    if (i == 2 && child == 1 && partialTreeDelay && attributeRequests >= 3)
        return ref<AtspiAccessible>(6);
    return nullptr;
}
guint atspi_accessible_get_process_id(AtspiAccessible *, GError **) { return 42; }
gchar *atspi_accessible_get_name(AtspiAccessible *obj, GError **) {
    const int i = indexOf(obj);
    if (i == 3 || i == 5 || i == 6) {
        ++targetNameReads;
        return g_strdup("Button");
    }
    return g_strdup(i == 4 && !sameTitleTop ? "Popup" : "Fixture");
}
AtspiRole atspi_accessible_get_role(AtspiAccessible *obj, GError **) {
    const int i = indexOf(obj);
    return i == 3 || i == 5 || i == 6 ? ATSPI_ROLE_PUSH_BUTTON : ATSPI_ROLE_FRAME;
}
AtspiComponent *atspi_accessible_get_component(AtspiAccessible *obj) { return ref<AtspiComponent>(indexOf(obj)); }
AtspiRect *atspi_component_get_extents(AtspiComponent *obj, AtspiCoordType type, GError **) {
    check(type == ATSPI_COORD_TYPE_WINDOW, "Expected window-relative geometry");
    auto *rect = g_new(AtspiRect, 1);
    const int i = indexOf(obj);
    if (i == 3 || i == 5 || i == 6) {
        ++targetBoundsReads;
        *rect = AtspiRect{i == 6 ? 220 : firstButtonX, 30, 80, 40};
    } else {
        *rect = i == 4 && !sameSizeTop ? AtspiRect{0, 0, 470, 55} : AtspiRect{0, 0, 400, 300};
    }
    return rect;
}
AtspiStateSet *atspi_accessible_get_state_set(AtspiAccessible *obj) { return ref<AtspiStateSet>(indexOf(obj)); }
gboolean atspi_state_set_contains(AtspiStateSet *states, AtspiStateType type) {
    return type == ATSPI_STATE_VISIBLE || type == ATSPI_STATE_SHOWING || type == ATSPI_STATE_SENSITIVE ||
        (type == ATSPI_STATE_BUSY && busyRoot && indexOf(states) == 2);
}
void atspi_accessible_clear_cache(AtspiAccessible *obj) {
    if (indexOf(obj) != 2) return;
    ++cacheClears;
    cachedAttributes = false;
    if (active && (!delayedProvider || attributeRequests >= 2)) cachedNullChild = false;
}
GHashTable *atspi_accessible_get_attributes(AtspiAccessible *obj, GError **error) {
    check(indexOf(obj) == 2, "Activate only the matched window");
    ++attributeRequests;
    if (attributeFailure) {
        g_set_error_literal(error, g_quark_from_static_string("fixture"), 1, "Unsupported attributes");
        return nullptr;
    }
    if (!cachedAttributes) {
        ++activations;
        active = true;
        // The placeholder remains cached even after the provider activates.
        cachedNullChild = true;
        cachedAttributes = true;
    }
    return g_hash_table_new(g_str_hash, g_str_equal);
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    for (auto &object : objects) object = G_OBJECT(g_object_new(G_TYPE_OBJECT, nullptr));
    TargetScanner scanner;
    reset();
    auto result = scanner.scan(snapshot());
    check(result.error.isEmpty() && result.targets.size() == 1,
          "Dormant tree must activate and discard its cached null child");
    check(result.targets[0].globalPoint == QPointF(160, 150), "Window-relative target must preserve placement");
    check(activations == 1, "Cached attributes must be refreshed to activate the provider");
    check(result.metrics.visitedNodes == 2 && targetNameReads == 0,
          "Read actionable geometry without requesting unused target names");
    reset();
    QJsonObject noProvider = snapshot();
    QJsonArray noProviderWindows = noProvider.value(QStringLiteral("windows")).toArray();
    QJsonObject otherProcess = noProviderWindows.first().toObject();
    otherProcess.insert(QStringLiteral("pid"), 77);
    noProviderWindows[0] = otherProcess;
    noProvider.insert(QStringLiteral("windows"), noProviderWindows);
    const auto noProviderResult = scanner.scan(noProvider);
    check(noProviderResult.targets.isEmpty() && noProviderResult.matchedWindows == 0 &&
              noProviderResult.error.contains(QStringLiteral("no matching application windows")),
          "A missing AT-SPI provider must be distinguished from unsafe geometry");
    check(appTopListReads == 0, "Skip applications with no eligible KWin window for their PID");
    check(scanner.probe(noProvider).status == TargetScanner::ProbeStatus::NeedsHydration,
          "A provider that has not registered yet may still appear during hydration");
    reset();
    multiWindow = true;
    const auto multiResult = scanner.scan(multiWindowSnapshot());
    check(multiResult.error.isEmpty() && multiResult.targets.size() == 1 &&
              multiResult.targets.first().windowId == QStringLiteral("main") &&
              multiResult.matchedWindows == 1,
          "A unique normal-window footprint resolves the main surface despite a same-PID popup");
    const QRectF mainExtent(0, 0, 1920, 1080);
    check(WindowMatcher::uniqueGeometryMatch(0, mainExtent,
              {{1, QRectF(0, 0, 1920, 1080)}}, {mainExtent, QRectF(0, 0, 470, 55)}) == 1,
          "The main-window footprint wins over a popup footprint");
    check(WindowMatcher::uniqueGeometryMatch(0, mainExtent,
              {{1, QRectF(0, 0, 1920, 1080)}, {2, QRectF(10, 10, 1920, 1080)}},
              {mainExtent}) == -1,
          "Two same-size windows remain ambiguous");
    check(WindowMatcher::uniqueGeometryMatch(0, mainExtent,
              {{1, QRectF(0, 0, 2880, 1620)}}, {mainExtent}) == -1,
          "A physical/logical scale mismatch is rejected instead of guessed");
    check(WindowMatcher::uniqueTopMatch(0, mainExtent, mainExtent,
              {mainExtent, QRectF(0, 0, 470, 55)}),
          "A small popup sibling does not make a matching main window ambiguous");
    check(!WindowMatcher::uniqueTopMatch(0, mainExtent, mainExtent, {mainExtent, mainExtent}),
          "Same-title roots with identical footprints cannot share one KWin window");
    reset();
    multiWindow = true;
    sameSizeTop = true;
    QJsonObject ambiguousWindows = snapshot();
    QJsonArray ambiguousList = ambiguousWindows.value(QStringLiteral("windows")).toArray();
    QJsonObject ambiguousCandidate = ambiguousList.first().toObject();
    ambiguousCandidate.insert(QStringLiteral("title"), QStringLiteral("Different title"));
    ambiguousList[0] = ambiguousCandidate;
    ambiguousWindows.insert(QStringLiteral("windows"), ambiguousList);
    const auto ambiguousResult = scanner.scan(ambiguousWindows);
    check(ambiguousResult.targets.isEmpty() && ambiguousResult.matchedWindows == 0,
          "Two same-PID AT-SPI roots cannot both map to one same-size KWin window");
    reset();
    multiWindow = true;
    sameSizeTop = true;
    sameTitleTop = true;
    const auto sameTitleResult = scanner.scan(snapshot());
    check(sameTitleResult.targets.isEmpty() && sameTitleResult.matchedWindows == 0,
          "Two same-title, same-size AT-SPI roots remain ambiguous against one KWin window");
    reset();
    delayedProvider = true;
    check(scanner.scan(snapshot()).targets.isEmpty(), "A late provider must be allowed to remain incomplete initially");
    auto lateResult = scanner.scan(snapshot());
    check(lateResult.targets.size() == 1,
          "A subsequent AT-SPI pass must discover children that appeared after activation");
    reset();
    zeroChildDelay = true;
    check(scanner.scan(snapshot()).targets.isEmpty(), "A dormant provider may initially report zero children");
    auto zeroChildLateResult = scanner.scan(snapshot());
    check(zeroChildLateResult.targets.size() == 1,
          "A later AT-SPI pass must discover a tree that initially reported zero children");
    reset();
    zeroChildDelay = true;
    using ProbeStatus = TargetScanner::ProbeStatus;
    const auto firstProbe = scanner.probe(snapshot());
    check(firstProbe.status == ProbeStatus::NeedsHydration && firstProbe.metrics.probedWindows == 1 &&
              firstProbe.metrics.visitedNodes == 0 && cacheClears == 2,
          "A probe must activate the root and discard its cached zero-child response");
    check(scanner.probe(snapshot()).status == ProbeStatus::Ready && targetBoundsReads == 0,
          "A second probe can see the late root child without reading target geometry");
    check(scanner.scan(snapshot()).targets.size() == 1 && targetBoundsReads == 1,
          "The final full scan alone collects target geometry after probes");
    reset();
    delayedProvider = true;
    check(scanner.probe(snapshot()).status == ProbeStatus::NeedsHydration,
          "A positive child count with a null child still needs hydration");
    check(scanner.probe(snapshot()).status == ProbeStatus::Ready,
          "A later probe observes the child once the provider publishes it");
    reset();
    partialTreeDelay = true;
    check(scanner.probe(snapshot()).status == ProbeStatus::Ready &&
              scanner.probe(snapshot()).status == ProbeStatus::Ready && targetBoundsReads == 0,
          "Identical healthy-looking shallow probes can precede the rest of the tree");
    firstButtonX = 60;
    const auto finalPass = scanner.scan(snapshot());
    check(finalPass.targets.size() == 2 && targetBoundsReads == 2 &&
              finalPass.targets.first().globalPoint == QPointF(200, 150),
          "Use the final tree and its current geometry, including late controls");
    reset();
    busyRoot = true;
    check(scanner.probe(snapshot()).status == ProbeStatus::NeedsHydration,
          "A busy root with children still needs hydration");
    check(scanner.probe({}).status == ProbeStatus::Failed,
          "Missing geometry fails the probe before discovery");
    reset(); // Simulate provider disabling itself between invocations.
    check(scanner.scan(snapshot()).targets.size() == 1, "Repeated invocation must reactivate a dormant provider");
    reset(true);
    attributeFailure = true;
    check(scanner.probe(snapshot()).status == ProbeStatus::NeedsHydration,
          "Unsupported activation requests do not certify a root as ready");
    check(scanner.scan(snapshot()).targets.size() == 1, "Unsupported attributes must not discard an active tree");
    reset();
    const auto unsafeGeometry = scanner.scan(snapshot(false, true));
    check(unsafeGeometry.targets.isEmpty() && unsafeGeometry.unmatchedWindows > 0 &&
              unsafeGeometry.error.contains(QStringLiteral("No safely positioned")),
          "Mismatched window geometry must remain rejected as unsafe");
    check(activations == 0, "Do not activate rejected geometry");
    check(scanner.probe(snapshot(false, true)).metrics.probedWindows == 0 && activations == 0,
          "Reject unsafe geometry before probing or activating the provider");
    reset();
    check(scanner.scan(snapshot(true)).targets.isEmpty(), "Covered targets must remain rejected");
    for (auto *object : objects) {
        check(object->ref_count == 1, "Scanner leaked an accessible/component/state reference");
        g_object_unref(object);
    }
    qInfo() << "Target scanner activation and safety checks passed";
}
