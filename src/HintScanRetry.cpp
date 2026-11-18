#include "HintScanRetry.h"

#include <QPointer>
#include <utility>

namespace {
constexpr int ProbeIntervalMs = 80;
constexpr int ProbeCount = 5;
}

HintScanRetry::HintScanRetry(QObject *parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &HintScanRetry::advance);
}

void HintScanRetry::start(Probe probe, Scan scan, Done completion, IsCurrent isCurrent) {
    cancel();
    m_probe = std::move(probe);
    m_scan = std::move(scan);
    m_completion = std::move(completion);
    m_isCurrent = std::move(isCurrent);
    m_probes = 0;
    m_active = true;
    m_elapsed.start();
    m_timer.start(0);
}

void HintScanRetry::cancel() {
    ++m_generation;
    m_timer.stop();
    m_active = false;
    m_probe = {};
    m_scan = {};
    m_completion = {};
    m_isCurrent = {};
}

bool HintScanRetry::isCurrent(quint64 generation) {
    if (!m_active || generation != m_generation) return false;
    if (m_isCurrent && m_isCurrent()) return true;
    cancel();
    return false;
}

void HintScanRetry::advance() {
    const quint64 generation = m_generation;
    if (!isCurrent(generation)) return;
    const QPointer<HintScanRetry> self(this);

    if (m_probes < ProbeCount) {
        // A shallow tree can appear complete before Chromium exposes its content.
        // Retain the full hydration window and use only the final collection.
        const Probe probe = m_probe;
        const int probeNumber = ++m_probes;
        probe([self, generation, probeNumber](TargetScanner::ProbeResult) {
            if (!self || !self->isCurrent(generation)) return;
            const qint64 remaining = probeNumber * ProbeIntervalMs - self->m_elapsed.elapsed();
            self->m_timer.start(int(qMax<qint64>(0, remaining)));
        });
        return;
    }

    const Scan scan = m_scan;
    scan([self, generation](TargetScanner::Result result) {
        if (!self || !self->isCurrent(generation)) return;
        self->complete(std::move(result));
    });
}

void HintScanRetry::complete(TargetScanner::Result result) {
    Done completion = std::move(m_completion);
    cancel();
    if (completion) completion(std::move(result));
}
