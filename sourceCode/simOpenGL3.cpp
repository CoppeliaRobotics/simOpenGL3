#include "simOpenGL3.h"
#include <simLib/simLib.h>
#include <simMath/4X4Matrix.h>
#include <simMath/mXnMatrix.h>
#include <iostream>
#include "openglOffscreen.h"
#include <QCoreApplication>
#include <algorithm>
#include "shaderProgram.h"
#include "light.h"
#include "mesh.h"
#include "texture.h"
#include <chrono>

#define MAX_LIGHTS 5

LIBRARY simLib;

// Shaders to be shared between all surfaces.
ShaderProgram* depthShader = NULL;
ShaderProgram* omniShader = NULL;

// Meshes and Textures to be shared between all surfaces.
COcContainer<Mesh>* meshContainer = new COcContainer<Mesh>();
COcContainer<Texture>* textureContainer = new COcContainer<Texture>();
COcContainer<Light>* lightContainer = new COcContainer<Light>();

int resolutionX;
int resolutionY;
float nearClippingPlane;
float farClippingPlane;
bool perspectiveOperation;
int visionSensorOrCameraId;
bool usingQGLWidget;
bool instanceSwitched=false;

int activeDirLightCounter, activePointLightCounter, activeSpotLightCounter;

std::vector<COpenglOffscreen*> oglOffscreens;
COpenglOffscreen* currentOffscreen = NULL;

std::vector<Light*> lightsToRender;
std::vector<Mesh*> meshesToRender;

COpenglOffscreen* getOffscreen(int objectHandle)
{
    for (size_t i=0;i<oglOffscreens.size();i++)
    {
        if (oglOffscreens[i]->getAssociatedObjectHandle()==objectHandle)
            return(oglOffscreens[i]);
    }
    return(NULL);
}

void removeOffscreen(int objectHandle)
{
    for (size_t i=0;i<oglOffscreens.size();i++)
    {
        if (oglOffscreens[i]->getAssociatedObjectHandle()==objectHandle)
        {
            delete lightContainer;
            lightContainer = new COcContainer<Light>();
            delete oglOffscreens[i];
            oglOffscreens.erase(oglOffscreens.begin()+i);
            break;
        }
    }
}

SIM_DLLEXPORT int simInit(SSimInit* info)
{
    simLib=loadSimLibrary(info->coppeliaSimLibPath);
    if (simLib==NULL)
    {
        simAddLog(info->pluginName,sim_verbosity_errors,"could not find or correctly load the CoppeliaSim library. Cannot start the plugin.");
        return(0);
    }
    if (getSimProcAddresses(simLib)==0)
    {
        simAddLog(info->pluginName,sim_verbosity_errors,"could not find all required functions in the CoppeliaSim library. Cannot start the plugin.");
        unloadSimLibrary(simLib);
        return(0);
    }

    usingQGLWidget=simGetBoolParam(sim_boolparam_qglwidget)!=0;

    return(3);  // 3 since CoppeliaSim V4.6
}

SIM_DLLEXPORT void simInit_ui()
{
}

SIM_DLLEXPORT void simCleanup_ui()
{
    delete depthShader;
    delete omniShader;

    meshContainer->removeAll();
    textureContainer->removeAll();
    lightContainer->removeAll();

    delete textureContainer;
    delete meshContainer;
    delete lightContainer;

    for (size_t i=0;i<oglOffscreens.size();i++)
        delete oglOffscreens[i];
}

SIM_DLLEXPORT void simCleanup()
{
    unloadSimLibrary(simLib); // release the library
}

SIM_DLLEXPORT void simMsg(SSimMsg* info)
{
    if (info->msgId==sim_message_eventcallback_instanceswitch)
        instanceSwitched=true;
}

SIM_DLLEXPORT void simMsg_ui(SSimMsg_ui*)
{
    if (instanceSwitched)
    {
        instanceSwitched=false;

        meshContainer->removeAll();
        textureContainer->removeAll();
        lightContainer->removeAll();

        for (size_t i=0;i<oglOffscreens.size();i++)
            delete oglOffscreens[i];

        oglOffscreens.clear();
    }
}

