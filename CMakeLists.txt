cmake_minimum_required(VERSION 3.16.3)
project(simOpenGL3)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)

# No Qt: the off-screen context comes from WGL (Windows), EGL (Linux) or CGL
# (macOS), see the platform sources below.

find_package(OpenGL REQUIRED)

if(NOT COPPELIASIM_INCLUDE_DIR)
    if(DEFINED ENV{COPPELIASIM_ROOT_DIR})
        set(COPPELIASIM_INCLUDE_DIR $ENV{COPPELIASIM_ROOT_DIR}/programming/include)
    else()
        set(COPPELIASIM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH ${COPPELIASIM_INCLUDE_DIR}/cmake)
# simLib-2 is the dynamically loaded plugin interface; no simMath components
# are needed because this renderer does its own vector/matrix maths.
find_package(CoppeliaSim 4.5.1.7 REQUIRED COMPONENTS simLib-2)

set(SIMOPENGL3_SOURCES
    sourceCode/simOpenGL3.cpp
    sourceCode/sceneRenderer.cpp
    sourceCode/shadowAtlas.cpp
    sourceCode/offscreenTarget.cpp
    sourceCode/offscreenContextCommon.cpp
    sourceCode/glUtil.cpp
    sourceCode/glApi.cpp
    sourceCode/log.cpp
    sourceCode/renderSettings.cpp
)

if(WIN32)
    list(APPEND SIMOPENGL3_SOURCES sourceCode/offscreenContextWin32.cpp)
elseif(APPLE)
    list(APPEND SIMOPENGL3_SOURCES sourceCode/offscreenContextCgl.cpp)
else()
    list(APPEND SIMOPENGL3_SOURCES sourceCode/offscreenContextEgl.cpp)
endif()

# The target is called simOpenGL3 on purpose: CoppeliaSim loads the renderer
# plugin by that name and calls its simOpenGL3Renderer() entry point.
coppeliasim_add_resource(simOpenGL3.ini)
coppeliasim_add_plugin(simOpenGL3 LEGACY SOURCES ${SIMOPENGL3_SOURCES})

target_link_libraries(simOpenGL3 PRIVATE OpenGL::GL)
if(WIN32)
    target_link_libraries(simOpenGL3 PRIVATE gdi32 user32)
elseif(APPLE)
    target_link_libraries(simOpenGL3 PRIVATE "-framework OpenGL")
else()
    target_link_libraries(simOpenGL3 PRIVATE dl EGL)
endif()

coppeliasim_add_lua(lua/simOpenGL3.lua)

# Shaders are embedded as C++ string literals so that neither the build nor the
# run time needs a resource compiler.  Regenerate after editing
# resources/shaders/*:
#   cmake -DSHADER_DIR=<dir>/resources/shaders -DOUT=<dir>/sourceCode/shaders_embedded.h -P tools/embed_shaders.cmake
add_custom_target(
    embed_shaders
    COMMAND ${CMAKE_COMMAND}
        -DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders
        -DOUT=${CMAKE_CURRENT_SOURCE_DIR}/sourceCode/shaders_embedded.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.cmake
    COMMENT "Regenerating sourceCode/shaders_embedded.h"
)

# Optional developer tool: renders a synthetic scene and writes smoke.png, so
# that shader, context and framebuffer problems can be caught without
# CoppeliaSim. It uses the same platform backend, so it also runs headless.
option(SIMOPENGL3_BUILD_SMOKE_TEST "Build the stand-alone renderer smoke test" OFF)
if(SIMOPENGL3_BUILD_SMOKE_TEST)
    add_executable(smokeTest
        tools/smokeTest/main.cpp
        sourceCode/sceneRenderer.cpp
        sourceCode/shadowAtlas.cpp
        sourceCode/offscreenTarget.cpp
        sourceCode/offscreenContextCommon.cpp
        sourceCode/glUtil.cpp
        sourceCode/glApi.cpp
        sourceCode/log.cpp
        sourceCode/renderSettings.cpp
    )
    if(WIN32)
        target_sources(smokeTest PRIVATE sourceCode/offscreenContextWin32.cpp)
    elseif(APPLE)
        target_sources(smokeTest PRIVATE sourceCode/offscreenContextCgl.cpp)
    else()
        target_sources(smokeTest PRIVATE sourceCode/offscreenContextEgl.cpp)
    endif()
    target_include_directories(smokeTest PRIVATE sourceCode tools/smokeTest)
    target_link_libraries(smokeTest PRIVATE OpenGL::GL)
    if(WIN32)
        target_link_libraries(smokeTest PRIVATE gdi32 user32)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(smokeTest PRIVATE dl EGL)
    endif()
endif()

# Optional developer tool: checks math3d.h against Qt's matrix classes. Only
# useful while porting, and it needs Qt, so it is off by default.
option(SIMOPENGL3_BUILD_MATH_PARITY "Build the math parity check" OFF)
if(SIMOPENGL3_BUILD_MATH_PARITY)
    set(Qt Qt5 CACHE STRING "Qt version to use (e.g. Qt5)")
    find_package(${Qt} COMPONENTS Gui REQUIRED)
    add_executable(mathParity tools/mathParity/main.cpp)
    target_include_directories(mathParity PRIVATE sourceCode)
    target_link_libraries(mathParity PRIVATE ${Qt}::Gui)
endif()
