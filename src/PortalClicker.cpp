#include "PortalClicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QDebug>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QUuid>

#include <cmath>

namespace {
constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto desktopPath = "/org/freedesktop/portal/desktop";
constexpr auto remoteInterface = "org.freedesktop.portal.RemoteDesktop";
constexpr auto screenInterface = "org.freedesktop.portal.ScreenCast";
constexpr auto requestInterface = "org.freedesktop.portal.Request";
constexpr auto sessionInterface = "org.freedesktop.portal.Session";
constexpr int leftButton = 0x110; // Linux BTN_LEFT

QString token() {
    return QStringLiteral("vimwhere_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
}

QVariant unwrap(QVariant value) {
    while (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = qvariant_cast<QDBusVariant>(value).variant();
    return value;
}

QString sessionPathFromVariant(const QVariant &raw) {
    const QVariant value = unwrap(raw);
    QString path;
    // RemoteDesktop.CreateSession's result is a D-Bus string, although it
    // contains an object path. The portal specification keeps `s` for ABI
    // compatibility. Other implementations may return a typed object path.
    if (value.metaType() == QMetaType::fromType<QString>())
        path = value.toString();
    else if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
        path = qvariant_cast<QDBusObjectPath>(value).path();
    static const QRegularExpression objectPath(
        QStringLiteral("^/[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*$"));
    return objectPath.match(path).hasMatch() ? path : QString();
}

bool pairFromVariant(const QVariant &raw, QPointF &point) {
    const QVariant value = unwrap(raw);
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return false;
    const auto argument = qvariant_cast<QDBusArgument>(value);
    if (argument.currentType() != QDBusArgument::StructureType)
        return false;
    int x = 0, y = 0;
    argument.beginStructure();
    argument >> x >> y;
    argument.endStructure();
    point = QPointF(x, y);
    return true;
}

template <typename Stream>
bool streamsFromVariant(const QVariant &raw, QVector<Stream> &streams) {
    const QVariant value = unwrap(raw);
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return false;
    const auto argument = qvariant_cast<QDBusArgument>(value);
    if (argument.currentType() != QDBusArgument::ArrayType)
        return false;
    argument.beginArray();
    while (!argument.atEnd()) {
        uint node = 0;
        QVariantMap properties;
        argument.beginStructure();
        argument >> node >> properties;
        argument.endStructure();
        // source_type was added after the original stream format. SelectSources
        // requests monitors only, so an older backend may omit this property.
        if (properties.contains(QStringLiteral("source_type")) &&
            unwrap(properties.value(QStringLiteral("source_type"))).toUInt() != 1)
            continue; // Absolute global positions only make sense for monitors.
        Stream stream;
        stream.node = node;
        stream.positioned = pairFromVariant(properties.value(QStringLiteral("position")), stream.position) &&
            pairFromVariant(properties.value(QStringLiteral("size")), stream.size) &&
            stream.size.x() > 0 && stream.size.y() > 0;
        if (stream.positioned) {
            stream.logicalSize = stream.size;
            QPointF logicalSize;
            if (pairFromVariant(properties.value(QStringLiteral("logical_size")), logicalSize) &&
                logicalSize.x() > 0 && logicalSize.y() > 0)
                stream.logicalSize = logicalSize;
        }
        streams.append(stream);
    }
    argument.endArray();
    return true;
}
} // namespace

PortalClicker::PortalClicker(QObject *parent) : QObject(parent) {}

PortalClicker::~PortalClicker() { closeSession(); }

bool PortalClicker::isReady() const { return ready_; }
QString PortalClicker::errorString() const { return error_; }

void PortalClicker::clickGlobal(const QPointF &point,
                                std::function<std::optional<QPointF>()> currentPointer) {
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
        emit clickFinished(false, QStringLiteral("Invalid click coordinates"));
        return;
    }
    if (!ready_) {
        emit clickFinished(false, error_.isEmpty()
            ? QStringLiteral("Portal pointer control is not ready; activate it and try again") : error_);
        return;
    }
    if (clickInFlight_) {
        emit clickFinished(false, QStringLiteral("A portal click is already in progress"));
        return;
    }
    dispatchClick(point, currentPointer);
}

void PortalClicker::prepare() {
    if (step_ == Step::Idle)
        beginSession();
}

void PortalClicker::beginSession() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        fail(QStringLiteral("Session D-Bus is unavailable"));
        return;
    }
    error_.clear();
    const QString sessionToken = token();
    QVariantMap options{{QStringLiteral("session_handle_token"), sessionToken}};
    request(QString::fromLatin1(remoteInterface), QStringLiteral("CreateSession"),
            {options}, Step::Creating);
}