void executeRenderCommands(bool,int message,void* data)
{
    if (message==sim_message_eventcallback_extrenderer_start)
    {
        // Collect camera and environment data from CoppeliaSim:
        void** valPtr=(void**)data;
        resolutionX=((int*)valPtr[0])[0];
        resolutionY=((int*)valPtr[1])[0];
        float* backgroundColor=((float*)valPtr[2]);
        float viewAngle=((float*)valPtr[8])[0];
        perspectiveOperation=(((int*)valPtr[5])[0]==0);
        nearClippingPlane=((float*)valPtr[9])[0];
        farClippingPlane=((float*)valPtr[10])[0];
        float* amb=(float*)valPtr[11];
        C7Vector cameraTranformation(C4Vector((float*)valPtr[4]),C3Vector((float*)valPtr[3]));
        C4X4Matrix m4(cameraTranformation.getMatrix());
        float orthoViewSize=((float*)valPtr[18])[0];
        visionSensorOrCameraId=((int*)valPtr[19])[0];
        int posX=0;
        int posY=0;
        if ((valPtr[20]!=NULL)&&(valPtr[21]!=NULL))
        {
            posX=((int*)valPtr[20])[0];
            posY=((int*)valPtr[21])[0];
        }

        currentOffscreen=getOffscreen(visionSensorOrCameraId);
        if (currentOffscreen!=NULL)
        {
            if ( (!currentOffscreen->isResolutionSame(resolutionX,resolutionY)) )
            {
                removeOffscreen(visionSensorOrCameraId);
                currentOffscreen=NULL;
                meshContainer->removeAll();
                textureContainer->removeAll();
                lightContainer->removeAll();
            }
        }
        if (currentOffscreen==NULL)
        {
            currentOffscreen=new COpenglOffscreen(visionSensorOrCameraId,resolutionX,resolutionY,valPtr[28],usingQGLWidget);
            meshContainer->removeAll();
            oglOffscreens.push_back(currentOffscreen);
        }

        if (currentOffscreen!=NULL)
        {
            currentOffscreen->makeContextCurrent();
            std::string glVersion = std::string((const char*)glGetString(GL_VERSION));
            float version = std::stof(glVersion);
            if (version < 3.2)
            {
                std::string tmp("this renderer requires atleast OpenGL 3.2. The version available is: ");
                tmp+=glVersion;
                simAddLog("OpenGL3Renderer",sim_verbosity_errors,tmp.c_str());
            }

        //    currentOffscreen->initGL();

            if (omniShader == NULL)
            {
                depthShader = new ShaderProgram(":/shadows/depth.vert", ":/shadows/depth.frag", "");
                omniShader = new ShaderProgram(":/shadows/omni_depth.vert", ":/shadows/omni_depth.frag", "");
            }

            currentOffscreen->clearBuffers(viewAngle,orthoViewSize,nearClippingPlane,farClippingPlane,perspectiveOperation,backgroundColor);

            // The following instructions have the same effect as gluLookAt()
            m4.inverse();
            m4.rotateAroundY(3.14159265359f);

            CMatrix m4_(m4);

            // Set the view matrix
            QMatrix4x4 m44 = QMatrix4x4(m4_.data.data());

            currentOffscreen->shader->setUniformValue("view", m44);

            QVector3D sceneAmbientLight = QVector3D(amb[0],amb[1],amb[2]);
            currentOffscreen->shader->setUniformValue("sceneAmbient", sceneAmbientLight);

            activeDirLightCounter=0;
            activePointLightCounter=0;
            activeSpotLightCounter=0;
        }
    }

    if ( (message==sim_message_eventcallback_extrenderer_light)&&(currentOffscreen!=nullptr) )
    {
        // Collect light data from CoppeliaSim (one light at a time):
        void** valPtr=(void**)data;
        int lightType=((int*)valPtr[0])[0];
        float cutoffAngle=((float*)valPtr[1])[0];
        int spotExponent=((int*)valPtr[2])[0];
        float* colors=((float*)valPtr[3]);
        float constAttenuation=((float*)valPtr[4])[0];
        float linAttenuation=((float*)valPtr[5])[0];
        float quadAttenuation=((float*)valPtr[6])[0];
        C7Vector lightTranformation(C4Vector((float*)valPtr[8]),C3Vector((float*)valPtr[7]));
        int lightHandle=((int*)valPtr[13])[0];

        float nearPlane=0.01f;
        float farPlane=10.0f;
        float orthoSize=8.0f;

        // Default values gained by tuning on a selection of environements.
        float bias = 0.001f;
        float normalBias = 0.012f;
        if (lightType == sim_light_directional)
        {
            normalBias = 0.005f;
        }
        else if (lightType == sim_light_spot)
        {
            bias = 0.0f;
            normalBias = 0.00008f;
        }


        // Have the shadow map scale based on the render resolution.
        int shadowTextureSize=std::max(currentOffscreen->_resX,currentOffscreen->_resY) * 4;

        char* str=simGetExtensionString(lightHandle,-1,"nearPlane@lightProjection@openGL3");
        if (str!=nullptr)
        {
            nearPlane=strtof(str,nullptr);
            simReleaseBuffer(str);
        }
        str=simGetExtensionString(lightHandle,-1,"farPlane@lightProjection@openGL3");
        if (str!=nullptr)
        {
            farPlane=strtof(str,nullptr);
            simReleaseBuffer(str);
        }
        str=simGetExtensionString(lightHandle,-1,"orthoSize@lightProjection@openGL3");
        if (str!=nullptr)
        {
            orthoSize=strtof(str,nullptr);
            simReleaseBuffer(str);
        }
        str=simGetExtensionString(lightHandle,-1,"shadowTextureSize@lightProjection@openGL3");
        if (str!=nullptr)
        {
            shadowTextureSize=atoll(str);
            simReleaseBuffer(str);
        }
        str=simGetExtensionString(lightHandle,-1,"bias@lightProjection@openGL3");
        if (str!=nullptr)
        {
            bias=strtof(str,nullptr);
            simReleaseBuffer(str);
        }
        str=simGetExtensionString(lightHandle,-1,"normalBias@lightProjection@openGL3");
        if (str!=nullptr)
        {
            normalBias=strtof(str,nullptr);
            simReleaseBuffer(str);
        }

        // Now set-up that light in OpenGl:
        C4X4Matrix m(lightTranformation.getMatrix());

        int counter = 0;
        if (lightType == sim_light_directional)
        {
            counter = activeDirLightCounter;
            activeDirLightCounter++;
        }
        else if (lightType == sim_light_omnidirectional)
        {
            counter = activePointLightCounter;
            activePointLightCounter++;
        }
        else if (lightType == sim_light_spot)
        {
            counter = activeSpotLightCounter;
            activeSpotLightCounter++;
        }

        int totalCount = activeDirLightCounter + activePointLightCounter + activeSpotLightCounter;
        Light* light = lightContainer->getFromId(lightHandle);
        if(light == NULL)
        {
            light = new Light(lightType, shadowTextureSize);
            lightContainer->add(light);
        }
        light->initForCamera(lightHandle, lightType, m, counter, totalCount, colors, constAttenuation, linAttenuation, quadAttenuation, cutoffAngle, spotExponent, nearPlane, farPlane, orthoSize, shadowTextureSize, bias, normalBias, currentOffscreen->shader);
        light->setPose(lightType, m, currentOffscreen->shader);
        lightsToRender.push_back(light);
    }

    if ( (message==sim_message_eventcallback_extrenderer_mesh)&&(currentOffscreen!=nullptr) )
    {
        // Collect mesh data from CoppeliaSim:
        void** valPtr=(void**)data;
        float* vertices=((float*)valPtr[0]);
        int verticesCnt=((int*)valPtr[1])[0];
        int* indices=((int*)valPtr[2]);
        int triangleCnt=((int*)valPtr[3])[0];
        float* normals=((float*)valPtr[4]);
        int normalsCnt=((int*)valPtr[5])[0];
        float* colors=((float*)valPtr[8]);
        C7Vector tr(C4Vector((float*)valPtr[7]),C3Vector((float*)valPtr[6]));
        bool textured=((bool*)valPtr[18])[0];
        float shadingAngle=((float*)valPtr[19])[0];
        bool translucid=((bool*)valPtr[21])[0];
        float opacityFactor=((float*)valPtr[22])[0];
        bool backfaceCulling=((bool*)valPtr[23])[0];
        int geomId=((int*)valPtr[24])[0];
        int texId=((int*)valPtr[25])[0];
        unsigned char* edges=((unsigned char*)valPtr[26]);
        bool visibleEdges=((bool*)valPtr[27])[0];

        float* texCoords=NULL;
        int texCoordCnt=0;
        bool repeatU=false;
        bool repeatV=false;
        bool interpolateColors=false;
        int applyMode=0;
        Texture* theTexture=NULL;
        if (textured)
        {
            // Read some additional data from CoppeliaSim (i.e. texture data):
            texCoords=((float*)valPtr[9]);
            texCoordCnt=((int*)valPtr[10])[0];
            unsigned char* textureBuff=((unsigned char*)valPtr[11]); // RGBA
            int textureSizeX=((int*)valPtr[12])[0];
            int textureSizeY=((int*)valPtr[13])[0];
            repeatU=((bool*)valPtr[14])[0];
            repeatV=((bool*)valPtr[15])[0];
            interpolateColors=((bool*)valPtr[16])[0];
            applyMode=((int*)valPtr[17])[0];

            theTexture=textureContainer->getFromId(texId);
            if (theTexture==NULL)
            {
                theTexture=new Texture(texId,textureBuff,textureSizeX,textureSizeY);
                textureContainer->add(theTexture);
            }
        }
        Mesh* mesh=meshContainer->getFromId(geomId);
        if (mesh==NULL)
        {
            mesh=new Mesh(geomId,vertices,verticesCnt*3,indices,triangleCnt*3,normals,normalsCnt*3,texCoords,texCoordCnt*2, edges);
            meshContainer->add(mesh);
        }
        mesh->store(tr,colors,textured,shadingAngle,translucid,opacityFactor,backfaceCulling,repeatU,repeatV,interpolateColors,applyMode,theTexture,visibleEdges);
        meshesToRender.push_back(mesh);
    }

    if ( (message==sim_message_eventcallback_extrenderer_stop)&&(currentOffscreen!=nullptr) )
    {
        void** valPtr=(void**)data;
        unsigned char* rgbBuffer=((unsigned char*)valPtr[0]);
        float* depthBuffer=((float*)valPtr[1]);
        bool readRgb=((bool*)valPtr[2])[0];
        bool readDepth=((bool*)valPtr[3])[0];

#ifdef _WIN32
        static PFNGLACTIVETEXTUREPROC glActiveTexture = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
#endif

        if(activeDirLightCounter == 0)
            currentOffscreen->shader->setUniformValue("dirLightLen", 0);
        if(activePointLightCounter == 0)
            currentOffscreen->shader->setUniformValue("pointLightLen", 0);
        if(activeSpotLightCounter == 0)
            currentOffscreen->shader->setUniformValue("spotLightLen", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, currentOffscreen->blankTexture);

        // Bind all of the Sampler2D and SampleCubes to an empty texture.
        int totalCount = activeDirLightCounter + activePointLightCounter + activeSpotLightCounter;
        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            QString depthCubeMaps = "depthCubeMap";
            depthCubeMaps.append(QString::number(i));
            currentOffscreen->shader->setUniformValue(depthCubeMaps, 1);
        }

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, currentOffscreen->blankTexture2);

        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            QString lightName = "spotLight";
            lightName.append(QString::number(i));
            lightName.append(".shadowMap");
            currentOffscreen->shader->setUniformValue(lightName, 2);
        }

        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            QString lightName = "dirLight";
            lightName.append(QString::number(i));
            lightName.append(".shadowMap");
            currentOffscreen->shader->setUniformValue(lightName, 2);
        }

        for (int i=0;i<int(lightsToRender.size());i++)
        {
            ShaderProgram* depthSh = depthShader;
            if (lightsToRender[i]->lightType == sim_light_omnidirectional)
                depthSh = omniShader;

            lightsToRender[i]->renderDepthFromLight(depthSh, meshesToRender);
        }

        currentOffscreen->makeContextCurrent();
        currentOffscreen->bindFramebuffer();
        currentOffscreen->clearViewport();
        currentOffscreen->shader->bind();

        // It seems Sampler2Ds and SamplerCubes cant match to the same ID
        int omnis_seen = 0;
        int spots_seen = 0;
        int dir_seen = 0;

        // Bind all the lighting textures
        for (int i=0;i<int(lightsToRender.size());i++)
        {
            glActiveTexture(GL_TEXTURE3 + i);
            if (lightsToRender[i]->lightType == sim_light_omnidirectional)
            {
                glBindTexture(GL_TEXTURE_CUBE_MAP, lightsToRender[i]->depthMap);
                QString depthCubeMaps = "depthCubeMap";
                depthCubeMaps.append(QString::number(omnis_seen));
                currentOffscreen->shader->setUniformValue(depthCubeMaps, 3+i);
                omnis_seen += 1;
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D, lightsToRender[i]->depthMap);
                if(lightsToRender[i]->lightType == sim_light_spot)
                {
                    QString lightName = "spotLight";
                    lightName.append(QString::number(spots_seen));
                    lightName.append(".shadowMap");
                    currentOffscreen->shader->setUniformValue(lightName, 3+i);
                    spots_seen += 1;
                }
                else if (lightsToRender[i]->lightType == sim_light_directional)
                {
                    QString lightName = "dirLight";
                    lightName.append(QString::number(dir_seen));
                    lightName.append(".shadowMap");
                    currentOffscreen->shader->setUniformValue(lightName, 3+i);
                    dir_seen += 1;
                }
            }
        }

        glActiveTexture(GL_TEXTURE0);
        for (size_t i=0;i<meshesToRender.size();i++)
            meshesToRender[i]->render(currentOffscreen->shader);

        lightsToRender.clear();
        meshesToRender.clear();

        if (readRgb)
        {
            glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,resolutionX,resolutionY,GL_RGB,GL_UNSIGNED_BYTE,rgbBuffer);
            glPixelStorei(GL_PACK_ALIGNMENT,4);
        }
        if (readDepth)
        {
            glReadPixels(0,0,resolutionX,resolutionY,GL_DEPTH_COMPONENT,GL_FLOAT,depthBuffer);
            // Convert this depth info into values corresponding to linear depths (if perspective mode):
            if (perspectiveOperation)
            {
                float farMinusNear= farClippingPlane-nearClippingPlane;
                float farDivFarMinusNear=farClippingPlane/farMinusNear;
                float nearTimesFar=nearClippingPlane*farClippingPlane;
                int v=resolutionX*resolutionY;
                for (int i=0;i<v;i++)
                    depthBuffer[i]=((nearTimesFar/(farMinusNear*(farDivFarMinusNear-depthBuffer[i])))-nearClippingPlane)/farMinusNear;
            }
        }

//        meshContainer->removeAll(); // when objects are dynamically added, might not render in all views... one could always remove them all, which goes 10% slower
//        textureContainer->removeAll();
        lightContainer->removeAll(); // need to remove all for correct shadow rendering
        meshContainer->decrementAllUsedCount();
        meshContainer->removeAllUnused();
        textureContainer->decrementAllUsedCount();
        textureContainer->removeAllUnused();
//        lightContainer->decrementAllUsedCount();
//        lightContainer->removeAllUnused();

        currentOffscreen->unbindFramebuffer();
        currentOffscreen->doneCurrentContext();

        currentOffscreen=NULL;
    }
}

SIM_DLLEXPORT void simOpenGL3Renderer(int message,void* data)
{
    executeRenderCommands(false,message,data);
}
