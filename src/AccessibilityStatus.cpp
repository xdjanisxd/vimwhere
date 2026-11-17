#include "AccessibilityStatus.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QVariant>
#include <atspi/atspi.h>

namespace {
constexpr auto busService = "org.a11y.Bus";
constexpr auto busPath = "/org/a11y/bus";
constexpr auto statusInterface = "org.a11y.Status";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
constexpr int dbusTimeoutMs = 1500;

bool readEnabled(bool *enabled, QString *error) {
    QDBusInterface properties(QString::fromLatin1(busService), QString::fromLatin1(busPath),
                              QString::fromLatin1(propertiesInterface), QDBusConnection::sessionBus());
    properties.setTimeout(dbusTimeoutMs);
    const QDBusReply<QDBusVariant> reply = properties.call(QStringLiteral("Get"),
        QString::fromLatin1(statusInterface), QStringLiteral("IsEnabled"));
    if (!reply.isValid()) {
        if (error) *error = QStringLiteral("Cannot read AT-SPI IsEnabled status: %1")
            .arg(reply.error().message());
        return false;
    }
    *enabled = reply.value().variant().toBool();
    return true;
}

bool setEnabled(QString *error) {
    QDBusInterface properties(QString::fromLatin1(busService), QString::fromLatin1(busPath),
                              QString::fromLatin1(propertiesInterface), QDBusConnection::sessionBus());
    properties.setTimeout(dbusTimeoutMs);
    const QDBusReply<void> reply = properties.call(QStringLiteral("Set"),
        QString::fromLatin1(statusInterface), QStringLiteral("IsEnabled"),
        QVariant::fromValue(QDBusVariant(true)));
    if (reply.isValid()) return true;
    if (error) *error = QStringLiteral("Cannot enable AT-SPI for this session: %1")
        .arg(reply.error().message());
    return false;
}
}

bool AccessibilityStatus::ensureEnabled(const ReadEnabled &read, const SetEnabled &set,
                                        QString *error) {
    bool enabled = false;
    if (!read || !read(&enabled, error)) return false;
    if (enabled) return true;
    if (!set || !set(error)) return false;
    if (!read(&enabled, error)) return false;
    if (!enabled) {
        if (error) *error = QStringLiteral("AT-SPI remained disabled after the enable request");
        return false;
    }
    return true;
}

bool AccessibilityStatus::enableForSession(QString *error) {
    const int initStatus = atspi_init();
    if (initStatus != 0 && initStatus != 1) {
        if (error) *error = QStringLiteral("AT-SPI initialization failed (code %1)").arg(initStatus);
        return false;
    }
    return ensureEnabled(readEnabled, setEnabled, error);
}
