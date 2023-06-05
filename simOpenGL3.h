#pragma once

#include <QtCore/qglobal.h>
#include <simLib/simExp.h>

// The entry points of the plugin:
SIM_DLLEXPORT int simInit(const char* pluginName);
SIM_DLLEXPORT void simCleanup();
SIM_DLLEXPORT void simMsg(int message,int* auxData,void* pointerData);
SIM_DLLEXPORT void simInit_ui();
SIM_DLLEXPORT void simMsg_ui(int message,int* auxData,void* pointerData);
SIM_DLLEXPORT void simCleanup_ui();

SIM_DLLEXPORT void simExtRenderer(int message,void* data);
SIM_DLLEXPORT void simExtRendererWindowed(int message,void* data);
