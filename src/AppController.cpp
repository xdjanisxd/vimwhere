#include "AppController.h"
#include "OverlayView.h"
#include "NavigationMath.h"
#include "AccessibilityStatus.h"

#include <QAction>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QQuickItem>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>
#include <QDebug>
#include <KGlobalAccel>

AppController::AppController(QObject *parent) : QObject(parent), m_kwin(this), m_portal(this) {
    connect(&m_portal, &PortalClicker::clickFinished, this,
        [this](bool success, const QString &error) { if (!success) showError(error); });
    connect(&m_portal, &PortalClicker::sessionFailed, this,
        [this](const QString &error) { showError(QStringLiteral("Pointer permission failed: %1").arg(error)); });
    connect(qApp, &QGuiApplication::screenAdded, this, [this] {
        if (m_mode != Mode::Idle) { closeMode(); showError(QStringLiteral("Display layout changed; invoke the command again")); }
        else if (m_hintScanRetry.isActive()) m_hintScanRetry.cancel();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        if (m_mode != Mode::Idle) { closeMode(); showError(QStringLiteral("Display layout changed; invoke the command again")); }
        else if (m_hintScanRetry.isActive()) m_hintScanRetry.cancel();
    });
}

bool AppController::start() {
    if (QGuiApplication::platformName() != QStringLiteral("wayland")) {
        qCritical() << "Vimwhere requires a native Wayland Qt session";
        return false;
    }
    if (!m_kwin.start()) {
        qCritical().noquote() << m_kwin.errorString();
        return false;
    }
    QString accessibilityError;
    if (!AccessibilityStatus::enableForSession(&accessibilityError))
        qWarning().noquote() << "AT-SPI startup:" << accessibilityError;
    QSettings settings;
    const QKeySequence hintKey(settings.value(QStringLiteral("shortcuts/hints"),
        QStringLiteral("Ctrl+Alt+F")).toString());
    const QKeySequence gridKey(settings.value(QStringLiteral("shortcuts/grid"),
        QStringLiteral("Ctrl+Alt+G")).toString());
    m_hintAction = new QAction(QStringLiteral("Show Vimwhere hints"), this);
    m_hintAction->setObjectName(QStringLiteral("show-hints"));
    m_gridAction = new QAction(QStringLiteral("Show Vimwhere grid"), this);
    m_gridAction->setObjectName(QStringLiteral("show-grid"));
    const bool hintsRegistered = KGlobalAccel::setGlobalShortcut(m_hintAction, {hintKey});
    const bool gridRegistered = KGlobalAccel::setGlobalShortcut(m_gridAction, {gridKey});
    connect(m_hintAction, &QAction::triggered, this, &AppController::openHints);
    connect(m_gridAction, &QAction::triggered, this, &AppController::openGrid);
    qInfo() << "Vimwhere running; hints:" << KGlobalAccel::self()->shortcut(m_hintAction)
            << "grid:" << KGlobalAccel::self()->shortcut(m_gridAction);
    if (!hintsRegistered || !gridRegistered) {
        qWarning() << "Could not register both Vimwhere shortcuts";
#ifdef VIMWHERE_LIVE_RELOAD
        return false;
#endif
    }
#ifdef VIMWHERE_LIVE_RELOAD
    if (m_kwin.isReady()) emit startupReady();
    else connect(&m_kwin, &KWinBridge::readyChanged, this, [this](bool ready) {
        if (ready) emit startupReady();
    }, Qt::SingleShotConnection);
#endif
    return true;
}

void AppController::ensureOverlays() {
    const auto screens = QGuiApplication::screens();
    if (m_overlays.size() == screens.size()) {
        bool same = true;
        for (int i = 0; i < screens.size(); ++i)
            if (m_overlays[i]->screen() != screens[i]) same = false;
        if (same) return;
    }
    qDeleteAll(m_overlays);
    m_overlays.clear();
    for (QScreen *screen : screens) {
        auto *view = new OverlayView(screen);
        connect(view, &OverlayView::keyInput, this, &AppController::handleKey);
        m_overlays.push_back(view);
    }
}

