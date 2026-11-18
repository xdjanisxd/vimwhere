#include "HintScanWorker.h"

#include <QPointer>
#include <utility>

using namespace HintScanProtocol;

HintScanWorker::HintScanWorker(QObject *parent)
    // The dev reload can replace the executable's path before stopping this
    // instance. Start the helper from the running executable's own inode.
    : HintScanWorker(QStringLiteral("/proc/self/exe"), {QStringLiteral("--hint-worker")}, parent) {}

HintScanWorker::HintScanWorker(QString program, QStringList arguments, QObject *parent) : QObject(parent) {
    m_process.setProgram(std::move(program));
    m_process.setArguments(std::move(arguments));
    m_process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(&m_process, &QProcess::started, this, &HintScanWorker::writeRequest);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &HintScanWorker::readReply);
    connect(&m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (m_discarding) {
            m_discarding = false;
            m_output.clear();
            scheduleNext();
        } else {
            const QPointer<HintScanWorker> self(this);
            readReply();
            if (!self) return;
            m_output.clear();
            finishActive({}, QStringLiteral("The AT-SPI hint worker exited unexpectedly"));
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_discarding = false;
            finishActive({}, QStringLiteral("Cannot start the AT-SPI hint worker: %1").arg(m_process.errorString()));
        } else if (!m_discarding && (error == QProcess::ReadError || error == QProcess::WriteError)) {
            const QString message = QStringLiteral("Cannot communicate with the AT-SPI hint worker: %1")
                .arg(m_process.errorString());
            discardProcess();
            finishActive({}, message);
        }
    });
}

HintScanWorker::~HintScanWorker() {
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() == QProcess::NotRunning) return;
    m_process.closeWriteChannel();
    if (!m_process.waitForFinished(50)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void HintScanWorker::warmUp(std::function<void(QString)> done) {
    prepare(std::move(done), true);
}

void HintScanWorker::prepare(std::function<void(QString)> done) {
    prepare(std::move(done), false);
}

void HintScanWorker::prepare(std::function<void(QString)> done, bool startup) {
    submit(Operation::Prepare, {}, [done = std::move(done)](const QJsonObject &reply, const QString &error) {
        const auto status = reply.value(QStringLiteral("error"));
        if (!error.isEmpty()) done(error);
        else if (status.isString()) done(status.toString());
        else done(QStringLiteral("Invalid accessibility status from the AT-SPI hint worker"));
    }, startup);
}

void HintScanWorker::probe(QJsonObject snapshot, std::function<void(TargetScanner::ProbeResult)> done) {
    submit(Operation::Probe, std::move(snapshot),
        [done = std::move(done)](const QJsonObject &reply, const QString &error) {
            if (auto result = decodeProbe(reply)) { done(std::move(*result)); return; }
            TargetScanner::ProbeResult failure;
            failure.status = TargetScanner::ProbeStatus::Failed;
            failure.error = error.isEmpty() ? QStringLiteral("Invalid probe result from the AT-SPI hint worker") : error;
            done(std::move(failure));
        });
}

void HintScanWorker::scan(QJsonObject snapshot, std::function<void(TargetScanner::Result)> done) {
    submit(Operation::Scan, std::move(snapshot),
        [done = std::move(done)](const QJsonObject &reply, const QString &error) {
            if (auto result = decodeScan(reply)) { done(std::move(*result)); return; }
            TargetScanner::Result failure;
            failure.error = error.isEmpty() ? QStringLiteral("Invalid scan result from the AT-SPI hint worker") : error;
            done(std::move(failure));
        });
}

void HintScanWorker::submit(Operation operation, QJsonObject snapshot, Completion completion, bool startup) {
    (startup ? m_startup : m_pending) = Work{
        {QString::number(++m_nextId), operation, std::move(snapshot)}, std::move(completion), startup};
    scheduleNext();
}

void HintScanWorker::scheduleNext() {
    if (m_startQueued || m_active || m_discarding || (!m_startup && !m_pending)) return;
    m_startQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_startQueued = false;
        startNext();
    }, Qt::QueuedConnection);
}

void HintScanWorker::startNext() {
    if (m_active || m_discarding || (!m_startup && !m_pending)) return;
    m_active = std::exchange(m_startup ? m_startup : m_pending, std::nullopt);
    if (m_process.state() == QProcess::Running) writeRequest();
    else m_process.start();
}

void HintScanWorker::writeRequest() {
    if (m_discarding || !m_active) return;
    const QByteArray request = encode(m_active->request);
    if (request.size() - 1 > MaximumMessageBytes || m_process.write(request) != request.size()) {
        discardProcess();
        finishActive({}, QStringLiteral("Cannot send a request to the AT-SPI hint worker"));
    }
}

void HintScanWorker::readReply() {
    const QByteArray output = m_process.readAllStandardOutput();
    if (m_discarding || output.isEmpty()) return;
    m_output.append(output);
    if (m_output.size() <= MaximumMessageBytes && !output.contains('\n')) return;

    QString error;
    const auto response = decodeResponse(std::exchange(m_output, {}), &error);
    if (!m_active || !response || response->id != m_active->request.id) {
        discardProcess();
        finishActive({}, error.isEmpty() ? QStringLiteral("Unexpected response from the AT-SPI hint worker") : error);
        return;
    }
    finishActive(response->result);
}

void HintScanWorker::finishActive(const QJsonObject &reply, const QString &error) {
    if (!m_active) { scheduleNext(); return; }
    Completion completion = std::move(m_active->completion);
    m_active.reset();
    scheduleNext();
    if (completion) completion(reply, error);
}

void HintScanWorker::discardProcess() {
    m_output.clear();
    if (m_process.state() == QProcess::NotRunning) return;
    m_discarding = true;
    m_process.kill();
}

void HintScanWorker::cancel() {
    m_pending.reset();
    if (!m_active || m_active->startup) return;
    m_active.reset();
    discardProcess();
    scheduleNext();
}
