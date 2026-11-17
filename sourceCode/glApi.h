// Minimal, self-contained OpenGL 3.3 core binding.
//
// Replaces QOpenGLExtraFunctions so that the renderer has no Qt dependency:
// the function table is loaded at runtime through a caller supplied lookup,
// which keeps the platform specifics (WGL / GLX / EGL / dlsym) out of the
// renderer and lets the same code run against a Qt-owned context during the
// migration.
//
// Only the entry points and constants the renderer actually uses are declared,
// so there is no generated 100k line loader in the tree.

#pragma once

// ---------------------------------------------------------------------------
// Types and constants
// ---------------------------------------------------------------------------

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#define GLAPIENTRY APIENTRY
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#define GLAPIENTRY
#else
#include <GL/gl.h>
#define GLAPIENTRY
#endif

#include <cstddef>

// GL 1.1 headers do not declare the sized integer types introduced with OpenGL
// 1.5/2.0; the extension headers do.  Apple's gl3.h already provides them, and
// elsewhere the definitions below are identical to the ones in glext.h, which
// makes the duplicate typedefs legal when both are visible.
#if !defined(__APPLE__)
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;
typedef char GLchar;
#endif

// The system headers of Windows (GL 1.1) and of many Linux distributions stop
// well before GL 3.3, so the missing enumerants are filled in here.  The
// guards keep this a no-op where the platform already defines them.
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#endif
#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#endif
#ifndef GL_COMPARE_REF_TO_TEXTURE
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#define GL_PROGRAM_POINT_SIZE 0x8642 // GL 3.2; the 1.1 headers do not have it
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_FRAMEBUFFER_UNDEFINED
#define GL_FRAMEBUFFER_UNDEFINED 0x8219
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER 0x8CDB
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER 0x8CDC
#endif
#ifndef GL_FRAMEBUFFER_UNSUPPORTED
#define GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE 0x8D56
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif
#ifndef GL_NEAREST_MIPMAP_NEAREST
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#endif
#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL 0x8037
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05
#endif
#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01
#endif
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif
#ifndef GL_INVALID_FRAMEBUFFER_OPERATION
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506
#endif
#ifndef GL_OUT_OF_MEMORY
#define GL_OUT_OF_MEMORY 0x0505
#endif

