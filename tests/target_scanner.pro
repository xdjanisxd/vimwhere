QT += core
QT -= gui
CONFIG += c++20 console link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = target-scanner-test
SOURCES += target_scanner.cpp ../src/TargetScanner.cpp
PKGCONFIG += atspi-2 gobject-2.0
