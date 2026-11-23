QT += core gui widgets quick qml dbus
CONFIG += c++20 link_pkgconfig
PKGCONFIG += atspi-2 glib-2.0 gobject-2.0
TEMPLATE = app
TARGET = vimwhere
SOURCES += src/main.cpp src/AccessibilityStatus.cpp src/AppController.cpp src/KWinBridge.cpp src/TargetScanner.cpp src/HintScanRetry.cpp src/HintScanWorker.cpp src/HintScanProtocol.cpp src/HintScanChild.cpp src/OverlayView.cpp src/PortalClicker.cpp
HEADERS += src/AccessibilityStatus.h src/AppController.h src/KWinBridge.h src/TargetScanner.h src/HintScanRetry.h src/HintScanWorker.h src/HintScanProtocol.h src/HintScanChild.h src/OverlayView.h src/PortalClicker.h
INCLUDEPATH += /usr/include/LayerShellQt /usr/include/KF6/KGlobalAccel
LIBS += -L/usr/lib -lLayerShellQtInterface -lKF6GlobalAccel
DEFINES += VIMWHERE_SOURCE_DIR=\\\"$$PWD\\\"

LIVE_DIR = $$OUT_PWD/.vimwhere-live
exists($$LIVE_DIR/enabled): CONFIG += vimwhere_live_reload

contains(CONFIG, vimwhere_live_reload) {
    PKGCONFIG += libsystemd
    DEFINES += VIMWHERE_LIVE_RELOAD
    RESOURCES += dev-assets.qrc

    DESTDIR = $$LIVE_DIR/build
    OBJECTS_DIR = $$LIVE_DIR/obj
    MOC_DIR = $$OBJECTS_DIR
    RCC_DIR = $$OBJECTS_DIR

    PUBLISH_SCRIPT = $$PWD/tools/publish-dev-build
    PRE_TARGETDEPS += $$PUBLISH_SCRIPT
    LIVE_BINARY = $$shell_quote($$DESTDIR/$$TARGET)
    QMAKE_POST_LINK = $$shell_quote($$PUBLISH_SCRIPT) $$LIVE_BINARY $$shell_quote($$LIVE_DIR) || { rm -f -- $$LIVE_BINARY; exit 1; }
}
