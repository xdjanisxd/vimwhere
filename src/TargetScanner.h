#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QString>

struct HintTarget {
    QPointF globalPoint;
    QString name;
    QString windowId;
    QString label;
    QRectF clientRect;
};

class TargetScanner {
public:
    struct Result {
        QVector<HintTarget> targets;
        QString error;
        int unmatchedWindows = 0;
        int matchedWindows = 0;
    };
    Result scan(const QJsonObject &kwinSnapshot) const;
};
