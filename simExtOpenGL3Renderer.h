#ifndef SIMEXTEXTERNALRENDERER_H
#define SIMEXTEXTERNALRENDERER_H

#include <QtCore/qglobal.h>
#include <simLib/simExp.h>

// The 3 required entry points of the plugin:
SIM_DLLEXPORT unsigned char simStart(void* reservedPointer,int reservedInt);
SIM_DLLEXPORT void simEnd();
SIM_DLLEXPORT void* simMessage(int message,int* auxiliaryData,void* customData,int* replyData);

SIM_DLLEXPORT void simExtRenderer(int message,void* data);
SIM_DLLEXPORT void simExtRendererWindowed(int message,void* data);

#endif // SIMEXTEXTERNALRENDERER_H
