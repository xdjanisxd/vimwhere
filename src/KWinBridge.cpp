#include "KWinBridge.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QTemporaryFile>

KWinBridge::KWinBridge(QObject *parent) : QObject(parent) {}

KWinBridge::~KWinBridge() {
    if (!m_loaded) return;
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QDBusConnection::sessionBus());
    scripting.call(QStringLiteral("unloadScript"), QStringLiteral("vimwhere-runtime"));
}

bool KWinBridge::start() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_error = QStringLiteral("Session D-Bus is unavailable: %1").arg(bus.lastError().message());
        return false;
    }
    if (!bus.registerService(QStringLiteral("org.kde.Vimwhere"))) {
        m_error = QStringLiteral("Cannot own org.kde.Vimwhere: %1").arg(bus.lastError().message());
        return false;
    }
    if (!bus.registerObject(QStringLiteral("/Vimwhere"), this,
            QDBusConnection::ExportScriptableSlots)) {
        m_error = QStringLiteral("Cannot export KWin bridge: %1").arg(bus.lastError().message());
        return false;
    }
    QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
        QStringLiteral("vimwhere/kwin/main.js"));
    if (path.isEmpty()) path = QStringLiteral(VIMWHERE_SOURCE_DIR "/kwin/main.js");
    if (!QFileInfo::exists(path)) {
        m_error = QStringLiteral("KWin script was not found: %1").arg(path);
        return false;
    }
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), bus);
    if (!scripting.isValid()) {
        m_error = QStringLiteral("KWin scripting D-Bus interface unavailable: %1")
                      .arg(scripting.lastError().message());
        return false;
    }
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Cannot read KWin script: %1").arg(source.errorString());
        return false;
    }
    QByteArray scriptText = source.readAll();
    scriptText.replace("__VIMWHERE_BUS_NAME__", bus.baseService().toUtf8());
    m_scriptFile = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/vimwhere-XXXXXX.js"));
    if (!m_scriptFile->open() || m_scriptFile->write(scriptText) != scriptText.size() || !m_scriptFile->flush()) {
        m_error = QStringLiteral("Cannot stage KWin script in temporary storage");
        return false;
    }
    // A previous process may have crashed without unloading its runtime script.
    scripting.call(QStringLiteral("unloadScript"), QStringLiteral("vimwhere-runtime"));
    QDBusReply<int> loaded = scripting.call(QStringLiteral("loadScript"), m_scriptFile->fileName(),
        QStringLiteral("vimwhere-runtime"));
    if (!loaded.isValid() || loaded.value() < 0) {
        m_error = QStringLiteral("KWin refused script: %1").arg(loaded.error().message());
        return false;
    }
    m_loaded = true;
    QDBusInterface script(QStringLiteral("org.kde.KWin"),
        QStringLiteral("/Scripting/Script%1").arg(loaded.value()),
        QStringLiteral("org.kde.kwin.Script"), bus);
    QDBusMessage started = script.call(QStringLiteral("run"));
    if (started.type() == QDBusMessage::ErrorMessage) {
        m_error = QStringLiteral("KWin script failed to run: %1").arg(started.errorMessage());
        return false;
    }
    return true;
}

void KWinBridge::Snapshot(const QString &json) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        m_error = QStringLiteral("Invalid KWin snapshot: %1").arg(error.errorString());
        return;
    }
    const auto object = doc.object();
    if (!object.value(QStringLiteral("windows")).isArray() ||
        !object.value(QStringLiteral("screens")).isArray()) {
        m_error = QStringLiteral("Incomplete KWin snapshot");
        return;
    }
    m_snapshot = object;
    if (!m_ready) {
        m_ready = true;
        emit readyChanged(true);
    }
    emit snapshotChanged();
}
