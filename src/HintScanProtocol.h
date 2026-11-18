#pragma once

#include "TargetScanner.h"

#include <QByteArray>
#include <QJsonObject>
#include <optional>

namespace HintScanProtocol {

inline constexpr qsizetype MaximumMessageBytes = 64 * 1024 * 1024;

enum class Operation { Prepare, Probe, Scan };

struct Request {
    QString id;
    Operation operation;
    QJsonObject snapshot;
};

struct Response {
    QString id;
    QJsonObject result;
};

QByteArray encode(const Request &request);
QByteArray encode(const Response &response);
std::optional<Request> decodeRequest(const QByteArray &line, QString *error = nullptr);
std::optional<Response> decodeResponse(const QByteArray &line, QString *error = nullptr);

QJsonObject encodeProbe(const TargetScanner::ProbeResult &result);
QJsonObject encodeScan(const TargetScanner::Result &result);
std::optional<TargetScanner::ProbeResult> decodeProbe(const QJsonObject &value);
std::optional<TargetScanner::Result> decodeScan(const QJsonObject &value);

}
