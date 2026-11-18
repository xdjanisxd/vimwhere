#pragma once

#include <QObject>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <memory>
#include <optional>

class QTemporaryFile;

class KWinBridge : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.Vimwhere")
public:
    explicit KWinBridge(QObject *parent = nullptr);
    ~KWinBridge() override;
    bool start();
    bool isReady() const { return m_ready; }
    QString errorString() const { return m_error; }
    QJsonObject snapshot() const { return m_snapshot; }
    std::optional<QPointF> cursorPosition() const { return m_cursorPosition; }
public slots:
    Q_SCRIPTABLE void Snapshot(const QString &json);
    Q_SCRIPTABLE void CursorPosition(const QString &json);
signals:
    void snapshotChanged();
    void readyChanged(bool ready);
private:
    QJsonObject m_snapshot;
    std::optional<QPointF> m_cursorPosition;
    QString m_error;
    bool m_ready = false;
    bool m_loaded = false;
#ifndef VIMWHERE_LIVE_RELOAD
    std::unique_ptr<QTemporaryFile> m_scriptFile;
#endif
};
