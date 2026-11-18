#include "HintScanProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtMath>

#include <limits>
#include <span>
#include <utility>

namespace HintScanProtocol {
namespace {
std::nullopt_t invalid(QString *error, const QString &message) {
    if (error) *error = message;
    return std::nullopt;
}

std::optional<QJsonObject> decodeObject(const QByteArray &line, QString *error) {
    if (error) error->clear();
    const qsizetype payloadSize = line.endsWith('\n') ? line.size() - 1 : line.size();
    if (payloadSize > MaximumMessageBytes)
        return invalid(error, QStringLiteral("Hint worker message exceeded the size limit"));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return invalid(error, QStringLiteral("Invalid hint worker JSON: %1").arg(parseError.errorString()));
    if (!document.isObject())
        return invalid(error, QStringLiteral("Hint worker message must be a JSON object"));
    return document.object();
}

QByteArray encodeObject(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

QString operationName(Operation operation) {
    switch (operation) {
    case Operation::Prepare: return QStringLiteral("prepare");
    case Operation::Probe: return QStringLiteral("probe");
    case Operation::Scan: return QStringLiteral("scan");
    }
    return {};
}

QJsonObject encodeMetrics(const TargetScanner::Metrics &metrics) {
    return {
        {QStringLiteral("matchingMs"), metrics.matchingMs},
        {QStringLiteral("activationMs"), metrics.activationMs},
        {QStringLiteral("traversalMs"), metrics.traversalMs},
        {QStringLiteral("probedWindows"), metrics.probedWindows},
        {QStringLiteral("visitedNodes"), metrics.visitedNodes},
        {QStringLiteral("depthLimitReached"), metrics.depthLimitReached},
        {QStringLiteral("nodeLimitReached"), metrics.nodeLimitReached},
        {QStringLiteral("childLimitReached"), metrics.childLimitReached}
    };
}

int readCount(const QJsonValue &value) {
    const auto count = value.toInteger(-1);
    if (count < 0 || count > std::numeric_limits<int>::max()) return -1;
    return int(count);
}

std::optional<TargetScanner::Metrics> decodeMetrics(const QJsonValue &value) {
    if (!value.isObject()) return std::nullopt;
    const auto object = value.toObject();
    const auto matching = object.value(QStringLiteral("matchingMs")).toInteger(-1);
    const auto activation = object.value(QStringLiteral("activationMs")).toInteger(-1);
    const auto traversal = object.value(QStringLiteral("traversalMs")).toInteger(-1);
    const int probed = readCount(object.value(QStringLiteral("probedWindows")));
    const int visited = readCount(object.value(QStringLiteral("visitedNodes")));
    const auto depthLimit = object.value(QStringLiteral("depthLimitReached"));
    const auto nodeLimit = object.value(QStringLiteral("nodeLimitReached"));
    const auto childLimit = object.value(QStringLiteral("childLimitReached"));
    if (matching < 0 || activation < 0 || traversal < 0 || probed < 0 || visited < 0 ||
        !depthLimit.isBool() || !nodeLimit.isBool() || !childLimit.isBool())
        return std::nullopt;
    return TargetScanner::Metrics{matching, activation, traversal, probed, visited,
        depthLimit.toBool(), nodeLimit.toBool(), childLimit.toBool()};
}

template <typename Result>
QJsonObject encodeResult(const Result &result) {
    return {
        {QStringLiteral("error"), result.error},
        {QStringLiteral("unmatchedWindows"), result.unmatchedWindows},
        {QStringLiteral("matchedWindows"), result.matchedWindows},
        {QStringLiteral("metrics"), encodeMetrics(result.metrics)}
    };
}

template <typename Result>
bool decodeResult(const QJsonObject &object, Result &result) {
    const auto error = object.value(QStringLiteral("error"));
    const int unmatched = readCount(object.value(QStringLiteral("unmatchedWindows")));
    const int matched = readCount(object.value(QStringLiteral("matchedWindows")));
    const auto metrics = decodeMetrics(object.value(QStringLiteral("metrics")));
    if (!error.isString() || unmatched < 0 || matched < 0 || !metrics) return false;
    result.error = error.toString();
    result.unmatchedWindows = unmatched;
    result.matchedWindows = matched;
    result.metrics = *metrics;
    return true;
}

bool decodeCoordinates(const QJsonValue &value, std::span<qreal> coordinates) {
    const auto array = value.toArray();
    if (array.size() != qsizetype(coordinates.size())) return false;
    for (qsizetype i = 0; i < array.size(); ++i) {
        const auto coordinate = array.at(i);
        if (!coordinate.isDouble() || !qIsFinite(coordinate.toDouble())) return false;
        coordinates[i] = coordinate.toDouble();
    }
    return true;
}

QJsonObject encodeTarget(const HintTarget &target) {
    return {
        {QStringLiteral("globalPoint"), QJsonArray{target.globalPoint.x(), target.globalPoint.y()}},
        {QStringLiteral("windowId"), target.windowId},
        {QStringLiteral("clientRect"), QJsonArray{target.clientRect.x(), target.clientRect.y(),
            target.clientRect.width(), target.clientRect.height()}}
    };
}

std::optional<HintTarget> decodeTarget(const QJsonValue &value) {
    const auto object = value.toObject();
    const auto window = object.value(QStringLiteral("windowId"));
    qreal point[2];
    qreal client[4];
    if (!window.isString() ||
        !decodeCoordinates(object.value(QStringLiteral("globalPoint")), point) ||
        !decodeCoordinates(object.value(QStringLiteral("clientRect")), client))
        return std::nullopt;
    return HintTarget{{point[0], point[1]}, window.toString(), {},
        {client[0], client[1], client[2], client[3]}};
}
}

QByteArray encode(const Request &request) {
    QJsonObject object{{QStringLiteral("id"), request.id},
                       {QStringLiteral("operation"), operationName(request.operation)}};
    if (request.operation != Operation::Prepare) object.insert(QStringLiteral("snapshot"), request.snapshot);
    return encodeObject(object);
}

QByteArray encode(const Response &response) {
    return encodeObject({{QStringLiteral("id"), response.id}, {QStringLiteral("result"), response.result}});
}

std::optional<Request> decodeRequest(const QByteArray &line, QString *error) {
    const auto object = decodeObject(line, error);
    if (!object) return std::nullopt;
    const auto id = object->value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return invalid(error, QStringLiteral("Hint worker request has no ID"));
    const auto operation = object->value(QStringLiteral("operation")).toString();
    if (operation == QLatin1String("prepare")) return Request{id, Operation::Prepare, {}};
    if (operation != QLatin1String("probe") && operation != QLatin1String("scan"))
        return invalid(error, QStringLiteral("Unknown hint worker operation"));
    const auto snapshot = object->value(QStringLiteral("snapshot"));
    if (!snapshot.isObject()) return invalid(error, QStringLiteral("Hint worker request has no snapshot"));
    return Request{id, operation == QLatin1String("probe") ? Operation::Probe : Operation::Scan,
        snapshot.toObject()};
}

std::optional<Response> decodeResponse(const QByteArray &line, QString *error) {
    const auto object = decodeObject(line, error);
    if (!object) return std::nullopt;
    const auto id = object->value(QStringLiteral("id")).toString();
    const auto result = object->value(QStringLiteral("result"));
    if (id.isEmpty() || !result.isObject())
        return invalid(error, QStringLiteral("Hint worker response requires an ID and a result object"));
    return Response{id, result.toObject()};
}

QJsonObject encodeProbe(const TargetScanner::ProbeResult &result) {
    auto object = encodeResult(result);
    object.insert(QStringLiteral("status"), int(result.status));
    return object;
}

QJsonObject encodeScan(const TargetScanner::Result &result) {
    auto object = encodeResult(result);
    QJsonArray targets;
    for (const auto &target : result.targets) targets.append(encodeTarget(target));
    object.insert(QStringLiteral("targets"), targets);
    return object;
}

std::optional<TargetScanner::ProbeResult> decodeProbe(const QJsonObject &value) {
    TargetScanner::ProbeResult result;
    const int status = readCount(value.value(QStringLiteral("status")));
    if (status < int(TargetScanner::ProbeStatus::Ready) ||
        status > int(TargetScanner::ProbeStatus::Failed) || !decodeResult(value, result))
        return std::nullopt;
    result.status = TargetScanner::ProbeStatus(status);
    return result;
}

std::optional<TargetScanner::Result> decodeScan(const QJsonObject &value) {
    TargetScanner::Result result;
    const auto targets = value.value(QStringLiteral("targets"));
    if (!targets.isArray() || !decodeResult(value, result))
        return std::nullopt;
    const auto items = targets.toArray();
    result.targets.reserve(items.size());
    for (const auto &item : items) {
        auto target = decodeTarget(item);
        if (!target) return std::nullopt;
        result.targets.push_back(std::move(*target));
    }
    return result;
}

}
