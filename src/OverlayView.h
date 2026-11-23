#pragma once

#include <QQuickView>
#include <QVariantList>

class QScreen;

class OverlayView : public QQuickView {
    Q_OBJECT
public:
    explicit OverlayView(QScreen *screen);
    void setPresentation(const QString &prompt,
                         const QVariantList &hints, const QVariantList &grid);
    void setKeyboardOwner(bool owner);
signals:
    void keyInput(int key, const QString &text);
protected:
    void keyPressEvent(QKeyEvent *event) override;
};
