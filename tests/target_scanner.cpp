#include "../src/TargetScanner.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QDebug>
#include <atspi/atspi.h>
#include <array>

// In-process AT-SPI provider: model Chromium's dormant tree and libatspi's
// cached attributes/null child without requiring a desktop or browser.
namespace {
std::array<GObject *, 4> objects;
bool active = false;
bool cachedAttributes = true;
bool cachedNullChild = true;
bool attributeFailure = false;
int activations = 0;
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
    cachedAttributes = true;
    cachedNullChild = !enabled;
    attributeFailure = false;
    activations = 0;
}
}
extern "C" {
int atspi_init() { return 1; } // Already initialized is a successful scan.
gint atspi_get_desktop_count() { return 1; }
AtspiAccessible *atspi_get_desktop(gint) { return ref<AtspiAccessible>(0); }
gint atspi_accessible_get_child_count(AtspiAccessible *obj, GError **) { return indexOf(obj) < 3 ? 1 : 0; }
AtspiAccessible *atspi_accessible_get_child_at_index(AtspiAccessible *obj, gint child, GError **) {
    const int i = indexOf(obj);
    if (child != 0 || i == 3 || (i == 2 && (!active || cachedNullChild))) return nullptr;
    return ref<AtspiAccessible>(i + 1);
}
guint atspi_accessible_get_process_id(AtspiAccessible *, GError **) { return 42; }
gchar *atspi_accessible_get_name(AtspiAccessible *obj, GError **) {
    return g_strdup(indexOf(obj) == 3 ? "Button" : "Fixture");
}
AtspiRole atspi_accessible_get_role(AtspiAccessible *obj, GError **) {
    return indexOf(obj) == 3 ? ATSPI_ROLE_PUSH_BUTTON : ATSPI_ROLE_FRAME;
}
AtspiComponent *atspi_accessible_get_component(AtspiAccessible *obj) { return ref<AtspiComponent>(indexOf(obj)); }
AtspiRect *atspi_component_get_extents(AtspiComponent *obj, AtspiCoordType type, GError **) {
    check(type == ATSPI_COORD_TYPE_WINDOW, "Expected window-relative geometry");
    auto *rect = g_new(AtspiRect, 1);
    *rect = indexOf(obj) == 3 ? AtspiRect{20, 30, 80, 40} : AtspiRect{0, 0, 400, 300};
    return rect;
}
AtspiStateSet *atspi_accessible_get_state_set(AtspiAccessible *obj) { return ref<AtspiStateSet>(indexOf(obj)); }
gboolean atspi_state_set_contains(AtspiStateSet *, AtspiStateType) { return true; }
void atspi_accessible_clear_cache(AtspiAccessible *obj) {
    if (indexOf(obj) != 2) return;
    cachedAttributes = false;
    if (active) cachedNullChild = false;
}
GHashTable *atspi_accessible_get_attributes(AtspiAccessible *obj, GError **error) {
    check(indexOf(obj) == 2, "Activate only the matched window");
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
    reset(); // Simulate provider disabling itself between invocations.
    check(scanner.scan(snapshot()).targets.size() == 1, "Repeated invocation must reactivate a dormant provider");
    reset(true);
    attributeFailure = true;
    check(scanner.scan(snapshot()).targets.size() == 1, "Unsupported attributes must not discard an active tree");
    reset();
    check(scanner.scan(snapshot(false, true)).targets.isEmpty(), "Mismatched window geometry must remain rejected");
    check(activations == 0, "Do not activate rejected geometry");
    reset();
    check(scanner.scan(snapshot(true)).targets.isEmpty(), "Covered targets must remain rejected");
    for (auto *object : objects) {
        check(object->ref_count == 1, "Scanner leaked an accessible/component/state reference");
        g_object_unref(object);
    }
    qInfo() << "Target scanner activation and safety checks passed";
}
