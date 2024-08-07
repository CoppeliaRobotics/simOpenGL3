#pragma once

#include <QtOpenGL>
#include <QOpenGLContext>
#include <QOffscreenSurface>


class COffscreenGlContext : public QObject
{
    Q_OBJECT
public:

    COffscreenGlContext(int resX,int resY,void* otherWidgetToShareResourcesWith,bool usingQGLWidget);
    virtual ~COffscreenGlContext();

    bool makeCurrent();
    bool doneCurrent();

protected:
    // Qt offscreen vars:
    QOpenGLContext* _qContext;
    QOffscreenSurface* _qOffscreenSurface;
};
