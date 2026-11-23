#include "../src/HintScanProtocol.h"
#include "../src/HintScanRetry.h"
#include "../src/HintScanWorker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace {
using ProbeResult = TargetScanner::ProbeResult;
using ProbeStatus = TargetScanner::ProbeStatus;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

void runLoop(QEventLoop &loop) {
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [] {
        check(false, "Timed out waiting for the hint scan");
    });
    timeout.start(4000);
    loop.exec();
}

TargetScanner::Result resultForTarget(int position) {
    TargetScanner::Result result;
    HintTarget target;
    target.globalPoint = QPointF(position, position);
    result.targets.push_back(target);
    return result;
}

ProbeResult probeResult(ProbeStatus status) {
    ProbeResult result;
    result.status = status;
    return result;
}

void testHydrationAndFinalSnapshot() {
    HintScanRetry retry;
    QEventLoop loop;
    QElapsedTimer elapsed;
    QVector<qint64> probeTimes;
    int finalScans = 0;
    int position = 0;
    qint64 finalTime = 0;
    elapsed.start();

    retry.start([&](HintScanRetry::ProbeDone done) {
        probeTimes.push_back(elapsed.elapsed());
        // The provider starts empty, briefly shows a target, and replaces it before collection.
        if (probeTimes.size() >= 2) position = 10;
        done(probeResult(position ? ProbeStatus::Ready : ProbeStatus::NeedsHydration));
    }, [&](HintScanRetry::Done done) {
        ++finalScans;
        finalTime = elapsed.elapsed();
        position = 20;
        done(resultForTarget(position));
    }, [&](TargetScanner::Result result) {
        check(result.targets.size() == 1 && result.targets.first().globalPoint == QPointF(20, 20),
              "Publish the late target at the position from the final snapshot");
        loop.quit();
    }, [] { return true; });
    runLoop(loop);

    check(!probeTimes.isEmpty() && probeTimes.size() <= 5 && finalScans == 1,
          "Allow up to five shallow probes and one final full collection even when a probe is ready");
    for (int i = 1; i < probeTimes.size(); ++i)
        check(probeTimes[i] >= i * 80 - 5,
              "The probes must respect their 80 ms schedule");
    check(finalTime >= 395,
          "Preserve the roughly 400 ms hydration window before collecting targets");
    check(!retry.isActive(), "The retry scheduler must finish after publishing the result");
}

void testSlowProbe() {
    HintScanRetry retry;
    QEventLoop loop;
    int probes = 0;
    retry.start([&](HintScanRetry::ProbeDone done) {
        ++probes;
        QTimer::singleShot(420, &loop, [done = std::move(done)] {
            done(probeResult(ProbeStatus::NeedsHydration));
        });
    }, [](HintScanRetry::Done done) {
        done(resultForTarget(1));
    }, [&](TargetScanner::Result) { loop.quit(); }, [] { return true; });
    runLoop(loop);
    check(probes == 1, "Collect immediately after a probe consumes the hydration window");
}

void testRepeatedLayout() {
    HintScanRetry retry;
    QEventLoop loop;
    int probes = 0, scans = 0;
    auto invoke = [&](int layout, bool firstScanMissing) {
        probes = scans = 0;
        retry.start([&](HintScanRetry::ProbeDone done) {
            ++probes;
            done(probeResult(ProbeStatus::Ready));
        }, [&](HintScanRetry::Done done) {
            ++scans;
            auto result = resultForTarget(layout * 10 + scans);
            result.targets.first().windowId = firstScanMissing && scans == 1
                ? QStringLiteral("other") : QStringLiteral("main");
            done(std::move(result));
        }, [&](TargetScanner::Result result) {
            check(result.targets.first().windowId == QStringLiteral("main") &&
                  result.targets.first().globalPoint.x() == layout * 10 + scans,
                  "Show only targets from the latest complete scan");
            loop.quit();
        }, [] { return true; }, {{QStringLiteral("layout"), layout}});
        runLoop(loop);
    };
    invoke(1, false);
    check(probes > 0 && scans == 1, "Hydrate a new layout");
    invoke(1, false);
    check(probes == 0 && scans == 1, "Repeated layout should scan immediately");
    invoke(1, true);
    check(probes > 0 && scans == 2, "Recover a previously visible window after a partial warm scan");
    invoke(2, false);
    check(probes > 0 && scans == 1, "Hydrate on window or focus changes");
}

