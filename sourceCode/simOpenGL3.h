// Modern shader based external renderer plugin for CoppeliaSim.
//
// Drop-in replacement for the legacy simOpenGL3 plugin: it keeps the same
// plugin name, the same exported entry points and the same external renderer
// message protocol, so CoppeliaSim's "OpenGL3" render mode picks it up without
// any change on the engine side.

#pragma once

#include <simLib-2/simTypes.h>
#include <simLib-2/simExp.h>

SIM_DLLEXPORT int simInit(SSimInit*);
SIM_DLLEXPORT void simCleanup();
SIM_DLLEXPORT void simMsg(SSimMsg*);
SIM_DLLEXPORT void simInit_ui();
SIM_DLLEXPORT void simMsg_ui(SSimMsg_ui*);
SIM_DLLEXPORT void simCleanup_ui();

// The name matters: CoppeliaSim resolves this exact symbol when the render
// mode is sim_rendermode_opengl3.
SIM_DLLEXPORT void simOpenGL3Renderer(int message, void* data);
