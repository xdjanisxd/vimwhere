#include "AppController.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QGuiApplication>
#include <QScreen>
#include <QTextStream>
#include <atspi/atspi.h>
#ifdef VIMWHERE_LIVE_RELOAD
#include <QDebug>
#include <systemd/sd-daemon.h>
#endif

static int diagnose() {
    QTextStream out(stdout);
    const bool wayland = QGuiApplication::platformName() == QStringLiteral("wayland");
    out << "Qt platform: " << QGuiApplication::platformName() << (wayland ? " (OK)" : " (requires wayland)") << Qt::endl;
    out << "Screens: " << QGuiApplication::screens().size() << Qt::endl;
    for (QScreen *screen : QGuiApplication::screens())
        out << "  " << screen->name() << " " << screen->geometry().x() << ","
            << screen->geometry().y() << " " << screen->geometry().width() << "x"
            << screen->geometry().height() << " scale=" << screen->devicePixelRatio() << Qt::endl;
    auto bus = QDBusConnection::sessionBus();
    out << "Session D-Bus: " << (bus.isConnected() ? "OK" : bus.lastError().message()) << Qt::endl;
    if (bus.isConnected() && bus.interface()) {
        for (const QString &service : {QStringLiteral("org.kde.KWin"),
                QStringLiteral("org.freedesktop.portal.Desktop")}) {
            auto present = bus.interface()->isServiceRegistered(service);
            out << service << ": " << (present.isValid() && present.value() ? "available" : "unavailable") << Qt::endl;
        }
    }
    const int atspi = atspi_init();
    out << "AT-SPI: " << (atspi == 0 || atspi == 1 ? "available" : "unavailable") << Qt::endl;
    if (atspi == 0 || atspi == 1) out << "AT-SPI desktops: " << atspi_get_desktop_count() << Qt::endl;
    out << "Input: RemoteDesktop and ScreenCast portal consent is required on first use" << Qt::endl;
    return wayland && bus.isConnected() && (atspi == 0 || atspi == 1) ? 0 : 1;
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Vimwhere"));
    QCoreApplication::setApplicationName(QStringLiteral("vimwhere"));
    if (app.arguments().contains(QStringLiteral("--diagnose"))) return diagnose();
    if (app.arguments().contains(QStringLiteral("--help"))) {
        QTextStream(stdout) << "vimwhere [--diagnose]\nCtrl+Alt+F: hints; Ctrl+Alt+G: grid\n";
        return 0;
    }
    app.setQuitOnLastWindowClosed(false);
    AppController controller;
#ifdef VIMWHERE_LIVE_RELOAD
    QObject::connect(&controller, &AppController::startupReady, &app, [] {
        const int notified = sd_notify(1, "READY=1\nSTATUS=Shortcuts registered and KWin snapshot received");
        if (notified < 0) qWarning() << "Could not send systemd readiness notification:" << notified;
    });
#endif
    if (!controller.start()) return 1;
    return app.exec();
}
