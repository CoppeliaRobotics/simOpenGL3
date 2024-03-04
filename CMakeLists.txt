cmake_minimum_required(VERSION 3.16.3)
project(simOpenGL3)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(Qt Qt5 CACHE STRING "Qt version to use (e.g. Qt5)")
set_property(CACHE Qt PROPERTY STRINGS Qt5 Qt6)
find_package(${Qt} COMPONENTS Widgets OpenGL PrintSupport REQUIRED)

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
find_package(CoppeliaSim 4.5.1.7 REQUIRED)

coppeliasim_add_plugin(
    simOpenGL3
    LEGACY
    SOURCES
    sourceCode/simOpenGL3.cpp
    sourceCode/light.cpp
    sourceCode/mesh.cpp
    sourceCode/shaderProgram.cpp
    sourceCode/texture.cpp
    sourceCode/frameBufferObject.cpp
    sourceCode/offscreenGlContext.cpp
    sourceCode/openglOffscreen.cpp
    sourceCode/openglBase.cpp
    resources/res.qrc
    ${COPPELIASIM_INCLUDE_DIR}/simMath/3Vector.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/3X3Matrix.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/4Vector.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/4X4Matrix.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/mXnMatrix.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/7Vector.cpp
    ${COPPELIASIM_INCLUDE_DIR}/simMath/mathFuncs.cpp
)
target_link_libraries(simOpenGL3 PRIVATE ${Qt}::Widgets ${Qt}::OpenGL ${Qt}::PrintSupport OpenGL::GL)
coppeliasim_add_lua(lua/simOpenGL3.lua)