void PortalClicker::request(const QString &interface, const QString &method,
                            const QVariantList &arguments, Step step) {
    auto bus = QDBusConnection::sessionBus();
    const QString requestToken = token();
    QString sender = bus.baseService();
    sender.remove(QLatin1Char(':'));
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    requestPath_ = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                       .arg(sender, requestToken);
    QVariantList args = arguments;
    QVariantMap options = args.takeLast().toMap();
    options.insert(QStringLiteral("handle_token"), requestToken);
    args.append(options);
    step_ = step;
    if (!bus.connect(QString::fromLatin1(service), requestPath_,
                     QString::fromLatin1(requestInterface), QStringLiteral("Response"),
                     this, SLOT(onRequestResponse(uint,QVariantMap)))) {
        fail(QStringLiteral("Could not subscribe to portal response"));
        return;
    }
    auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service),
                                                  QString::fromLatin1(desktopPath), interface, method);
    message.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    const QString expectedPath = requestPath_;
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, step, expectedPath, method] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (step_ != step || requestPath_ != expectedPath)
            return;
        if (reply.isError()) {
            fail(QStringLiteral("Portal %1 failed: %2")
                     .arg(method, reply.error().message()));
        } else if (reply.value().path() != expectedPath) {
            fail(QStringLiteral("Portal returned an unexpected request path"));
        }
    });
}

void PortalClicker::onRequestResponse(uint response, const QVariantMap &results) {
    QDBusConnection::sessionBus().disconnect(QString::fromLatin1(service), requestPath_,
        QString::fromLatin1(requestInterface), QStringLiteral("Response"),
        this, SLOT(onRequestResponse(uint,QVariantMap)));
    requestPath_.clear();
    if (response != 0) {
        fail(response == 1 ? QStringLiteral("Portal permission was cancelled or denied")
                           : QStringLiteral("Portal request failed (response %1)").arg(response));
        return;
    }
    switch (step_) {
    case Step::Creating: {
        const QString path = sessionPathFromVariant(results.value(QStringLiteral("session_handle")));
        if (path.isEmpty()) {
            fail(QStringLiteral("Portal did not return a session"));
            return;
        }
        sessionPath_ = path;
        QDBusConnection::sessionBus().connect(QString::fromLatin1(service), sessionPath_,
            QString::fromLatin1(sessionInterface), QStringLiteral("Closed"),
            this, SLOT(onSessionClosed()));
        request(QString::fromLatin1(remoteInterface), QStringLiteral("SelectDevices"),
                {QVariant::fromValue(QDBusObjectPath(sessionPath_)),
                 QVariantMap{{QStringLiteral("types"), uint(2)}}}, Step::SelectingDevices);
        break;
    }
    case Step::SelectingDevices:
        request(QString::fromLatin1(screenInterface), QStringLiteral("SelectSources"),
                {QVariant::fromValue(QDBusObjectPath(sessionPath_)),
                 QVariantMap{{QStringLiteral("types"), uint(1)},
                             {QStringLiteral("multiple"), true}}}, Step::SelectingSources);
        break;
    case Step::SelectingSources:
        request(QString::fromLatin1(remoteInterface), QStringLiteral("Start"),
                {QVariant::fromValue(QDBusObjectPath(sessionPath_)), QString(), QVariantMap{}},
                Step::Starting);
        break;
    case Step::Starting: {
        if (!(unwrap(results.value(QStringLiteral("devices"))).toUInt() & uint(2))) {
            fail(QStringLiteral("Portal did not grant pointer control"));
            return;
        }
        QVector<Stream> streams;
        if (!streamsFromVariant(results.value(QStringLiteral("streams")), streams) || streams.isEmpty()) {
            fail(QStringLiteral("Portal did not provide a monitor stream"));
            return;
        }
        streams_ = streams;
        step_ = Step::Ready;
        setReady(true);
        break;
    }
    default:
        fail(QStringLiteral("Unexpected portal response"));
    }
}

