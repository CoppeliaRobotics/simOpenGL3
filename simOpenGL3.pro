TARGET = simOpenGL3
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
        QMAKE_CXXFLAGS += -fvisibility=hidden
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

SOURCES += \
    ../include/simLib/simLib.cpp \
    sourceCode/light.cpp \
    sourceCode/mesh.cpp \
    sourceCode/shaderProgram.cpp \
    sourceCode/texture.cpp \
    sourceCode/simOpenGL3.cpp \
    sourceCode/frameBufferObject.cpp \
    sourceCode/offscreenGlContext.cpp \
    sourceCode/openglOffscreen.cpp \
    sourceCode/openglBase.cpp \
    ../include/simMath/mathFuncs.cpp \
    ../include/simMath/3Vector.cpp \
    ../include/simMath/4Vector.cpp \
    ../include/simMath/7Vector.cpp \
    ../include/simMath/3X3Matrix.cpp \
    ../include/simMath/4X4Matrix.cpp \
    ../include/simMath/mXnMatrix.cpp \

HEADERS +=\
    ../include/simLib/simLib.h \
    sourceCode/container.h \
    sourceCode/light.h \
    sourceCode/mesh.h \
    sourceCode/shaderProgram.h \
    sourceCode/texture.h \
    sourceCode/simOpenGL3.h \
    sourceCode/frameBufferObject.h \
    sourceCode/offscreenGlContext.h \
    sourceCode/openglOffscreen.h \
    sourceCode/openglBase.h \
    ../include/simMath/mathFuncs.h \
    ../include/simMath/mathDefines.h \
    ../include/simMath/3Vector.h \
    ../include/simMath/4Vector.h \
    ../include/simMath/7Vector.h \
    ../include/simMath/3X3Matrix.h \
    ../include/simMath/4X4Matrix.h \
    ../include/simMath/mXnMatrix.h \

unix:!symbian {
    maemo5 {
        target.path = /opt/usr/lib
    } else {
        target.path = /usr/lib
    }
    INSTALLS += target
}

DISTFILES += \
    resources/default.vert \
    resources/default.frag \
    resources/depth.frag \
    resources/depth.vert \
    resources/omni_depth.vert \
    resources/omni_depth.frag

RESOURCES += \
    resources/res.qrc



















