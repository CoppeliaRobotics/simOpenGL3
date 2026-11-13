# Development tool: renders a synthetic scene with the real renderer and
# writes smoke.png / smokeOrtho.png, so that GLSL errors and framebuffer
# problems show up without starting CoppeliaSim.
#
# Qt-free on purpose: it uses the same platform context backend as the plugin,
# so it also runs on a headless build machine.

TARGET = smokeTest
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle

QT -= core gui

win32 {
    DEFINES += WIN_SIM NOMINMAX _USE_MATH_DEFINES
    LIBS += -lopengl32 -lgdi32 -luser32
}

unix:!macx {
    DEFINES += LIN_SIM
    LIBS += -lEGL -ldl
}

macx {
    DEFINES += MAC_SIM
    LIBS += -framework OpenGL
}

*-msvc* {
    QMAKE_CXXFLAGS += /permissive-
    QMAKE_CXXFLAGS -= -W3
}

INCLUDEPATH += ../../sourceCode

SOURCES += \
    main.cpp \
    ../../sourceCode/glUtil.cpp \
    ../../sourceCode/glApi.cpp \
    ../../sourceCode/log.cpp \
    ../../sourceCode/renderSettings.cpp \
    ../../sourceCode/offscreenContextCommon.cpp \
    ../../sourceCode/offscreenTarget.cpp \
    ../../sourceCode/shadowAtlas.cpp \
    ../../sourceCode/sceneRenderer.cpp \

win32 {
    SOURCES += ../../sourceCode/offscreenContextWin32.cpp
}
unix:!macx {
    SOURCES += ../../sourceCode/offscreenContextEgl.cpp
}
macx {
    SOURCES += ../../sourceCode/offscreenContextCgl.cpp
}

HEADERS += \
    pngWriter.h \
    ../../sourceCode/glUtil.h \
    ../../sourceCode/glApi.h \
    ../../sourceCode/math3d.h \
    ../../sourceCode/log.h \
    ../../sourceCode/offscreenContext.h \
    ../../sourceCode/offscreenTarget.h \
    ../../sourceCode/shadowAtlas.h \
    ../../sourceCode/sceneRenderer.h \
    ../../sourceCode/renderSettings.h \
    ../../sourceCode/sceneTypes.h \
    ../../sourceCode/shaders_embedded.h