void PortalClicker::dispatchClick(const QPointF &point,
                                  const std::function<std::optional<QPointF>()> &currentPointer) {
    const Stream *chosen = nullptr;
    for (const Stream &stream : streams_) {
        if (stream.positioned && point.x() >= stream.position.x() && point.y() >= stream.position.y() &&
            point.x() < stream.position.x() + stream.size.x() &&
            point.y() < stream.position.y() + stream.size.y()) {
            chosen = &stream;
            break;
        }
    }
    if (!chosen) {
        const auto origin = currentPointer ? currentPointer() : std::nullopt;
        if (!origin || !std::isfinite(origin->x()) || !std::isfinite(origin->y())) {
            error_ = QStringLiteral("KWin pointer position is unavailable for portal movement");
            emit clickFinished(false, error_);
            return;
        }
        clickInFlight_ = true;
        auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service),
            QString::fromLatin1(desktopPath), QString::fromLatin1(remoteInterface),
            QStringLiteral("NotifyPointerMotion"));
        message.setArguments({QVariant::fromValue(QDBusObjectPath(sessionPath_)), QVariantMap{},
                              point.x() - origin->x(), point.y() - origin->y()});
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
        const QString session = sessionPath_;
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, watcher, session, point, currentPointer] {
            const QDBusPendingReply<> reply = *watcher;
            watcher->deleteLater();
            if (step_ != Step::Ready || sessionPath_ != session) return;
            if (reply.isError()) {
                clickInFlight_ = false;
                error_ = QStringLiteral("Pointer motion failed: %1").arg(reply.error().message());
                emit clickFinished(false, error_);
                return;
            }
            // The portal does not promise one-to-one relative motion. Observe
            // KWin's resulting cursor position before sending a click.
            verifyRelativeMotion(point, currentPointer, session, 20);
        });
        return;
    }
    bool geometryMatches = false;
    for (const QScreen *screen : QGuiApplication::screens()) {
        const QRect geometry = screen->geometry();
        if (geometry.x() == chosen->position.x() &&
            geometry.y() == chosen->position.y() &&
            geometry.width() == chosen->size.x() &&
            geometry.height() == chosen->size.y()) {
            geometryMatches = true;
            break;
        }
    }
    if (!geometryMatches) {
        error_ = QStringLiteral("Portal monitor geometry differs from the current desktop layout");
        emit clickFinished(false, error_);
        return;
    }
    clickInFlight_ = true;
    const QPointF position((point.x() - chosen->position.x()) * chosen->logicalSize.x() / chosen->size.x(),
                           (point.y() - chosen->position.y()) * chosen->logicalSize.y() / chosen->size.y());
    auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service),
        QString::fromLatin1(desktopPath), QString::fromLatin1(remoteInterface),
        QStringLiteral("NotifyPointerMotionAbsolute"));
    message.setArguments({QVariant::fromValue(QDBusObjectPath(sessionPath_)), QVariantMap{},
                          chosen->node, position.x(), position.y()});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    const QString session = sessionPath_;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, session] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (step_ != Step::Ready || sessionPath_ != session)
            return;
        if (reply.isError()) {
            clickInFlight_ = false;
            error_ = QStringLiteral("Pointer motion failed: %1").arg(reply.error().message());
            emit clickFinished(false, error_);
            return;
        }
        sendButton(1);
    });
}

