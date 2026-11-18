QT += core
QT -= gui
CONFIG += c++20 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = hint-scan-retry-test
SOURCES += hint_scan_retry.cpp ../src/HintScanProtocol.cpp ../src/HintScanRetry.cpp ../src/HintScanWorker.cpp
