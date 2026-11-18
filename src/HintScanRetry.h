#pragma once

#include "TargetScanner.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <functional>

class HintScanRetry : public QObject {
public:
    using Done = std::function<void(TargetScanner::Result)>;
    using ProbeDone = std::function<void(TargetScanner::ProbeResult)>;
    using Probe = std::function<void(ProbeDone)>;
    using Scan = std::function<void(Done)>;
    using IsCurrent = std::function<bool()>;

    explicit HintScanRetry(QObject *parent = nullptr);
    void start(Probe probe, Scan scan, Done completion, IsCurrent isCurrent);
    void cancel();
    bool isActive() const { return m_active; }

private:
    bool isCurrent(quint64 generation);
    void advance();
    void complete(TargetScanner::Result result);

    QTimer m_timer;
    QElapsedTimer m_elapsed;
    Probe m_probe;
    Scan m_scan;
    Done m_completion;
    IsCurrent m_isCurrent;
    quint64 m_generation = 0;
    int m_probes = 0;
    bool m_active = false;
};
