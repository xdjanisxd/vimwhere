#include "../src/PortalClicker.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QVariantMap>
#include <QDebug>

// Compile the real decoder into this test translation unit so the socket-free
// check below exercises precisely the code used by the portal response slot.
#include "../src/PortalClicker.cpp"

struct IntPair { int x; int y; };
struct StreamTuple { uint node; QVariantMap properties; };
using StreamList = QList<StreamTuple>;
Q_DECLARE_METATYPE(IntPair)
Q_DECLARE_METATYPE(StreamTuple)
Q_DECLARE_METATYPE(StreamList)

QDBusArgument &operator<<(QDBusArgument &arg, const IntPair &pair) {
    arg.beginStructure(); arg << pair.x << pair.y; arg.endStructure(); return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, IntPair &pair) {
    arg.beginStructure(); arg >> pair.x >> pair.y; arg.endStructure(); return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const StreamTuple &stream) {
    arg.beginStructure(); arg << stream.node << stream.properties; arg.endStructure(); return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, StreamTuple &stream) {
    arg.beginStructure(); arg >> stream.node >> stream.properties; arg.endStructure(); return arg;
}

class FakePortal : public QObject, public QDBusContext {
    Q_OBJECT
public:
    using QObject::QObject;
    int selectDevicesCalls = 0;
    int selectSourcesCalls = 0;
    int startCalls = 0;

    QDBusObjectPath responsePath(const QString &sender, const QVariantMap &options,
                                 const QVariantMap &results) {
        QString normalized = sender;
        normalized.remove(QLatin1Char(':'));
        normalized.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                                 .arg(normalized, options.value(QStringLiteral("handle_token")).toString());
        QTimer::singleShot(0, this, [path, results] {
            auto signal = QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.portal.Request"),
                                                     QStringLiteral("Response"));
            signal.setArguments({uint(0), results});
            QDBusConnection::sessionBus().send(signal);
        });
        return QDBusObjectPath(path);
    }
};

class RemoteDesktop : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.RemoteDesktop")
public:
    explicit RemoteDesktop(FakePortal *portal) : QDBusAbstractAdaptor(portal), portal_(portal) {}
public slots:
    QDBusObjectPath CreateSession(const QVariantMap &options) {
        QString sender = portal_->message().service();
        QString normalized = sender;
        normalized.remove(QLatin1Char(':'));
        normalized.replace(QLatin1Char('.'), QLatin1Char('_'));
        // The success dictionary deliberately carries a D-Bus *string* (`s`),
        // whereas the method reply itself is an object path (`o`).
        const QString session = QStringLiteral("/org/freedesktop/portal/desktop/session/%1/%2")
                                    .arg(normalized,
                                         options.value(QStringLiteral("session_handle_token")).toString());
        return portal_->responsePath(sender, options,
                                     {{QStringLiteral("session_handle"), session}});
    }
    QDBusObjectPath SelectDevices(const QDBusObjectPath &, const QVariantMap &options) {
        ++portal_->selectDevicesCalls;
        return portal_->responsePath(portal_->message().service(), options, {});
    }
    QDBusObjectPath Start(const QDBusObjectPath &, const QString &, const QVariantMap &options) {
        ++portal_->startCalls;
        const QRect geometry = QGuiApplication::primaryScreen()->geometry();
        StreamTuple stream{42,
            {{QStringLiteral("source_type"), uint(1)},
             {QStringLiteral("position"), QVariant::fromValue(IntPair{geometry.x(), geometry.y()})},
             {QStringLiteral("size"), QVariant::fromValue(IntPair{geometry.width(), geometry.height()})}}};
        return portal_->responsePath(portal_->message().service(), options,
            {{QStringLiteral("devices"), uint(2)},
             {QStringLiteral("streams"), QVariant::fromValue(StreamList{stream})}});
    }
private:
    FakePortal *portal_;
};

class ScreenCast : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.ScreenCast")
public:
    explicit ScreenCast(FakePortal *portal) : QDBusAbstractAdaptor(portal), portal_(portal) {}
public slots:
    QDBusObjectPath SelectSources(const QDBusObjectPath &, const QVariantMap &options) {
        ++portal_->selectSourcesCalls;
        return portal_->responsePath(portal_->message().service(), options, {});
    }
private:
    FakePortal *portal_;
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--decode-only"))) {
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/session/1_42/vimwhere_abc");
        const QVariantMap wireResults{{QStringLiteral("session_handle"), path}};
        if (sessionPathFromVariant(wireResults.value(QStringLiteral("session_handle"))) != path ||
            sessionPathFromVariant(QVariant::fromValue(QDBusVariant(path))) != path ||
            sessionPathFromVariant(QVariant::fromValue(QDBusObjectPath(path))) != path ||
            !sessionPathFromVariant(QStringLiteral("not/an/object/path")).isEmpty()) {
            qCritical() << "Portal session handle decode regression failed";
            return 1;
        }
        qInfo() << "portal session handle decode checks passed";
        return 0;
    }
    qDBusRegisterMetaType<IntPair>();
    qDBusRegisterMetaType<StreamTuple>();
    qDBusRegisterMetaType<StreamList>();
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.registerService(QStringLiteral("org.freedesktop.portal.Desktop"))) {
        qCritical() << "Cannot start private test portal:" << bus.lastError().message();
        return 1;
    }
    FakePortal portal;
    RemoteDesktop remote(&portal);
    ScreenCast screen(&portal);
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &portal,
                            QDBusConnection::ExportAdaptors)) {
        qCritical() << "Cannot export test portal:" << bus.lastError().message();
        return 1;
    }
    PortalClicker clicker;
    bool ready = false;
    QString error;
    QObject::connect(&clicker, &PortalClicker::readyChanged, &app, [&](bool value) {
        if (value) { ready = true; app.quit(); }
    });
    QObject::connect(&clicker, &PortalClicker::sessionFailed, &app, [&](const QString &value) {
        error = value; app.quit();
    });
    QTimer::singleShot(5000, &app, [&] { error = QStringLiteral("timed out"); app.quit(); });
    clicker.prepare();
    app.exec();
    if (!ready || !clicker.isReady() || portal.selectDevicesCalls != 1 ||
        portal.selectSourcesCalls != 1 || portal.startCalls != 1) {
        qCritical() << "Portal string session regression failed:" << error
                    << portal.selectDevicesCalls << portal.selectSourcesCalls << portal.startCalls;
        return 1;
    }
    qInfo() << "portal string session regression passed";
    return 0;
}

#include "portal_session.moc"
