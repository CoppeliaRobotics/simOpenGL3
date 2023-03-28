cmake_minimum_required(VERSION 3.15)
project(simExtOpenGL3Renderer)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Widgets OpenGL PrintSupport REQUIRED)
find_package(Boost REQUIRED)
find_package(OpenGL REQUIRED)

if(NOT COPPELIASIM_INCLUDE_DIR)
    if(DEFINED ENV{COPPELIASIM_ROOT_DIR})
        set(COPPELIASIM_INCLUDE_DIR $ENV{COPPELIASIM_ROOT_DIR}/programming/include)
    else()
        set(COPPELIASIM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    endif()
endif()
list(APPEND CMAKE_MODULE_PATH ${COPPELIASIM_INCLUDE_DIR}/cmake)
find_package(CoppeliaSim 4.5.0.0 REQUIRED)

coppeliasim_add_plugin(
    simExtOpenGL3Renderer
    LEGACY
    SOURCES
    simExtOpenGL3Renderer.cpp
    light.cpp
    mesh.cpp
    openglWindow.cpp
    shaderProgram.cpp
    texture.cpp
    frameBufferObject.cpp
    offscreenGlContext.cpp
    openglOffscreen.cpp
    openglBase.cpp
    res.qrc
    ${COPPELIASIM_INCLUDE_DIR}/simMath/3Vector.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/3X3Matrix.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/4Vector.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/4X4Matrix.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/mXnMatrix.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/7Vector.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/mathFuncs.cpp
)
target_link_libraries(simExtOpenGL3Renderer Qt5::Widgets Qt5::OpenGL Qt5::PrintSupport OpenGL::GL)
