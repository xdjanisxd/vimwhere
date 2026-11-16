#include "../src/NavigationMath.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSet>

static bool check(bool value, const char *name) {
    if (!value) qCritical() << "FAIL:" << name;
    return value;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    bool ok = true;
    ok &= check(NavigationMath::hintCodeLength(26) == 1, "26 hints use one key");
    ok &= check(NavigationMath::hintCodeLength(27) == 2, "27 hints use two keys");
    QSet<QString> codes;
    for (int i = 0; i < 900; ++i) codes.insert(NavigationMath::hintLabel(i, 3));
    ok &= check(codes.size() == 900, "hint codes are unique");
    ok &= check(NavigationMath::hintLabel(0, 2) == QStringLiteral("AA"), "first label");
    ok &= check(NavigationMath::hintLabel(26, 2) == QStringLiteral("BA"), "base 26 rollover");
    const QRectF desktop(0, 0, 900, 900);
    ok &= check(NavigationMath::gridCell(desktop, 1) == QRectF(0, 0, 300, 300), "top left grid cell");
    ok &= check(NavigationMath::gridCell(desktop, 9) == QRectF(600, 600, 300, 300), "bottom right grid cell");
    const QVector<QRect> staggered{QRect(0, 0, 900, 400), QRect(900, 300, 900, 400)};
    ok &= check(NavigationMath::visibleParts(QRectF(0, 700, 300, 200), staggered).isEmpty(),
                "empty gap in staggered monitors is rejected");
    ok &= check(NavigationMath::visibleParts(QRectF(800, 300, 200, 100), staggered).size() == 2,
                "cell split across displays requires more narrowing");
    ok &= check(NavigationMath::visibleParts(QRectF(50, 50, 100, 100), staggered).size() == 1,
                "single monitor cell can be clicked");
    if (ok) qInfo() << "navigation checks passed";
    return ok ? 0 : 1;
}
