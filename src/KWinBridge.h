#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <memory>

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
public slots:
    Q_SCRIPTABLE void Snapshot(const QString &json);
signals:
    void snapshotChanged();
    void readyChanged(bool ready);
private:
    QJsonObject m_snapshot;
    QString m_error;
    bool m_ready = false;
    bool m_loaded = false;
    std::unique_ptr<QTemporaryFile> m_scriptFile;
};
