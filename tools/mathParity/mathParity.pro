# Throw-away tool: compares math3d.h against Qt's matrix/vector classes.
TARGET = mathParity
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
QT += gui
INCLUDEPATH += ../../sourceCode
SOURCES += main.cpp
HEADERS += ../../sourceCode/math3d.h
