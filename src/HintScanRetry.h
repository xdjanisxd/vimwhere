#pragma once

#include "TargetScanner.h"

#include <QObject>
#include <QTimer>
#include <functional>

class HintScanRetry : public QObject {
public:
    using Scan = std::function<TargetScanner::Result()>;
    using Completion = std::function<void(TargetScanner::Result)>;
    using IsCurrent = std::function<bool()>;

    explicit HintScanRetry(QObject *parent = nullptr);
    void start(Scan scan, Completion completion, IsCurrent isCurrent);
    void cancel();
    bool isActive() const { return m_active; }

private:
    void scanNext();

    QTimer m_timer;
    Scan m_scan;
    Completion m_completion;
    IsCurrent m_isCurrent;
    TargetScanner::Result m_latest;
    int m_attempts = 0;
    bool m_active = false;
};
