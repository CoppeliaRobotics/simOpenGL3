#include "frameBufferObject.h"
#include <iostream>

CFrameBufferObject::CFrameBufferObject(int resX,int resY) : QObject()
{
    QOpenGLFramebufferObject::Attachment attachment=QOpenGLFramebufferObject::Depth;
    _frameBufferObject = new QOpenGLFramebufferObject(resX,resY,attachment,GL_TEXTURE_2D,GL_RGBA8); // GL_RGB);
}

CFrameBufferObject::~CFrameBufferObject()
{
    release();
    delete _frameBufferObject;
}

void CFrameBufferObject::bind()
{
    _frameBufferObject->bind();
}

void CFrameBufferObject::release()
{
    _frameBufferObject->release();
}
