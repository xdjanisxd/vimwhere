#pragma once

#include <QRectF>
#include <QVector>

namespace WindowMatcher {
struct Candidate {
    int index = -1;
    QRectF client;
};

// Resolve an otherwise ambiguous process match only when exactly one normal
// KWin client has dimensions consistent with the AT-SPI top-level surface.
inline bool dimensionsMatch(const QRectF &topExtents, const QRectF &client) {
    if (!topExtents.isValid() || !client.isValid()) return false;
    const qreal dx = qAbs(topExtents.width() - client.width());
    const qreal dy = qAbs(topExtents.height() - client.height());
    const qreal toleranceX = qMax<qreal>(24, client.width() * 0.08);
    const qreal toleranceY = qMax<qreal>(24, client.height() * 0.08);
    return dx <= toleranceX && dy <= toleranceY;
}

inline bool uniqueTopMatch(int topIndex, const QRectF &topExtents, const QRectF &client,
                           const QVector<QRectF> &siblingExtents) {
    if (!dimensionsMatch(topExtents, client)) return false;
    for (int i = 0; i < siblingExtents.size(); ++i) {
        if (i != topIndex && dimensionsMatch(siblingExtents[i], client)) return false;
    }
    return true;
}

inline int uniqueGeometryMatch(int topIndex, const QRectF &topExtents,
                               const QVector<Candidate> &candidates,
                               const QVector<QRectF> &siblingExtents) {
    if (!topExtents.isValid()) return -1;
    int match = -1;
    for (const Candidate &candidate : candidates) {
        if (!dimensionsMatch(topExtents, candidate.client)) continue;
        if (match >= 0) return -1; // More than one plausible surface is unsafe.
        if (!uniqueTopMatch(topIndex, topExtents, candidate.client, siblingExtents)) return -1;
        match = candidate.index;
    }
    return match;
}
}
