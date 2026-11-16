#pragma once

#include "KWinBridge.h"
#include "TargetScanner.h"
#include "PortalClicker.h"

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QRect>
#include <QVector>

class QAction;
class OverlayView;

class AppController : public QObject {
    Q_OBJECT
public:
    explicit AppController(QObject *parent = nullptr);
    bool start();
private:
    enum class Mode { Idle, Hints, Grid, Error };
    void openHints();
    void openGrid();
    void showError(const QString &error);
    void closeMode();
    void handleKey(int key, const QString &text);
    void render();
    void ensureOverlays();
    void selectHint(int index);
    void selectGrid();
    QVector<QRect> screenLayout() const;
    bool hintStillValid(const HintTarget &target) const;

    KWinBridge m_kwin;
    TargetScanner m_scanner;
    PortalClicker m_portal;
    QVector<OverlayView *> m_overlays;
    QVector<HintTarget> m_hints;
    QVector<QRectF> m_gridHistory;
    QRectF m_gridBounds;
    QString m_gridMessage;
    bool m_gridScreenSelect = false;
    QVector<QRect> m_screenLayout;
    QString m_prefix;
    QString m_error;
    Mode m_mode = Mode::Idle;
    QAction *m_hintAction = nullptr;
    QAction *m_gridAction = nullptr;
};
