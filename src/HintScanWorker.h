#pragma once

#include "HintScanProtocol.h"

#include <QObject>
#include <QProcess>
#include <optional>

// The helper keeps libatspi's GLib contexts and blocking calls out of the UI process.
class HintScanWorker : public QObject {
public:
    explicit HintScanWorker(QObject *parent = nullptr);
    HintScanWorker(QString program, QStringList arguments, QObject *parent = nullptr);
    ~HintScanWorker() override;

    void warmUp(std::function<void(QString)> done);
    void prepare(std::function<void(QString)> done);
    void probe(QJsonObject snapshot, std::function<void(TargetScanner::ProbeResult)> done);
    void scan(QJsonObject snapshot, std::function<void(TargetScanner::Result)> done);
    void cancel();

private:
    using Completion = std::function<void(const QJsonObject &, const QString &)>;
    struct Work {
        HintScanProtocol::Request request;
        Completion completion;
        bool startup;
    };

    void prepare(std::function<void(QString)> done, bool startup);
    void submit(HintScanProtocol::Operation operation, QJsonObject snapshot,
                Completion completion, bool startup = false);
    void scheduleNext();
    void startNext();
    void writeRequest();
    void readReply();
    void finishActive(const QJsonObject &reply, const QString &error = {});
    void discardProcess();

    QProcess m_process;
    QByteArray m_output;
    std::optional<Work> m_active;
    std::optional<Work> m_pending;
    std::optional<Work> m_startup;
    quint64 m_nextId = 0;
    bool m_startQueued = false;
    bool m_discarding = false;
};
