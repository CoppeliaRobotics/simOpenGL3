#include "offscreenGlContext.h"

COffscreenGlContext::COffscreenGlContext(int resX,int resY,void* otherWidgetToShareResourcesWith,bool usingQGLWidget) : QObject()
{
    _qOffscreenSurface=new QOffscreenSurface();
    if (usingQGLWidget)
    {
        QSurfaceFormat f;
        f.setVersion(3,2);
        f.setSwapBehavior(QSurfaceFormat::SingleBuffer);
        f.setRenderableType(QSurfaceFormat::OpenGL);
        f.setRedBufferSize(8);
        f.setGreenBufferSize(8);
        f.setBlueBufferSize(8);
        f.setAlphaBufferSize(0);
        f.setStencilBufferSize(8);
        f.setDepthBufferSize(24);
        _qOffscreenSurface->setFormat(f);
    }
    _qOffscreenSurface->create();
    _qContext=nullptr;

    if (_qOffscreenSurface->isValid())
    {
        _qContext= new QOpenGLContext();

        if (usingQGLWidget)
            _qContext->setFormat(_qOffscreenSurface->format());
        if (otherWidgetToShareResourcesWith!=nullptr)
        {
            if (usingQGLWidget)
                _qContext->setShareContext(((QGLWidget*)otherWidgetToShareResourcesWith)->context()->contextHandle());
            else
                _qContext->setShareContext(((QOpenGLWidget*)otherWidgetToShareResourcesWith)->context());
        }

        _qContext->create();
        makeCurrent();
    }
}

COffscreenGlContext::~COffscreenGlContext()
{
    if (_qContext!=nullptr)
        delete _qContext;
    _qOffscreenSurface->destroy();
    delete _qOffscreenSurface;
}

bool COffscreenGlContext::makeCurrent()
{
    if (_qContext!=nullptr)
        _qContext->makeCurrent(_qOffscreenSurface);
    return(true);
}

bool COffscreenGlContext::doneCurrent()
{
    if (_qContext!=nullptr)
    {
        _qContext->swapBuffers(_qOffscreenSurface);
        _qContext->doneCurrent();
    }
    return(true);
}