void AppController::openHints() {
    closeMode();
    if (!m_portal.isReady()) {
        m_portal.prepare();
        qInfo() << "Complete the portal permission dialog, then press the hint shortcut again";
        return;
    }
    QString accessibilityError;
    if (!AccessibilityStatus::enableForSession(&accessibilityError)) {
        showError(accessibilityError);
        return;
    }
    if (!m_kwin.isReady()) {
        showError(QStringLiteral("KWin geometry bridge is not ready: %1").arg(m_kwin.errorString()));
        return;
    }
    const QJsonObject initialSnapshot = m_kwin.snapshot();
    const QString activeWindow = initialSnapshot.value(QStringLiteral("activeWindow")).toString();
    const QString activeScreen = initialSnapshot.value(QStringLiteral("activeScreen")).toString();
    QJsonValue activeClient, activeFrame;
    for (const auto &value : initialSnapshot.value(QStringLiteral("windows")).toArray()) {
        const auto window = value.toObject();
        if (window.value(QStringLiteral("id")).toString() == activeWindow) {
            activeClient = window.value(QStringLiteral("client"));
            activeFrame = window.value(QStringLiteral("frame"));
            break;
        }
    }
    m_hintScanRetry.start(
        [this] { return m_scanner.scan(m_kwin.snapshot()); },
        [this](TargetScanner::Result result) {
            if (!result.error.isEmpty()) { showError(result.error); return; }
            m_hints = std::move(result.targets);
            const int length = NavigationMath::hintCodeLength(m_hints.size());
            for (int i = 0; i < m_hints.size(); ++i)
                m_hints[i].label = NavigationMath::hintLabel(i, length);
            m_prefix.clear();
            m_mode = Mode::Hints;
            render();
        },
        [this, activeWindow, activeScreen, initialSnapshot, activeClient, activeFrame] {
            const QJsonObject current = m_kwin.snapshot();
            if (current.value(QStringLiteral("activeWindow")).toString() != activeWindow ||
                current.value(QStringLiteral("activeScreen")).toString() != activeScreen ||
                current.value(QStringLiteral("screens")) != initialSnapshot.value(QStringLiteral("screens")))
                return false;
            for (const auto &value : current.value(QStringLiteral("windows")).toArray()) {
                const auto window = value.toObject();
                if (window.value(QStringLiteral("id")).toString() == activeWindow)
                    return window.value(QStringLiteral("client")) == activeClient &&
                        window.value(QStringLiteral("frame")) == activeFrame;
            }
            return activeWindow.isEmpty() && activeClient.isUndefined() && activeFrame.isUndefined();
        });
}

void AppController::openGrid() {
    closeMode();
    if (!m_portal.isReady()) {
        m_portal.prepare();
        qInfo() << "Complete the portal permission dialog, then press the grid shortcut again";
        return;
    }
    m_screenLayout = screenLayout();
    if (m_screenLayout.isEmpty()) { showError(QStringLiteral("No display geometry is available")); return; }
    if (m_screenLayout.size() > 9) { showError(QStringLiteral("Grid supports at most nine displays")); return; }
    m_gridScreenSelect = true;
    m_gridHistory.clear();
    m_gridMessage.clear();
    m_mode = Mode::Grid;
    render();
}

void AppController::showError(const QString &error) {
    m_hintScanRetry.cancel();
    qWarning().noquote() << "Vimwhere:" << error;
    m_error = error;
    m_mode = Mode::Error;
    render();
}

void AppController::closeMode() {
    m_hintScanRetry.cancel();
    for (OverlayView *view : m_overlays) view->hide();
    m_mode = Mode::Idle;
    m_hints.clear();
    m_gridHistory.clear();
    m_gridMessage.clear();
    m_gridScreenSelect = false;
    m_prefix.clear();
    m_error.clear();
}

QVector<QRect> AppController::screenLayout() const {
    QVector<QRect> layout;
    for (QScreen *screen : QGuiApplication::screens()) layout.push_back(screen->geometry());
    return layout;
}