namespace modern
{
namespace gl
{

// ---------------------------------------------------------------------------
// Entry point signatures
// ---------------------------------------------------------------------------

typedef void(GLAPIENTRY* PFN_glActiveTexture)(GLenum);
typedef void(GLAPIENTRY* PFN_glAttachShader)(GLuint, GLuint);
typedef void(GLAPIENTRY* PFN_glBindBuffer)(GLenum, GLuint);
typedef void(GLAPIENTRY* PFN_glBindBufferBase)(GLenum, GLuint, GLuint);
typedef void(GLAPIENTRY* PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void(GLAPIENTRY* PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void(GLAPIENTRY* PFN_glBindTexture)(GLenum, GLuint);
typedef void(GLAPIENTRY* PFN_glBindVertexArray)(GLuint);
typedef void(GLAPIENTRY* PFN_glBlendEquation)(GLenum);
typedef void(GLAPIENTRY* PFN_glBlendFunc)(GLenum, GLenum);
typedef void(GLAPIENTRY* PFN_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                                                GLint, GLbitfield, GLenum);
typedef void(GLAPIENTRY* PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void(GLAPIENTRY* PFN_glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef GLenum(GLAPIENTRY* PFN_glCheckFramebufferStatus)(GLenum);
typedef void(GLAPIENTRY* PFN_glClear)(GLbitfield);
typedef void(GLAPIENTRY* PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(GLAPIENTRY* PFN_glClearDepthf)(GLfloat);
typedef void(GLAPIENTRY* PFN_glCompileShader)(GLuint);
typedef GLuint(GLAPIENTRY* PFN_glCreateProgram)(void);
typedef GLuint(GLAPIENTRY* PFN_glCreateShader)(GLenum);
typedef void(GLAPIENTRY* PFN_glCullFace)(GLenum);
typedef void(GLAPIENTRY* PFN_glDeleteBuffers)(GLsizei, const GLuint*);
typedef void(GLAPIENTRY* PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void(GLAPIENTRY* PFN_glDeleteProgram)(GLuint);
typedef void(GLAPIENTRY* PFN_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void(GLAPIENTRY* PFN_glDeleteShader)(GLuint);
typedef void(GLAPIENTRY* PFN_glDeleteTextures)(GLsizei, const GLuint*);
typedef void(GLAPIENTRY* PFN_glDeleteVertexArrays)(GLsizei, const GLuint*);
typedef void(GLAPIENTRY* PFN_glDepthFunc)(GLenum);
typedef void(GLAPIENTRY* PFN_glDepthMask)(GLboolean);
typedef void(GLAPIENTRY* PFN_glDisable)(GLenum);
typedef void(GLAPIENTRY* PFN_glDisableVertexAttribArray)(GLuint);
typedef void(GLAPIENTRY* PFN_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void(GLAPIENTRY* PFN_glDrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
typedef void(GLAPIENTRY* PFN_glDrawBuffers)(GLsizei, const GLenum*);
typedef void(GLAPIENTRY* PFN_glEnable)(GLenum);
typedef void(GLAPIENTRY* PFN_glEnableVertexAttribArray)(GLuint);
typedef void(GLAPIENTRY* PFN_glFinish)(void);
typedef void(GLAPIENTRY* PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void(GLAPIENTRY* PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void(GLAPIENTRY* PFN_glFramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint);
typedef void(GLAPIENTRY* PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void(GLAPIENTRY* PFN_glGenerateMipmap)(GLenum);
typedef void(GLAPIENTRY* PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void(GLAPIENTRY* PFN_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void(GLAPIENTRY* PFN_glGenTextures)(GLsizei, GLuint*);
typedef void(GLAPIENTRY* PFN_glGenVertexArrays)(GLsizei, GLuint*);
typedef GLenum(GLAPIENTRY* PFN_glGetError)(void);
typedef const GLubyte*(GLAPIENTRY* PFN_glGetString)(GLenum);
typedef void(GLAPIENTRY* PFN_glGetIntegerv)(GLenum, GLint*);
typedef void(GLAPIENTRY* PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void(GLAPIENTRY* PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void(GLAPIENTRY* PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void(GLAPIENTRY* PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint(GLAPIENTRY* PFN_glGetUniformBlockIndex)(GLuint, const GLchar*);
typedef GLint(GLAPIENTRY* PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void(GLAPIENTRY* PFN_glLinkProgram)(GLuint);
typedef void*(GLAPIENTRY* PFN_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef void(GLAPIENTRY* PFN_glPixelStorei)(GLenum, GLint);
typedef void(GLAPIENTRY* PFN_glPolygonOffset)(GLfloat, GLfloat);
typedef void(GLAPIENTRY* PFN_glReadBuffer)(GLenum);
typedef void(GLAPIENTRY* PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void(GLAPIENTRY* PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void(GLAPIENTRY* PFN_glRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei,
                                                               GLsizei);
typedef void(GLAPIENTRY* PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void(GLAPIENTRY* PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                                           GLenum, const void*);
typedef void(GLAPIENTRY* PFN_glTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint,
                                           GLenum, GLenum, const void*);
typedef void(GLAPIENTRY* PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef void(GLAPIENTRY* PFN_glUniform1f)(GLint, GLfloat);
typedef void(GLAPIENTRY* PFN_glUniform1i)(GLint, GLint);
typedef void(GLAPIENTRY* PFN_glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void(GLAPIENTRY* PFN_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(GLAPIENTRY* PFN_glUniformBlockBinding)(GLuint, GLuint, GLuint);
typedef void(GLAPIENTRY* PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef GLboolean(GLAPIENTRY* PFN_glUnmapBuffer)(GLenum);
typedef void(GLAPIENTRY* PFN_glUseProgram)(GLuint);
typedef void(GLAPIENTRY* PFN_glVertexAttribDivisor)(GLuint, GLuint);
typedef void(GLAPIENTRY* PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                                                    const void*);
typedef void(GLAPIENTRY* PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void(GLAPIENTRY* PFN_glPointSize)(GLfloat);

#define MODERN_GL_FUNCTIONS(X) \
    X(ActiveTexture)           \
    X(AttachShader)            \
    X(BindBuffer)              \
    X(BindBufferBase)          \
    X(BindFramebuffer)         \
    X(BindRenderbuffer)        \
    X(BindTexture)             \
    X(BindVertexArray)         \
    X(BlendEquation)           \
    X(BlendFunc)               \
    X(BlitFramebuffer)         \
    X(BufferData)              \
    X(BufferSubData)           \
    X(CheckFramebufferStatus)  \
    X(Clear)                   \
    X(ClearColor)              \
    X(ClearDepthf)             \
    X(CompileShader)           \
    X(CreateProgram)           \
    X(CreateShader)            \
    X(CullFace)                \
    X(DeleteBuffers)           \
    X(DeleteFramebuffers)      \
    X(DeleteProgram)           \
    X(DeleteRenderbuffers)     \
    X(DeleteShader)            \
    X(DeleteTextures)          \
    X(DeleteVertexArrays)      \
    X(DepthFunc)               \
    X(DepthMask)               \
    X(Disable)                 \
    X(DisableVertexAttribArray) \
    X(DrawArrays)              \
    X(DrawArraysInstanced)     \
    X(DrawBuffers)             \
    X(Enable)                  \
    X(EnableVertexAttribArray) \
    X(Finish)                  \
    X(FramebufferRenderbuffer) \
    X(FramebufferTexture2D)    \
    X(FramebufferTextureLayer) \
    X(GenBuffers)              \
    X(GenerateMipmap)          \
    X(GenFramebuffers)         \
    X(GenRenderbuffers)        \
    X(GenTextures)             \
    X(GenVertexArrays)         \
    X(GetError)                \
    X(GetString)               \
    X(GetIntegerv)             \
    X(GetProgramInfoLog)       \
    X(GetProgramiv)            \
    X(GetShaderInfoLog)        \
    X(GetShaderiv)             \
    X(GetUniformBlockIndex)    \
    X(GetUniformLocation)      \
    X(LinkProgram)             \
    X(MapBufferRange)          \
    X(PixelStorei)             \
    X(PointSize)               \
    X(PolygonOffset)           \
    X(ReadBuffer)              \
    X(ReadPixels)              \
    X(RenderbufferStorage)     \
    X(RenderbufferStorageMultisample) \
    X(ShaderSource)            \
    X(TexImage2D)              \
    X(TexImage3D)              \
    X(TexParameteri)           \
    X(Uniform1f)               \
    X(Uniform1i)               \
    X(Uniform3f)               \
    X(Uniform4f)               \
    X(UniformBlockBinding)     \
    X(UniformMatrix4fv)        \
    X(UnmapBuffer)             \
    X(UseProgram)              \
    X(VertexAttribDivisor)     \
    X(VertexAttribPointer)     \
    X(Viewport)

// ---------------------------------------------------------------------------
// The table itself.
//
// The member names are the GL names, so call sites keep reading
// "gl.glDrawArraysInstanced(...)" exactly like they did with QOpenGLExtraFunctions.
// ---------------------------------------------------------------------------
struct GLFuncs
{
#define DECLARE(name) PFN_gl##name gl##name = nullptr;
    MODERN_GL_FUNCTIONS(DECLARE)
#undef DECLARE

    using Lookup = void* (*)(const char* name);

    // Resolves every entry point.  On return, `missing` holds the name of the
    // first one that could not be found (empty on success).
    bool load(Lookup lookup, const char** missing = nullptr);

    // Name of the loader that produced this table, for logging.
    const char* loaderName = "unknown";
};

} // namespace gl

using GLFuncs = gl::GLFuncs;

} // namespace modern