void PortalClicker::verifyRelativeMotion(
    const QPointF &point, const std::function<std::optional<QPointF>()> &currentPointer,
    const QString &session, int attemptsLeft) {
    QTimer::singleShot(20, this, [this, point, currentPointer, session, attemptsLeft] {
        if (step_ != Step::Ready || sessionPath_ != session) return;
        const auto actual = currentPointer();
        if (actual && std::isfinite(actual->x()) && std::isfinite(actual->y()) &&
            std::hypot(actual->x() - point.x(), actual->y() - point.y()) <= 3.0) {
            sendButton(1);
        } else if (attemptsLeft > 1) {
            verifyRelativeMotion(point, currentPointer, session, attemptsLeft - 1);
        } else {
            qWarning() << "Portal relative motion ended at"
                       << (actual ? QVariant(*actual) : QVariant()) << "instead of" << point;
            clickInFlight_ = false;
            error_ = QStringLiteral("Portal pointer did not reach the selected target");
            emit clickFinished(false, error_);
        }
    });
}

void PortalClicker::sendButton(uint state) {
    auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service),
        QString::fromLatin1(desktopPath), QString::fromLatin1(remoteInterface),
        QStringLiteral("NotifyPointerButton"));
    message.setArguments({QVariant::fromValue(QDBusObjectPath(sessionPath_)), QVariantMap{},
                          leftButton, state});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    const QString session = sessionPath_;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, state, session] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (step_ != Step::Ready || sessionPath_ != session)
            return;
        if (state == 1 && !reply.isError()) {
            sendButton(0);
            return;
        }
        clickInFlight_ = false;
        error_ = reply.isError()
            ? QStringLiteral("Pointer button failed: %1").arg(reply.error().message()) : QString();
        emit clickFinished(!reply.isError(), error_);
        if (state == 0 && reply.isError()) {
            fail(error_); // Closing the session also releases any held pointer state.
            return;
        }
        if (state == 1 && reply.isError()) {
            // A failed call may still have reached the compositor. Release without
            // treating this safety action as a second click.
            auto release = QDBusMessage::createMethodCall(QString::fromLatin1(service),
                QString::fromLatin1(desktopPath), QString::fromLatin1(remoteInterface),
                QStringLiteral("NotifyPointerButton"));
            release.setArguments({QVariant::fromValue(QDBusObjectPath(sessionPath_)),
                                  QVariantMap{}, leftButton, uint(0)});
            QDBusConnection::sessionBus().asyncCall(release);
        }
    });
}

void PortalClicker::onSessionClosed() { fail(QStringLiteral("Portal session was closed")); }

void PortalClicker::setReady(bool ready) {
    if (ready_ == ready)
        return;
    ready_ = ready;
    emit readyChanged(ready);
}

void PortalClicker::closeSession() {
    if (!requestPath_.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(QString::fromLatin1(service), requestPath_,
            QString::fromLatin1(requestInterface), QStringLiteral("Response"),
            this, SLOT(onRequestResponse(uint,QVariantMap)));
        auto close = QDBusMessage::createMethodCall(QString::fromLatin1(service), requestPath_,
            QString::fromLatin1(requestInterface), QStringLiteral("Close"));
        QDBusConnection::sessionBus().asyncCall(close);
        requestPath_.clear();
    }
    if (sessionPath_.isEmpty())
        return;
    QDBusConnection::sessionBus().disconnect(QString::fromLatin1(service), sessionPath_,
        QString::fromLatin1(sessionInterface), QStringLiteral("Closed"), this, SLOT(onSessionClosed()));
    auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service), sessionPath_,
        QString::fromLatin1(sessionInterface), QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(message);
    sessionPath_.clear();
}

void PortalClicker::fail(const QString &message) {
    error_ = message;
    setReady(false);
    step_ = Step::Idle;
    streams_.clear();
    const bool wasClickInFlight = clickInFlight_;
    clickInFlight_ = false;
    closeSession();
    if (wasClickInFlight)
        emit clickFinished(false, message);
    emit sessionFailed(message);
}
