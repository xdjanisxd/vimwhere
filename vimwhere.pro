QT += core gui quick qml dbus
CONFIG += c++20 link_pkgconfig
PKGCONFIG += atspi-2 gobject-2.0
TEMPLATE = app
TARGET = vimwhere
SOURCES += src/main.cpp src/AccessibilityStatus.cpp src/AppController.cpp src/KWinBridge.cpp src/TargetScanner.cpp src/HintScanRetry.cpp src/OverlayView.cpp src/PortalClicker.cpp
HEADERS += src/AccessibilityStatus.h src/AppController.h src/KWinBridge.h src/TargetScanner.h src/HintScanRetry.h src/OverlayView.h src/PortalClicker.h
INCLUDEPATH += /usr/include/LayerShellQt /usr/include/KF6/KGlobalAccel
LIBS += -L/usr/lib -lLayerShellQtInterface -lKF6GlobalAccel
DEFINES += VIMWHERE_SOURCE_DIR=\\\"$$PWD\\\"