bool AppController::hintStillValid(const HintTarget &target) const {
    const auto windows = m_kwin.snapshot().value(QStringLiteral("windows")).toArray();
    int targetIndex = -1;
    for (int i = 0; i < windows.size(); ++i) {
        const auto w = windows[i].toObject();
        if (w.value(QStringLiteral("id")).toString() == target.windowId) {
            const auto a = w.value(QStringLiteral("client")).toArray();
            if (a.size() != 4 || QRectF(a[0].toDouble(), a[1].toDouble(),
                a[2].toDouble(), a[3].toDouble()) != target.clientRect) return false;
            targetIndex = i;
            break;
        }
    }
    if (targetIndex < 0) return false;
    for (int i = targetIndex + 1; i < windows.size(); ++i) {
        const auto w = windows[i].toObject();
        if (w.value(QStringLiteral("desktop")).toBool() || w.value(QStringLiteral("opacity")).toDouble(1.0) <= 0.01)
            continue;
        const auto a = w.value(QStringLiteral("frame")).toArray();
        if (a.size() == 4 && QRectF(a[0].toDouble(), a[1].toDouble(),
            a[2].toDouble(), a[3].toDouble()).contains(target.globalPoint)) return false;
    }
    return true;
}

void AppController::render() {
    if (m_mode == Mode::Idle) return;
    ensureOverlays();
    if (m_overlays.isEmpty()) { qWarning() << "No overlay screen"; return; }
    QString prompt;
    if (m_mode == Mode::Hints) prompt = QStringLiteral("Hints: %1   Esc cancel   Backspace undo").arg(m_prefix);
    else if (m_mode == Mode::Grid) prompt = m_gridScreenSelect
        ? QStringLiteral("Grid: choose display 1–%1   Esc cancel").arg(m_screenLayout.size())
        : QStringLiteral("Grid: 1–9 narrow   Enter click   Backspace undo   Esc cancel %1").arg(m_gridMessage);
    else prompt = QStringLiteral("Vimwhere error: %1   Esc close").arg(m_error);
    int focusIndex = 0;
    const QString active = m_kwin.snapshot().value(QStringLiteral("activeScreen")).toString();
    for (int i = 0; i < m_overlays.size(); ++i)
        if (m_overlays[i]->screen()->name() == active) focusIndex = i;
    for (int i = 0; i < m_overlays.size(); ++i) {
        OverlayView *view = m_overlays[i];
        const QRectF screen = view->screen()->geometry();
        QVariantList hints, grid;
        if (m_mode == Mode::Hints) {
            for (const auto &hint : m_hints) {
                if (!hint.label.startsWith(m_prefix) ||
                    !NavigationMath::containsGlobalPoint(view->screen()->geometry(), hint.globalPoint)) continue;
                QVariantMap item;
                const QPointF local = NavigationMath::screenLocalPoint(
                    hint.globalPoint, view->screen()->geometry());
                item.insert(QStringLiteral("x"), local.x());
                item.insert(QStringLiteral("y"), local.y());
                item.insert(QStringLiteral("label"), hint.label.mid(m_prefix.size()));
                hints << item;
            }
        } else if (m_mode == Mode::Grid) {
            if (m_gridScreenSelect) {
                QVariantMap item;
                item.insert(QStringLiteral("x"), 0);
                item.insert(QStringLiteral("y"), 0);
                item.insert(QStringLiteral("width"), screen.width());
                item.insert(QStringLiteral("height"), screen.height());
                item.insert(QStringLiteral("label"), QString::number(i + 1));
                grid << item;
            } else for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) {
                const QRectF cell = NavigationMath::gridCell(m_gridBounds, row * 3 + col + 1);
                const QRectF visible = cell.intersected(screen);
                if (visible.isEmpty()) continue;
                QVariantMap item;
                item.insert(QStringLiteral("x"), visible.x() - screen.x());
                item.insert(QStringLiteral("y"), visible.y() - screen.y());
                item.insert(QStringLiteral("width"), visible.width());
                item.insert(QStringLiteral("height"), visible.height());
                item.insert(QStringLiteral("label"), QString::number(row * 3 + col + 1));
                grid << item;
            }
        }
        view->setKeyboardOwner(i == focusIndex);
        view->setPresentation(m_mode == Mode::Hints ? QStringLiteral("hints") :
            m_mode == Mode::Grid ? QStringLiteral("grid") : QStringLiteral("error"), prompt, hints, grid);
        view->show();
    }
    m_overlays[focusIndex]->requestActivate();
}

