#pragma once

#include <QObject>
#include <QOpenGLFramebufferObject>

class CFrameBufferObject : public QObject
{
    Q_OBJECT
public:

    CFrameBufferObject(int resX,int resY);
    virtual ~CFrameBufferObject();

    void bind();
    void release();

protected:
    QOpenGLFramebufferObject* _frameBufferObject;
};