void testDisappearingTarget() {
    HintScanRetry retry;
    QEventLoop loop;
    int probes = 0;
    int finalScans = 0;
    const QString error = QStringLiteral("No safely positioned accessible targets found");

    retry.start([&](HintScanRetry::ProbeDone done) {
        ++probes;
        // Earlier probes saw a provider tree that disappears before collection.
        done(probeResult(probes < 5 ? ProbeStatus::Ready : ProbeStatus::NeedsHydration));
    }, [&](HintScanRetry::Done done) {
        ++finalScans;
        TargetScanner::Result empty;
        empty.error = error;
        done(std::move(empty));
    }, [&](TargetScanner::Result result) {
        check(result.targets.isEmpty() && result.error == error,
              "Publish the final empty result when an earlier provider disappears");
        loop.quit();
    }, [] { return true; });
    runLoop(loop);
    check(probes > 0 && probes <= 5 && finalScans == 1, "Hydration expiry still collects exactly one snapshot");
}

void testFailedProbe() {
    HintScanRetry retry;
    QEventLoop loop;
    int probes = 0;
    int finalScans = 0;

    retry.start([&](HintScanRetry::ProbeDone done) {
        ProbeResult result = probeResult(++probes == 1 ? ProbeStatus::Failed : ProbeStatus::Ready);
        if (probes == 1) result.error = QStringLiteral("Unable to inspect accessibility trees");
        done(std::move(result));
    }, [&](HintScanRetry::Done done) {
        ++finalScans;
        done(resultForTarget(1));
    }, [&](TargetScanner::Result result) {
        check(result.targets.size() == 1 && result.error.isEmpty(),
              "Only the final collection can supply targets or report their scan error");
        loop.quit();
    }, [] { return true; });
    runLoop(loop);
    check(probes > 0 && probes <= 5 && finalScans == 1, "Continue through a transient early probe failure");
}

void testStaleFocusAndCallback() {
    HintScanRetry retry;
    QEventLoop loop;
    HintScanRetry::ProbeDone staleProbe;
    bool isCurrent = true;

    retry.start([&](HintScanRetry::ProbeDone done) {
        staleProbe = std::move(done);
        loop.quit();
    }, [](HintScanRetry::Done) {
        check(false, "A stale generation must not collect targets");
    }, [](TargetScanner::Result) {
        check(false, "A stale generation must not publish hints");
    }, [&] { return isCurrent; });
    runLoop(loop);
    isCurrent = false;
    staleProbe(probeResult(ProbeStatus::Ready));
    check(!retry.isActive(), "An in-flight probe from a stale focus snapshot must cancel");

    HintScanRetry::Done staleResult;
    retry.start([](HintScanRetry::ProbeDone done) {
        done(probeResult(ProbeStatus::Ready));
    }, [&](HintScanRetry::Done done) {
        staleResult = std::move(done);
        loop.quit();
    }, [](TargetScanner::Result) {
        check(false, "A replaced generation must not publish hints");
    }, [] { return true; });
    runLoop(loop);

    int newProbes = 0;
    retry.start([&](HintScanRetry::ProbeDone done) {
        ++newProbes;
        done(probeResult(ProbeStatus::Ready));
    }, [](HintScanRetry::Done done) {
        done(resultForTarget(2));
    }, [&](TargetScanner::Result result) {
        check(result.targets.size() == 1 && result.targets.first().globalPoint == QPointF(2, 2),
              "Publish the newest final result even if the replaced scan replies");
        loop.quit();
    }, [] { return true; });
    staleResult(resultForTarget(1));
    runLoop(loop);
    check(newProbes > 0 && newProbes <= 5, "A queued stale callback must not interrupt the new generation");
}

