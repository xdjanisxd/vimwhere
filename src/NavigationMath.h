#pragma once

#include <QRectF>
#include <QVector>
#include <QString>

namespace NavigationMath {
inline int hintCodeLength(int count) {
    int length = 1, capacity = 26;
    while (capacity < count) { ++length; capacity *= 26; }
    return length;
}

inline QString hintLabel(int index, int length) {
    QString result(length, QLatin1Char('A'));
    for (int i = length - 1; i >= 0; --i) {
        result[i] = QChar('A' + index % 26);
        index /= 26;
    }
    return result;
}

inline QRectF gridCell(const QRectF &bounds, int key) {
    if (key < 1 || key > 9 || !bounds.isValid()) return {};
    const int cell = key - 1;
    const qreal width = bounds.width() / 3.0, height = bounds.height() / 3.0;
    return {bounds.x() + (cell % 3) * width,
            bounds.y() + (cell / 3) * height, width, height};
}

inline QVector<QRectF> visibleParts(const QRectF &bounds, const QVector<QRect> &screens) {
    QVector<QRectF> parts;
    for (const QRect &screen : screens) {
        const QRectF part = bounds.intersected(QRectF(screen));
        if (!part.isEmpty()) parts.push_back(part);
    }
    return parts;
}
}
