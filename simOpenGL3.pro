# Modern shader based external renderer for CoppeliaSim.
#
# The target name is deliberately "simOpenGL3": CoppeliaSim resolves the
# renderer plugin by that name (SIMOPENGL3_DEFAULT in pluginContainer.cpp) and
# calls the "simOpenGL3Renderer" entry point it exports, so this build replaces
# the legacy renderer without any engine change.
#
# No Qt: the off-screen context comes from WGL (Windows), EGL (Linux) or CGL
# (macOS), see the platform sources below.

TARGET = simOpenGL3
TEMPLATE = lib
DEFINES -= UNICODE
CONFIG += shared plugin c++17
CONFIG -= app_bundle
CONFIG += console

# qmake links core and gui by default; this plugin needs neither.
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
    QMAKE_CXXFLAGS += /W4 /permissive-
    QMAKE_CXXFLAGS -= -W3
}
*-g++* {
    QMAKE_CXXFLAGS += -O3 -Wall -fvisibility=hidden
    QMAKE_CXXFLAGS += -Wno-unused-parameter
    QMAKE_CXXFLAGS += -Wno-narrowing
}

# programming/include, i.e. the directory that contains simLib-2/ and simMath/.
# Override on the command line when building against another CoppeliaSim
# version, e.g. qmake "COPPELIASIM_INCLUDE_DIR=C:/coppeliaSim/programming/include"
isEmpty(COPPELIASIM_INCLUDE_DIR): COPPELIASIM_INCLUDE_DIR = $$PWD/../include
INCLUDEPATH += $$COPPELIASIM_INCLUDE_DIR

SOURCES += \
    $$COPPELIASIM_INCLUDE_DIR/simLib-2/simLib.cpp \
    sourceCode/simOpenGL3.cpp \
    sourceCode/sceneRenderer.cpp \
    sourceCode/shadowAtlas.cpp \
    sourceCode/offscreenTarget.cpp \
    sourceCode/offscreenContextCommon.cpp \
    sourceCode/glUtil.cpp \
    sourceCode/glApi.cpp \
    sourceCode/log.cpp \
    sourceCode/renderSettings.cpp \

win32 {
    SOURCES += sourceCode/offscreenContextWin32.cpp
}
unix:!macx {
    SOURCES += sourceCode/offscreenContextEgl.cpp
}
macx {
    SOURCES += sourceCode/offscreenContextCgl.cpp
}

HEADERS += \
    $$COPPELIASIM_INCLUDE_DIR/simLib-2/simLib.h \
    sourceCode/simOpenGL3.h \
    sourceCode/sceneRenderer.h \
    sourceCode/sceneTypes.h \
    sourceCode/shadowAtlas.h \
    sourceCode/offscreenTarget.h \
    sourceCode/offscreenContext.h \
    sourceCode/glUtil.h \
    sourceCode/glApi.h \
    sourceCode/math3d.h \
    sourceCode/log.h \
    sourceCode/renderSettings.h \
    sourceCode/shaders_embedded.h

DISTFILES += \
    simOpenGL3.ini \
    resources/shaders/common.glsl \
    resources/shaders/scene.vert \
    resources/shaders/scene.frag \
    resources/shaders/depth.vert \
    resources/shaders/depth.frag \
    resources/shaders/depth_omni.vert \
    resources/shaders/depth_omni.frag \
    resources/shaders/fullscreen.vert \
    resources/shaders/tonemap.frag \
    resources/shaders/marker.vert \
    resources/shaders/marker.frag \
    resources/shaders/colorcode.vert \
    resources/shaders/colorcode.frag \
    resources/shaders/points.vert \
    resources/shaders/points.frag \
    resources/shaders/voxels.vert \
    resources/shaders/voxels.frag \
    lua/simOpenGL3.lua \

unix:!symbian {
    target.path = /usr/lib
    INSTALLS += target
}
