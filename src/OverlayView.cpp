#include "OverlayView.h"

#include <QKeyEvent>
#include <QQmlContext>
#include <QQuickItem>
#include <QScreen>
#include <QStandardPaths>
#include <QFileInfo>
#include <LayerShellQt/window.h>

OverlayView::OverlayView(QScreen *screen) : QQuickView() {
    setScreen(screen);
    setTitle(QStringLiteral("Vimwhere overlay"));
    setFlags(Qt::FramelessWindowHint | Qt::Tool);
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    auto *shell = LayerShellQt::Window::get(this);
    shell->setScope(QStringLiteral("vimwhere"));
    shell->setLayer(LayerShellQt::Window::LayerOverlay);
    shell->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) | LayerShellQt::Window::AnchorBottom |
                      LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
    shell->setExclusiveZone(-1);
    shell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
#ifdef VIMWHERE_LIVE_RELOAD
    setSource(QUrl(QStringLiteral("qrc:/vimwhere/qml/Overlay.qml")));
#else
    QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
        QStringLiteral("vimwhere/qml/Overlay.qml"));
    if (path.isEmpty()) path = QStringLiteral(VIMWHERE_SOURCE_DIR "/qml/Overlay.qml");
    setSource(QUrl::fromLocalFile(path));
#endif
    // QQuickView can reset its screen while loading the QML source.
    setScreen(screen);
    // Keep the layer-shell output binding explicit after QQuickView loaded QML.
    shell->setScreen(screen);
}

void OverlayView::setPresentation(const QString &mode, const QString &prompt,
                                  const QVariantList &hints, const QVariantList &grid) {
    if (!rootObject()) return;
    rootObject()->setProperty("mode", mode);
    rootObject()->setProperty("prompt", prompt);
    rootObject()->setProperty("hintItems", hints);
    rootObject()->setProperty("gridItems", grid);
}

void OverlayView::setKeyboardOwner(bool owner) {
    auto *shell = LayerShellQt::Window::get(this);
    shell->setKeyboardInteractivity(owner ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                          : LayerShellQt::Window::KeyboardInteractivityNone);
}

void OverlayView::keyPressEvent(QKeyEvent *event) {
    emit keyInput(event->key(), event->text());
    event->accept();
}
