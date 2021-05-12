cmake_minimum_required(VERSION 3.15)
project(simExtOpenGL3Renderer)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Widgets OpenGL PrintSupport REQUIRED)
find_package(Boost REQUIRED)
find_package(OpenGL REQUIRED)

if(NOT LIBPLUGIN_DIR)
    if(DEFINED ENV{COPPELIASIM_ROOT_DIR})
        set(LIBPLUGIN_DIR $ENV{COPPELIASIM_ROOT_DIR}/programming/libPlugin)
    else()
        set(LIBPLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../libPlugin)
    endif()
endif()
list(APPEND CMAKE_MODULE_PATH ${LIBPLUGIN_DIR}/cmake)
find_package(CoppeliaSim 4.1.0.0 REQUIRED)

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
    ${COPPELIASIM_ROOT_DIR}/programming/simMath/3Vector.cpp
    ${COPPELIASIM_ROOT_DIR}/programming/simMath/3X3Matrix.cpp
    ${COPPELIASIM_ROOT_DIR}/programming/simMath/4Vector.cpp
    ${COPPELIASIM_ROOT_DIR}/programming/simMath/4X4FullMatrix.cpp
    ${COPPELIASIM_ROOT_DIR}/programming/simMath/4X4Matrix.cpp
    ${COPPELIASIM_ROOT_DIR}/programming/simMath/7Vector.cpp
    ${COPPELIASIM_ROOT_DIR}/programming/simMath/MyMath.cpp
)
target_include_directories(simExtOpenGL3Renderer PRIVATE ${COPPELIASIM_ROOT_DIR}/programming/simMath)
target_link_libraries(simExtOpenGL3Renderer Qt5::Widgets Qt5::OpenGL Qt5::PrintSupport OpenGL::GL)
