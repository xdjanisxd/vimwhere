#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QString>

struct HintTarget {
    QPointF globalPoint;
    QString windowId;
    QString label;
    QRectF clientRect;
};

class TargetScanner {
public:
    struct Metrics {
        qint64 matchingMs = 0;
        qint64 activationMs = 0;
        qint64 traversalMs = 0;
        int probedWindows = 0;
        int visitedNodes = 0;
        bool depthLimitReached = false;
        bool nodeLimitReached = false;
        bool childLimitReached = false;
    };

    // Ready describes the immediate children of matched roots. A provider may
    // still be hydrating descendants that a probe never visits.
    enum class ProbeStatus { Ready, NeedsHydration, Failed };

    struct ProbeResult {
        ProbeStatus status = ProbeStatus::NeedsHydration;
        QString error;
        int unmatchedWindows = 0;
        int matchedWindows = 0;
        Metrics metrics;
    };

    struct Result {
        QVector<HintTarget> targets;
        QString error;
        int unmatchedWindows = 0;
        int matchedWindows = 0;
        Metrics metrics;
    };

    ProbeResult probe(const QJsonObject &kwinSnapshot) const;
    Result scan(const QJsonObject &kwinSnapshot) const;
};
