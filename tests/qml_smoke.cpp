#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>

static bool checkVisibleText(const QQuickItem *root, QStringList expected) {
    if (root->flags().testFlag(QQuickItem::ItemHasContents)) {
        qCritical() << "Overlay root paints a background";
        return false;
    }
    QStringList actual;
    for (const QQuickItem *item : root->childItems()) {
        if (!item->isVisible() || !item->flags().testFlag(QQuickItem::ItemHasContents)) continue;
        const QString text = item->property("text").toString();
        if (text.isEmpty() || item->property("style").toInt() == 0) {
            qCritical() << "Expected outlined text, got" << item;
            return false;
        }
        if (!item->property("modelData").isValid() && item->x() >= root->width() / 2) {
            qCritical() << "Expected a left-aligned message";
            return false;
        }
        actual.append(text);
    }
    actual.sort();
    expected.sort();
    if (actual == expected) return true;
    qCritical() << "Expected visible text" << expected << "got" << actual;
    return false;
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(320, 240);
    view.setSource(QUrl::fromLocalFile(QStringLiteral(VIMWHERE_SOURCE_DIR "/qml/Overlay.qml")));
    if (view.status() != QQuickView::Ready || !view.rootObject()) {
        qCritical() << "Overlay QML failed to load" << view.errors();
        return 1;
    }
    const QVariantMap topLeft{{QStringLiteral("x"), 1}, {QStringLiteral("y"), 1},
                              {QStringLiteral("label"), QStringLiteral("AA")}};
    const QVariantMap middle{{QStringLiteral("x"), 160}, {QStringLiteral("y"), 120},
                             {QStringLiteral("label"), QStringLiteral("AB")}};
    const QVariantMap bottomRight{{QStringLiteral("x"), 319}, {QStringLiteral("y"), 239},
                                  {QStringLiteral("label"), QStringLiteral("AC")}};
    QQuickItem *root = view.rootObject();
    root->setProperty("hintItems", QVariantList{topLeft, middle, bottomRight});
    if (!checkVisibleText(root, {QStringLiteral("AA"), QStringLiteral("AB"), QStringLiteral("AC")}))
        return 2;

    const QRectF overlayBounds = root->boundingRect();
    for (const QQuickItem *item : root->childItems()) {
        const auto data = item->property("modelData").toMap();
        if (data.isEmpty()) continue;
        const QRectF bounds(item->position(), item->size());
        if (!overlayBounds.contains(bounds)) {
            qCritical() << "Hint label is clipped" << data << bounds << overlayBounds;
            return 3;
        }
        if (data == middle && bounds.center() != QPointF(160, 120)) {
            qCritical() << "Hint label is off center" << bounds;
            return 4;
        }
    }

    const QVariantMap gridCell{{QStringLiteral("x"), 10}, {QStringLiteral("y"), 20},
                               {QStringLiteral("width"), 100}, {QStringLiteral("height"), 80},
                               {QStringLiteral("label"), QStringLiteral("5")}};
    root->setProperty("hintItems", QVariantList{});
    root->setProperty("gridItems", QVariantList{gridCell});
    if (!checkVisibleText(root, {QStringLiteral("5")})) return 5;

    const QString gridMessage = QStringLiteral("Choose another cell");
    root->setProperty("prompt", gridMessage);
    if (!checkVisibleText(root, {QStringLiteral("5"), gridMessage})) return 6;

    const QString error = QStringLiteral("Pointer permission failed");
    root->setProperty("gridItems", QVariantList{});
    root->setProperty("prompt", error);
    if (!checkVisibleText(root, {error})) return 7;
    return 0;
}
