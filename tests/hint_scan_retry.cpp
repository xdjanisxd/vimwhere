#include "../src/HintScanRetry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QVector>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

void runLoop(QEventLoop &loop, int timeoutMs) {
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    loop.exec();
}

TargetScanner::Result resultWithTargets(int count) {
    TargetScanner::Result result;
    for (int i = 0; i < count; ++i) {
        HintTarget target;
        target.globalPoint = QPointF(i + 1, count);
        result.targets.push_back(target);
    }
    return result;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    HintScanRetry retry;
    QEventLoop loop;
    int scans = 0;
    int heartbeats = 0;
    QElapsedTimer elapsed;
    QVector<qint64> scanTimes;
    TargetScanner::Result completed;
    QTimer heartbeat;
    heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start();
    elapsed.start();

    const int partialTree[] = {1, 3, 2, 5, 4, 6};
    retry.start([&] {
        scanTimes.push_back(elapsed.elapsed());
        const int count = partialTree[scans++];
        return resultWithTargets(count);
    }, [&](TargetScanner::Result result) {
        completed = std::move(result);
        loop.quit();
    }, [] { return true; });
    runLoop(loop, 1500);
    check(scans == 6, "Retry must inspect the tree for the full bounded window");
    check(completed.targets.size() == 6, "Use the final partial-tree snapshot");
    check(completed.targets.first().globalPoint.y() == 6,
          "Do not merge coordinates from snapshots taken at different times");
    check(heartbeats >= 4, "Retry delays must return control to the event loop");
    check(scanTimes.size() == 6, "Record all bounded retry scan times");
    for (int i = 1; i < scanTimes.size(); ++i)
        check(scanTimes[i] - scanTimes[i - 1] >= 60,
              "Retry scans must respect the 80 ms event-loop interval");
    check(scanTimes.last() - scanTimes.first() >= 300,
          "The hydration window must span multiple delayed retries");

    int delayedZeroChildren = 0;
    bool lateTargetFound = false;
    retry.start([&] {
        if (++delayedZeroChildren == 1) {
            TargetScanner::Result result;
            result.error = QStringLiteral("No safely positioned accessible targets found");
            return result;
        }
        return resultWithTargets(1);
    }, [&](TargetScanner::Result result) {
        lateTargetFound = result.targets.size() == 1;
        loop.quit();
    }, [] { return true; });
    runLoop(loop, 1000);
    check(delayedZeroChildren == 6 && lateTargetFound,
          "Keep scanning through the bounded hydration window after an initial zero-child result");

    int emptyAttempts = 0;
    bool emptyResult = false;
    retry.start([&] {
        if (++emptyAttempts == 1) return resultWithTargets(2);
        TargetScanner::Result result;
        result.error = QStringLiteral("No safely positioned accessible targets found");
        return result;
    }, [&](TargetScanner::Result result) {
        emptyResult = result.targets.isEmpty() && !result.error.isEmpty();
        loop.quit();
    }, [] { return true; });
    runLoop(loop, 1500);
    check(emptyResult && emptyAttempts == 6,
          "A final empty scan must not publish stale targets from an earlier scan");

    bool canceledCompletion = false;
    scans = 0;
    retry.start([&] { ++scans; return TargetScanner::Result{}; },
        [&](TargetScanner::Result) { canceledCompletion = true; }, [] { return true; });
    QTimer::singleShot(10, &retry, &HintScanRetry::cancel);
    runLoop(loop, 150);
    check(scans == 1 && !canceledCompletion, "Canceled scan must not continue or publish a result");

    bool staleCompletion = false;
    retry.start([] { return resultWithTargets(1); },
        [&](TargetScanner::Result) { staleCompletion = true; }, [] { return false; });
    check(!staleCompletion, "A scan whose focus snapshot changed must be discarded");

    bool restarted = false;
    int restartScans = 0;
    retry.start([&] { ++restartScans; return resultWithTargets(2); },
        [&](TargetScanner::Result result) {
            restarted = result.targets.size() == 2;
            loop.quit();
        }, [] { return true; });
    runLoop(loop, 1000);
    check(restarted && restartScans == 6, "A new invocation must start cleanly after cancellation");
    return 0;
}