void writeMarker(const QString &path) {
    QFile marker(path);
    check(marker.open(QIODevice::WriteOnly), "Create the fixture marker");
}

QJsonObject snapshotFor(int request) {
    return {{QStringLiteral("request"), request}};
}

int fakeHelper(const QString &mode, const QString &directory) {
    using namespace HintScanProtocol;
    const int pid = int(QCoreApplication::applicationPid());
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto request = decodeRequest(QByteArray::fromStdString(line));
        check(request.has_value(), "Decode a request sent by the worker");
        QJsonObject result;
        switch (request->operation) {
        case Operation::Prepare:
            if (mode == QStringLiteral("hold-prepare")) {
                writeMarker(directory + QStringLiteral("/started"));
                QElapsedTimer deadline;
                deadline.start();
                while (!QFile::exists(directory + QStringLiteral("/release"))) {
                    check(!deadline.hasExpired(6000), "The owner thread must release the startup fixture");
                    QThread::msleep(5);
                }
            }
            result.insert(QStringLiteral("error"), QStringLiteral(""));
            break;
        case Operation::Probe: {
            auto probe = probeResult(ProbeStatus::Ready);
            probe.matchedWindows = pid;
            result = encodeProbe(probe);
            break;
        }
        case Operation::Scan: {
            const int number = request->snapshot.value(QStringLiteral("request")).toInt();
            check(number == 1 || number == 2, "Superseded pending operations must never reach the helper");
            if (mode == QStringLiteral("hold-scan") && number == 1) {
                writeMarker(directory + QStringLiteral("/started"));
                // This request can only finish after the parent's deadline or after it kills the helper.
                QThread::sleep(10);
            }
            if (mode == QStringLiteral("exit-scan") && number == 1) return 1;
            auto scan = resultForTarget(number);
            scan.matchedWindows = pid;
            result = encodeScan(scan);
            break;
        }
        }
        const QByteArray response = encode(Response{request->id, std::move(result)});
        std::cout.write(response.constData(), response.size()) << std::flush;
    }
    return 0;
}

void testSlowWorkerCancellation() {
    QTemporaryDir directory;
    check(directory.isValid(), "Create a fixture directory for the blocked helper");
    QEventLoop loop;
    HintScanRetry retry;
    int heartbeats = 0;
    int completions = 0;
    int pids[2] = {};
    bool restarted = false;
    auto *ownerThread = QThread::currentThread();
    HintScanWorker worker(QCoreApplication::applicationFilePath(),
        {QStringLiteral("--fake-hint-helper"), QStringLiteral("hold-scan"), directory.path()});
    const auto start = [&](int request) {
        retry.start([&, request](HintScanRetry::ProbeDone done) {
            worker.probe(snapshotFor(request), [&, request, done = std::move(done)](ProbeResult result) {
                check(result.error.isEmpty() && result.status == ProbeStatus::Ready,
                      "Read the child process's probe result");
                int &pid = pids[request - 1];
                if (pid == 0) pid = result.matchedWindows;
                check(pid == result.matchedWindows && pid > 0 && pid != QCoreApplication::applicationPid(),
                      "Reuse a separate helper process throughout the hydration probes");
                done(std::move(result));
            });
        }, [&, request](HintScanRetry::Done done) {
            worker.scan(snapshotFor(request), std::move(done));
        }, [&, request](TargetScanner::Result result) {
            check(QThread::currentThread() == ownerThread, "Publish results on the owner thread");
            ++completions;
            check(request == 2 && result.targets.size() == 1 && result.targets.first().globalPoint == QPointF(2, 2),
                  "Publish only the newest worker result after restarting");
            check(result.matchedWindows == pids[1] && pids[1] != pids[0],
                  "The newest invocation must use one fresh helper for probes and final collection");
            loop.quit();
        }, [] { return true; });
    };

    const QString started = directory.filePath(QStringLiteral("started"));
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &loop, [&] {
        if (restarted || !QFile::exists(started)) return;
        if (++heartbeats < 3) return;
        retry.cancel();
        worker.cancel();
        restarted = true;
        start(2);
    });
    heartbeat.start(5);
    worker.prepare([&](QString error) {
        check(QThread::currentThread() == ownerThread, "Preparation must reply on the owner thread");
        check(error.isEmpty(), "Expected successful mock AT-SPI preparation");
        start(1);
    });
    runLoop(loop);

    check(heartbeats == 3 && completions == 1,
          "The owner heartbeat, cancel, and restart must work during a blocked scan");
}

