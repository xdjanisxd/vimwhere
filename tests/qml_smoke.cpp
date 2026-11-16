#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickView view;
    view.setSource(QUrl::fromLocalFile(QStringLiteral(VIMWHERE_SOURCE_DIR "/qml/Overlay.qml")));
    if (view.status() != QQuickView::Ready || !view.rootObject()) {
        qCritical() << "Overlay QML failed to load" << view.errors();
        return 1;
    }
    QVariantMap hint;
    hint.insert(QStringLiteral("x"), 100);
    hint.insert(QStringLiteral("y"), 100);
    hint.insert(QStringLiteral("label"), QStringLiteral("AA"));
    view.rootObject()->setProperty("mode", QStringLiteral("hints"));
    view.rootObject()->setProperty("prompt", QStringLiteral("Test"));
    view.rootObject()->setProperty("hintItems", QVariantList{hint});
    if (view.rootObject()->property("hintItems").toList().size() != 1) return 2;
    return 0;
}
