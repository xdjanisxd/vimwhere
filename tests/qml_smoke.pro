QT += core gui quick qml
CONFIG += c++20 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = qml-smoke
SOURCES += qml_smoke.cpp
DEFINES += VIMWHERE_SOURCE_DIR=\\\"$$PWD/..\\\"
