#pragma once

#include "KWinBridge.h"
#include "TargetScanner.h"
#include "HintScanRetry.h"
#include "HintScanWorker.h"
#include "PortalClicker.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QRect>
#include <QVector>
#include <optional>

class QAction;
class OverlayView;

class AppController : public QObject {
    Q_OBJECT
public:
    explicit AppController(QObject *parent = nullptr);
    bool start();
#ifdef VIMWHERE_LIVE_RELOAD
signals:
    void startupReady();
#endif
private:
    enum class Mode { Idle, Hints, Grid, Error };
    void openHints();
    void startHintScan();
    void cancelHintScan();
    bool isHintScanCurrent() const;
    void openGrid();
    void showError(const QString &error);
    void closeMode();
    void handleKey(int key, const QString &text);
    void render(bool measureHintPresentation = false);
    void ensureOverlays();
    void selectHint(int index);
    void selectGrid();
    QVector<QRect> screenLayout() const;
    bool hintStillValid(const HintTarget &target) const;

    KWinBridge m_kwin;
    HintScanRetry m_hintScanRetry;
    HintScanWorker m_hintScanWorker;
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
    std::optional<QJsonObject> m_hintLayout;
    QElapsedTimer m_hintOpenedAt;
    QMetaObject::Connection m_hintFrameConnection;
    quint64 m_hintInvocation = 0;
    int m_hintScanNumber = 0;
    int m_hintProbeNumber = 0;
    QAction *m_hintAction = nullptr;
    QAction *m_gridAction = nullptr;
};
