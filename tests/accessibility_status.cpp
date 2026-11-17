#include "../src/AccessibilityStatus.h"

#include <QCoreApplication>
#include <QDebug>

static bool check(bool value, const char *message) {
    if (!value) qCritical() << "FAIL:" << message;
    return value;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    bool ok = true;
    QString error;
    int reads = 0, writes = 0;
    bool enabled = true;
    auto read = [&](bool *value, QString *) { ++reads; *value = enabled; return true; };
    auto write = [&](QString *) { ++writes; enabled = true; return true; };

    ok &= check(AccessibilityStatus::ensureEnabled(read, write, &error) && writes == 0,
                "an enabled session remains unchanged");
    enabled = false; reads = writes = 0;
    ok &= check(AccessibilityStatus::ensureEnabled(read, write, &error) &&
                    reads == 2 && writes == 1 && enabled,
                "a disabled session enables and verifies only the AT-SPI bus");
    reads = writes = 0;
    auto unavailable = [](bool *, QString *failure) {
        *failure = QStringLiteral("status service unavailable"); return false;
    };
    ok &= check(!AccessibilityStatus::ensureEnabled(unavailable, write, &error) &&
                    error == QStringLiteral("status service unavailable") && writes == 0,
                "unavailable status service fails without attempting a write");
    auto failedWrite = [](QString *failure) {
        *failure = QStringLiteral("write denied"); return false;
    };
    enabled = false; error.clear();
    ok &= check(!AccessibilityStatus::ensureEnabled(read, failedWrite, &error) &&
                    error == QStringLiteral("write denied"),
                "a rejected enable request is reported");
    enabled = false; error.clear();
    auto ignoredWrite = [](QString *) { return true; };
    ok &= check(!AccessibilityStatus::ensureEnabled(read, ignoredWrite, &error) &&
                    error == QStringLiteral("AT-SPI remained disabled after the enable request"),
                "an enable request that does not change status is reported");
    if (ok) qInfo() << "AT-SPI session status checks passed";
    return ok ? 0 : 1;
}
