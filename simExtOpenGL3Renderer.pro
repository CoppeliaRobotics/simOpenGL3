TARGET = simExtOpenGL3Renderer
TEMPLATE = lib
DEFINES -= UNICODE
CONFIG += shared plugin
QT     += widgets opengl printsupport #printsupport required from MacOS, otherwise crashes strangely ('This CONFIG += shared

win32 {
    DEFINES += WIN_SIM
    greaterThan(QT_MAJOR_VERSION,4) {
        greaterThan(QT_MINOR_VERSION,4) {
            LIBS += -lopengl32
        }
    }
}

macx {
    INCLUDEPATH += "/usr/local/include"
    DEFINES += MAC_SIM
}

unix:!macx {
    DEFINES += LIN_SIM
}


*-msvc* {
        QMAKE_CXXFLAGS += -O2
        QMAKE_CXXFLAGS += -W3
}
*-g++* {
        QMAKE_CXXFLAGS += -O3
        QMAKE_CXXFLAGS += -Wall
        QMAKE_CXXFLAGS += -Wno-unused-parameter
        QMAKE_CXXFLAGS += -Wno-strict-aliasing
        QMAKE_CXXFLAGS += -Wno-empty-body
        QMAKE_CXXFLAGS += -Wno-write-strings

        QMAKE_CXXFLAGS += -Wno-unused-but-set-variable
        QMAKE_CXXFLAGS += -Wno-unused-local-typedefs
        QMAKE_CXXFLAGS += -Wno-narrowing

        QMAKE_CFLAGS += -O3
        QMAKE_CFLAGS += -Wall
        QMAKE_CFLAGS += -Wno-strict-aliasing
        QMAKE_CFLAGS += -Wno-unused-parameter
        QMAKE_CFLAGS += -Wno-unused-but-set-variable
        QMAKE_CFLAGS += -Wno-unused-local-typedefs
}

INCLUDEPATH += "../include"
INCLUDEPATH += "../simMath"

SOURCES += \
    ../common/simLib.cpp \
    light.cpp \
    mesh.cpp \
    openglWindow.cpp \
    shaderProgram.cpp \
    texture.cpp \
    simExtOpenGL3Renderer.cpp \
    frameBufferObject.cpp \
    offscreenGlContext.cpp \
    openglOffscreen.cpp \
    openglBase.cpp \
    ../simMath/MyMath.cpp \
    ../simMath/3Vector.cpp \
    ../simMath/4Vector.cpp \
    ../simMath/7Vector.cpp \
    ../simMath/3X3Matrix.cpp \
    ../simMath/4X4Matrix.cpp \

HEADERS +=\
    ../include/simLib.h \
    container.h \
    light.h \
    mesh.h \
    openglWindow.h \
    shaderProgram.h \
    texture.h \
    simExtOpenGL3Renderer.h \
    frameBufferObject.h \
    offscreenGlContext.h \
    openglOffscreen.h \
    openglBase.h \
    ../simMath/MyMath.h \
    ../simMath/mathDefines.h \
    ../simMath/3Vector.h \
    ../simMath/4Vector.h \
    ../simMath/7Vector.h \
    ../simMath/3X3Matrix.h \
    ../simMath/4X4Matrix.h \

unix:!symbian {
    maemo5 {
        target.path = /opt/usr/lib
    } else {
        target.path = /usr/lib
    }
    INSTALLS += target
}

DISTFILES += \
    default.vert \
    default.frag \
    depth.frag \
    depth.vert \
    omni_depth.vert \
    omni_depth.frag

RESOURCES += \
    res.qrc



















