QT += core dbus
CONFIG += c++20 console link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = accessibility-status-test
SOURCES += accessibility_status.cpp ../src/AccessibilityStatus.cpp
PKGCONFIG += atspi-2