void testNewestPendingOperation() {
    QTemporaryDir directory;
    check(directory.isValid(), "Create a fixture directory for the blocked warm-up");
    QEventLoop loop;
    HintScanWorker worker(QCoreApplication::applicationFilePath(),
        {QStringLiteral("--fake-hint-helper"), QStringLiteral("hold-prepare"), directory.path()});
    bool startupCompleted = false;
    worker.warmUp([&](QString error) {
        check(error.isEmpty(), "Startup preparation can finish after shortcut cancellation");
        startupCompleted = true;
    });

    const QString started = directory.filePath(QStringLiteral("started"));
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (!QFile::exists(started)) return;
        poll.stop();
        worker.cancel();
        for (int request : {3, 4}) {
            worker.scan(snapshotFor(request), [](TargetScanner::Result) {
                check(false, "A superseded pending operation must not publish a result");
            });
        }
        worker.scan(snapshotFor(2), [&](TargetScanner::Result result) {
            check(startupCompleted && result.targets.size() == 1 &&
                  result.targets.first().globalPoint == QPointF(2, 2),
                  "Finish startup and run only the newest pending request");
            loop.quit();
        });
        writeMarker(directory.filePath(QStringLiteral("release")));
    });
    poll.start(5);
    runLoop(loop);
}

void testHelperExitAndRecovery() {
    QEventLoop loop;
    HintScanWorker worker(QCoreApplication::applicationFilePath(),
        {QStringLiteral("--fake-hint-helper"), QStringLiteral("exit-scan"), QStringLiteral("")});
    worker.probe(snapshotFor(1), [&](ProbeResult probe) {
        const int exitedPid = probe.matchedWindows;
        check(probe.status == ProbeStatus::Ready && exitedPid > 0, "Read the original helper PID");
        worker.scan(snapshotFor(1), [&, exitedPid](TargetScanner::Result failed) {
            check(!failed.error.isEmpty() && failed.targets.isEmpty(),
                  "Report a failed scan when the helper exits before replying");
            worker.scan(snapshotFor(2), [&, exitedPid](TargetScanner::Result result) {
                check(result.error.isEmpty() && result.targets.size() == 1 &&
                      result.targets.first().globalPoint == QPointF(2, 2),
                      "Recover the next scan after an unexpected helper exit");
                check(result.matchedWindows > 0 && result.matchedWindows != exitedPid,
                      "Launch a fresh helper after unexpected exit");
                loop.quit();
            });
        });
    });
    runLoop(loop);
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() == 4 && arguments[1] == QStringLiteral("--fake-hint-helper"))
        return fakeHelper(arguments[2], arguments[3]);

    testHydrationAndFinalSnapshot();
    testSlowProbe();
    testRepeatedLayout();
    testDisappearingTarget();
    testFailedProbe();
    testStaleFocusAndCallback();
    testSlowWorkerCancellation();
    testNewestPendingOperation();
    testHelperExitAndRecovery();
    return 0;
}
