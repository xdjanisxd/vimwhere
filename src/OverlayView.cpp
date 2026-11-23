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
    // Keep the layer surface mapped so KWin does not animate hint content as
    // a newly opened window. It never handles pointer input; keyboard focus is
    // managed separately through LayerShellQt's keyboard interactivity.
    setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput);
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
#ifdef VIMWHERE_LIVE_RELOAD
    setSource(QUrl(QStringLiteral("qrc:/vimwhere/qml/Overlay.qml")));
#else
    QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
        QStringLiteral("vimwhere/qml/Overlay.qml"));
    if (path.isEmpty()) path = QStringLiteral(VIMWHERE_SOURCE_DIR "/qml/Overlay.qml");
    setSource(QUrl::fromLocalFile(path));
#endif
    // Qt chooses a virtual sibling from the window geometry when creating
    // the native surface. Position it on the intended output before get().
    setScreen(screen);
    setGeometry(screen->geometry());
    auto *shell = LayerShellQt::Window::get(this);
    // Older LayerShellQt releases take their output from QWindow::screen() by default.
    if (shell->metaObject()->indexOfProperty("screen") >= 0)
        shell->setProperty("screen", QVariant::fromValue(screen));
    shell->setScope(QStringLiteral("vimwhere"));
    shell->setLayer(LayerShellQt::Window::LayerOverlay);
    shell->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) | LayerShellQt::Window::AnchorBottom |
                      LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
    shell->setExclusiveZone(-1);
    shell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
}

void OverlayView::setPresentation(const QString &prompt,
                                  const QVariantList &hints, const QVariantList &grid) {
    if (!rootObject()) return;
    rootObject()->setProperty("prompt", prompt);
    rootObject()->setProperty("hintItems", hints);
    rootObject()->setProperty("gridItems", grid);
    // Layer-shell keyboard interactivity is double-buffered with the surface.
    // Keep persistent overlays updating after presentation changes so those
    // state requests are committed together with the rendered content.
    update();
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
