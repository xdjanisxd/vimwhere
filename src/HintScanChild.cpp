#include "HintScanChild.h"

#include "AccessibilityStatus.h"
#include "HintScanProtocol.h"

#include <QCoreApplication>
#include <QDebug>
#include <atspi/atspi.h>
#include <glib-unix.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>

namespace {
using HintScanProtocol::Operation;
using HintScanProtocol::Request;

QJsonObject perform(const Request &request) {
    switch (request.operation) {
    case Operation::Prepare: {
        QString error;
        if (!AccessibilityStatus::enableForSession(&error) && error.isEmpty())
            error = QStringLiteral("AT-SPI accessibility bus is unavailable");
        return {{QStringLiteral("error"), error}};
    }
    case Operation::Probe:
        return HintScanProtocol::encodeProbe(TargetScanner{}.probe(request.snapshot));
    case Operation::Scan:
        return HintScanProtocol::encodeScan(TargetScanner{}.scan(request.snapshot));
    }
    Q_UNREACHABLE_RETURN(QJsonObject{});
}

bool writeAll(const QByteArray &message) {
    qsizetype written = 0;
    while (written < message.size()) {
        const ssize_t count = ::write(STDOUT_FILENO, message.constData() + written,
                                      size_t(message.size() - written));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        written += count;
    }
    return true;
}

struct Session {
    GMainLoop *loop;
    QByteArray pending;
    int exitCode = 0;

    gboolean stop(const QString &error = {}) {
        if (!error.isEmpty()) {
            qWarning().noquote() << error;
            exitCode = 1;
        }
        g_main_loop_quit(loop);
        return G_SOURCE_REMOVE;
    }
};

gboolean readRequest(gint fd, GIOCondition, gpointer data) {
    auto &session = *static_cast<Session *>(data);
    char chunk[16 * 1024];
    ssize_t count;
    do {
        count = ::read(fd, chunk, sizeof(chunk));
    } while (count < 0 && errno == EINTR);

    if (count == 0) {
        if (session.pending.isEmpty()) return session.stop();
        return session.stop(QStringLiteral("Incomplete hint worker request at end of input"));
    }
    if (count < 0) return session.stop(QStringLiteral("Could not read hint worker request"));

    session.pending.append(chunk, count);
    qsizetype newline;
    while ((newline = session.pending.indexOf('\n')) >= 0) {
        QString error;
        const auto request = HintScanProtocol::decodeRequest(session.pending.left(newline), &error);
        if (!request) return session.stop(error);
        session.pending.remove(0, newline + 1);
        const auto response = HintScanProtocol::encode(HintScanProtocol::Response{request->id, perform(*request)});
        if (response.size() - 1 > HintScanProtocol::MaximumMessageBytes)
            return session.stop(QStringLiteral("Hint worker response exceeded the size limit"));
        if (!writeAll(response)) return session.stop(QStringLiteral("Could not write hint worker response"));
    }
    if (session.pending.size() > HintScanProtocol::MaximumMessageBytes)
        return session.stop(QStringLiteral("Hint worker request exceeded the size limit"));
    return G_SOURCE_CONTINUE;
}
}

int runHintScanChild(int argc, char **argv) {
    // A parent can exit while the worker is blocked in a provider call and
    // unable to read EOF. Have Linux reap that worker even in that case.
    const pid_t parent = getppid();
    if (parent == 1 ||
        (prctl(PR_SET_PDEATHSIG, static_cast<unsigned long>(SIGTERM)) == 0 && getppid() != parent))
        return 0;

    QCoreApplication app(argc, argv);
    // Libatspi installs its accessibility and provider sources on GLib's
    // default context, which this headless process owns.
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    Session session{loop, {}};
    g_unix_fd_add(STDIN_FILENO, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), readRequest, &session);
    g_main_loop_run(loop);
    if (atspi_is_initialized()) atspi_exit();
    g_main_loop_unref(loop);
    return session.exitCode;
}