void AppController::handleKey(int key, const QString &text) {
    if (key == Qt::Key_Escape) { closeMode(); return; }
    if (m_mode == Mode::Error) return;
    if (key == Qt::Key_Backspace) {
        if (m_mode == Mode::Hints && !m_prefix.isEmpty()) m_prefix.chop(1);
        else if (m_mode == Mode::Grid && !m_gridHistory.isEmpty()) {
            m_gridBounds = m_gridHistory.takeLast();
            m_gridMessage.clear();
        } else if (m_mode == Mode::Grid && !m_gridScreenSelect) {
            m_gridScreenSelect = true;
            m_gridMessage.clear();
        }
        render();
        return;
    }
    if (m_mode == Mode::Hints) {
        if (text.size() != 1 || !text[0].isLetter()) return;
        const QString candidate = m_prefix + text.toUpper();
        int exact = -1, matches = 0;
        for (int i = 0; i < m_hints.size(); ++i) {
            if (m_hints[i].label.startsWith(candidate)) ++matches;
            if (m_hints[i].label == candidate) exact = i;
        }
        if (!matches) return;
        m_prefix = candidate;
        if (exact >= 0) selectHint(exact);
        else render();
    } else if (m_mode == Mode::Grid) {
        if (m_gridScreenSelect) {
            if (text.size() != 1 || text[0] < QLatin1Char('1') || text[0] > QLatin1Char('9')) return;
            const int screenIndex = text[0].digitValue() - 1;
            if (screenIndex >= m_screenLayout.size()) return;
            m_gridBounds = QRectF(m_screenLayout[screenIndex]);
            m_gridScreenSelect = false;
            m_gridHistory.clear();
            render();
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { selectGrid(); return; }
        if (text.size() != 1 || text[0] < QLatin1Char('1') || text[0] > QLatin1Char('9')) return;
        const QRectF next = NavigationMath::gridCell(m_gridBounds, text[0].digitValue());
        if (NavigationMath::visibleParts(next, m_screenLayout).isEmpty()) {
            m_gridMessage = QStringLiteral("— empty cell; choose another"); render(); return;
        }
        m_gridHistory.push_back(m_gridBounds);
        m_gridBounds = next;
        m_gridMessage.clear();
        render();
    }
}

void AppController::selectHint(int index) {
    const HintTarget target = m_hints[index];
    closeMode();
    QTimer::singleShot(90, &m_portal, [this, target] {
        if (!hintStillValid(target)) {
            showError(QStringLiteral("Target window moved, closed, or became covered; select it again"));
            return;
        }
        m_portal.clickGlobal(target.globalPoint, [this] { return m_kwin.cursorPosition(); });
    });
}

void AppController::selectGrid() {
    if (m_gridScreenSelect) return;
    const QVector<QRectF> visibleParts = NavigationMath::visibleParts(m_gridBounds, m_screenLayout);
    if (visibleParts.isEmpty()) { showError(QStringLiteral("Selected grid cell is outside all displays")); return; }
    if (visibleParts.size() > 1) {
        m_gridMessage = QStringLiteral("— narrow to one display before Enter");
        render();
        return;
    }
    const QPointF point = visibleParts.first().center();
    closeMode();
    const auto layout = m_screenLayout;
    QTimer::singleShot(90, &m_portal, [this, point, layout] {
        if (layout != screenLayout()) {
            showError(QStringLiteral("Display layout changed; select the grid point again"));
            return;
        }
        m_portal.clickGlobal(point, [this] { return m_kwin.cursorPosition(); });
    });
}
