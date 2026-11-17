#include "HintScanRetry.h"

#include <utility>

namespace {
constexpr int RetryIntervalMs = 80;
constexpr int MaximumAttempts = 6;
}

HintScanRetry::HintScanRetry(QObject *parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(RetryIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { scanNext(); });
}

void HintScanRetry::start(Scan scan, Completion completion, IsCurrent isCurrent) {
    cancel();
    m_scan = std::move(scan);
    m_completion = std::move(completion);
    m_isCurrent = std::move(isCurrent);
    m_latest = {};
    m_attempts = 0;
    m_active = true;
    m_timer.start(0);
}

void HintScanRetry::cancel() {
    m_timer.stop();
    m_active = false;
    m_scan = {};
    m_completion = {};
    m_isCurrent = {};
}

void HintScanRetry::scanNext() {
    if (!m_active) return;
    if (!m_isCurrent || !m_isCurrent()) {
        cancel();
        return;
    }

    m_latest = m_scan();
    ++m_attempts;

    if (!m_isCurrent || !m_isCurrent()) {
        cancel();
        return;
    }

    if (m_attempts < MaximumAttempts) {
        m_timer.start(RetryIntervalMs);
        return;
    }

    Completion completion = std::move(m_completion);
    m_active = false;
    m_scan = {};
    m_isCurrent = {};
    if (completion) completion(std::move(m_latest));
}
