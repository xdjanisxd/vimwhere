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
    // Qt/KWin use logical coordinates here; scale changes physical pixel
    // density, while each monitor's own origin determines its QML-local point.
    const QRect leftScreen(-1600, -200, 1600, 900);  // e.g. 1.0 scale
    const QRect rightScreen(0, 0, 2560, 1440);       // e.g. 1.5 scale
    const QPointF leftHint(-800, 250);
    const QPointF rightHint(1920, 1080);
    ok &= check(NavigationMath::containsGlobalPoint(leftScreen, leftHint) &&
                    NavigationMath::screenLocalPoint(leftHint, leftScreen) == QPointF(800, 450),
                "hint on negative-origin display maps to local logical coordinates");
    ok &= check(!NavigationMath::containsGlobalPoint(leftScreen, rightHint) &&
                    NavigationMath::containsGlobalPoint(rightScreen, rightHint) &&
                    NavigationMath::screenLocalPoint(rightHint, rightScreen) == QPointF(1920, 1080),
                "scaled display hint stays in its own local logical coordinate space");
    const auto rightBottom = NavigationMath::visibleParts(
        NavigationMath::gridCell(QRectF(rightScreen), 9), {leftScreen, rightScreen});
    ok &= check(rightBottom.size() == 1 &&
                    qAbs(rightBottom.first().x() - 1706.6666666667) < 0.001 &&
                    qAbs(rightBottom.first().y() - 960) < 0.001 &&
                    qAbs(rightBottom.first().right() - QRectF(rightScreen).right()) < 0.001 &&
                    qAbs(rightBottom.first().bottom() - QRectF(rightScreen).bottom()) < 0.001,
                "grid on scaled display clips to the selected display bounds");
    if (ok) qInfo() << "navigation checks passed";
    return ok ? 0 : 1;
}
