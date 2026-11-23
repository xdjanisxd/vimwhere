#include "HintScanRetry.h"

#include <QPointer>
#include <utility>

namespace {
constexpr int ProbeIntervalMs = 80;
constexpr int ProbeCount = 5;
constexpr int HydrationMs = ProbeCount * ProbeIntervalMs;
constexpr int WarmLayoutMs = 10000;

QHash<QString, int> countsByWindow(const TargetScanner::Result &result) {
    QHash<QString, int> counts;
    for (const auto &target : result.targets) ++counts[target.windowId];
    return counts;
}
}

HintScanRetry::HintScanRetry(QObject *parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &HintScanRetry::advance);
}

void HintScanRetry::start(Probe probe, Scan scan, Done completion, IsCurrent isCurrent,
                          QJsonObject layout) {
    cancel();
    m_probe = std::move(probe);
    m_scan = std::move(scan);
    m_completion = std::move(completion);
    m_isCurrent = std::move(isCurrent);
    m_layout = std::move(layout);
    m_tryImmediateScan = !m_layout.isEmpty() && m_layout == m_previousLayout &&
        m_lastSuccess.isValid() && !m_lastSuccess.hasExpired(WarmLayoutMs);
    m_startedHydration = false;
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

    if (!m_tryImmediateScan && (!m_startedHydration || m_elapsed.elapsed() < HydrationMs)) {
        // A shallow tree can appear complete before Chromium exposes its content.
        // Retain the full hydration window and use only the final collection.
        const Probe probe = m_probe;
        m_startedHydration = true;
        probe([self, generation](TargetScanner::ProbeResult) {
            if (!self || !self->isCurrent(generation)) return;
            const qint64 elapsed = self->m_elapsed.elapsed();
            // A slow provider can consume several intervals in one request.
            // Skip missed probes instead of issuing them back-to-back.
            const int delay = elapsed >= HydrationMs ? 0 : ProbeIntervalMs - int(elapsed % ProbeIntervalMs);
            self->m_timer.start(delay);
        });
        return;
    }

    const Scan scan = m_scan;
    scan([self, generation](TargetScanner::Result result) {
        if (!self || !self->isCurrent(generation)) return;
        const auto counts = countsByWindow(result);
        if (self->m_tryImmediateScan) {
            self->m_tryImmediateScan = false;
            bool covered = result.error.isEmpty();
            for (auto it = self->m_previousCounts.cbegin(); it != self->m_previousCounts.cend(); ++it)
                if (counts.value(it.key()) < it.value()) covered = false;
            if (!covered) {
                self->m_lastSuccess.invalidate();
                self->m_elapsed.restart();
                self->m_timer.start(0);
                return;
            }
        }
        if (result.error.isEmpty() && !counts.isEmpty()) {
            self->m_previousCounts = counts;
            self->m_previousLayout = self->m_layout;
            self->m_lastSuccess.start();
        } else {
            self->m_lastSuccess.invalidate();
        }
        self->complete(std::move(result));
    });
}

void HintScanRetry::complete(TargetScanner::Result result) {
    Done completion = std::move(m_completion);
    cancel();
    if (completion) completion(std::move(result));
}
