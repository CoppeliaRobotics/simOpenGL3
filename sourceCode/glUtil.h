// Thin RAII wrappers around the handful of GL objects this renderer needs.
//
// Design notes
// ------------
// * Every object is created and destroyed *while its owning context is
//   current*, which is why destroy() takes the function table by reference
//   instead of doing work in a destructor.  The legacy plugin deleted GL
//   objects from destructors running under whatever context happened to be
//   current, which is undefined behaviour.
// * A single GLFuncs table is created per GL context; all helpers take it as a
//   parameter.  This is the Qt-free replacement for the per-object
//   initializeOpenGLFunctions() calls of the legacy code.

#pragma once

#include "glApi.h"
#include "math3d.h"

#include <string>
#include <unordered_map>

namespace modern
{

using GLFuncs = gl::GLFuncs;

// ---------------------------------------------------------------------------
// Diagnostics helpers
// ---------------------------------------------------------------------------
void logGLError(GLFuncs& gl, const char* stage);

// Returns true (and logs) when a GL error is pending.
bool checkGLError(GLFuncs& gl, const char* stage);

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------
class GpuBuffer
{
public:
    GpuBuffer() = default;
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    ~GpuBuffer() { m_id = 0; }

    GLuint id() const { return m_id; }
    GLsizeiptr capacity() const { return m_capacity; }

    void create(GLFuncs& gl);
    void destroy(GLFuncs& gl);

    // (Re)allocates storage, discarding the previous content.
    void allocate(GLFuncs& gl, GLenum target, GLsizeiptr bytes, GLenum usage);
    // Uploads into an already allocated buffer.
    void upload(GLFuncs& gl, GLenum target, GLintptr offset, GLsizeiptr bytes, const void* data);
    // Uploads the whole buffer, growing it when needed.
    void uploadStreaming(GLFuncs& gl, GLenum target, GLsizeiptr bytes, const void* data);

    void bind(GLFuncs& gl, GLenum target) const;
    void bindBase(GLFuncs& gl, GLenum target, GLuint index) const;

private:
    GLuint m_id = 0;
    GLsizeiptr m_capacity = 0;
};

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
class GpuTexture
{
public:
    GpuTexture() = default;
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    ~GpuTexture() { m_id = 0; }

    GLuint id() const { return m_id; }
    GLenum target() const { return m_target; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int layers() const { return m_layers; }
    bool isValid() const { return m_id != 0; }

    void create2D(GLFuncs& gl, int width, int height, GLenum internalFormat, GLenum format,
                  GLenum type, const void* pixels, bool generateMipmaps);
    void createDepthArray(GLFuncs& gl, int width, int height, int layers, bool comparisonMode);
    void destroy(GLFuncs& gl);

    void bind(GLFuncs& gl, GLuint unit) const;

private:
    GLuint m_id = 0;
    GLenum m_target = GL_TEXTURE_2D;
    int m_width = 0;
    int m_height = 0;
    int m_layers = 0;
};

// ---------------------------------------------------------------------------
// Framebuffers
// ---------------------------------------------------------------------------
class GpuFramebuffer
{
public:
    GpuFramebuffer() = default;
    GpuFramebuffer(const GpuFramebuffer&) = delete;
    GpuFramebuffer& operator=(const GpuFramebuffer&) = delete;

    ~GpuFramebuffer() { m_id = 0; }

    GLuint id() const { return m_id; }

    void create(GLFuncs& gl);
    void destroy(GLFuncs& gl);
    void bind(GLFuncs& gl) const;
    static void bindDefault(GLFuncs& gl);

    void attachColorTexture(GLFuncs& gl, GLenum target, GLuint texture, int level = 0);
    void attachDepthTexture(GLFuncs& gl, GLenum target, GLuint texture, int level = 0,
                            int layer = -1);
    void attachColorRenderbuffer(GLFuncs& gl, GLuint renderbuffer);
    void attachDepthRenderbuffer(GLFuncs& gl, GLuint renderbuffer);
    void attachDepthLayer(GLFuncs& gl, GLuint texture, int layer);

    // Depth-only framebuffers must explicitly disable the colour attachments.
    void disableColorAttachment(GLFuncs& gl);

    bool isComplete(GLFuncs& gl, const char* tag) const;

private:
    GLuint m_id = 0;
};

// ---------------------------------------------------------------------------
// Renderbuffers (only used for the multisampled attachments)
// ---------------------------------------------------------------------------
class GpuRenderbuffer
{
public:
    GpuRenderbuffer() = default;
    GpuRenderbuffer(const GpuRenderbuffer&) = delete;
    GpuRenderbuffer& operator=(const GpuRenderbuffer&) = delete;

    ~GpuRenderbuffer() { m_id = 0; }

    GLuint id() const { return m_id; }

    void create(GLFuncs& gl);
    void allocate(GLFuncs& gl, GLenum internalFormat, int width, int height, int samples);
    void destroy(GLFuncs& gl);

private:
    GLuint m_id = 0;
};

// ---------------------------------------------------------------------------
// Shader program built from source strings
//
// The sources come from shaders_embedded.h, generated from resources/shaders/*
// by tools/embed_shaders.cmake, so there is no Qt resource system and no file
// loading at run time.
// ---------------------------------------------------------------------------
class GpuProgram
{
public:
    GpuProgram() = default;
    GpuProgram(const GpuProgram&) = delete;
    GpuProgram& operator=(const GpuProgram&) = delete;

    ~GpuProgram() { m_id = 0; }

    GLuint id() const { return m_id; }
    bool isValid() const { return m_id != 0; }

    bool build(GLFuncs& gl, const char* vertexSource, const char* fragmentSource,
               const char* debugName);
    void destroy(GLFuncs& gl);

    void use(GLFuncs& gl) const;

    // Cached uniform lookup; returns -1 when the uniform is not active.
    int location(GLFuncs& gl, const char* name);

    // Returns false when the block is not active in the linked program (which
    // silently breaks every uniform it carries).
    bool bindUniformBlock(GLFuncs& gl, const char* blockName, GLuint binding);

    // Convenience setters that tolerate -1 locations and non-active uniforms.
    void set(GLFuncs& gl, int location, float value) const;
    void set(GLFuncs& gl, int location, int value) const;
    void set(GLFuncs& gl, int location, const Vec3& value) const;
    void set(GLFuncs& gl, int location, const Vec3& value, float w) const;
    void set(GLFuncs& gl, int location, const Mat4& value) const;

    const std::string& lastError() const { return m_lastError; }

private:
    static GLuint compile(GLFuncs& gl, GLenum type, const char* source, const char* debugName,
                          std::string* error);
    bool link(GLFuncs& gl, GLuint vertexShader, GLuint fragmentShader, std::string* error);

    GLuint m_id = 0;
    std::unordered_map<std::string, int> m_uniforms;
    std::string m_lastError;
};

// ---------------------------------------------------------------------------
// Pixel readback through a pixel pack buffer.
//
// glReadPixels() has to synchronise with the GPU no matter what, but writing
// into a PBO first lets the driver use DMA instead of stalling on a cache
// invalidate of client memory.  A fence is deliberately *not* used:
// CoppeliaSim needs the pixels before this callback returns, so there is
// nothing to overlap.
//
// A frame usually reads two buffers (colour and depth).  Each map of a PBO
// waits for the GPU, so the two reads are queued first and fetched afterwards:
// that costs one synchronisation instead of two.
// ---------------------------------------------------------------------------
class PixelReader
{
public:
    void ensureCapacity(GLFuncs& gl, GLsizeiptr bytes);
    void ensureCapacityFor(GLFuncs& gl, int slot, GLsizeiptr bytes);
    void destroy(GLFuncs& gl);

    // Queues a read into one of the reader's buffers; returns a slot id.
    int queue(GLFuncs& gl, GLuint framebuffer, int width, int height, GLenum format, GLenum type,
              GLsizeiptr bytes);
    // Copies a queued read into destination (and waits for the GPU).
    bool fetch(GLFuncs& gl, int slot, GLsizeiptr bytes, void* destination);

    // Queue + fetch in one call, for single buffer reads.
    void readPixels(GLFuncs& gl, GLuint framebuffer, int width, int height, GLenum format,
                    GLenum type, GLsizeiptr bytes, void* destination);

private:
    static constexpr int kSlots = 2;
    GpuBuffer m_buffers[kSlots];
    GLsizeiptr m_pending[kSlots] = {0, 0};
    int m_nextSlot = 0;
    int m_generation = 0; // invalidates slots between frames
};

// ---------------------------------------------------------------------------
// Small maths helpers
// ---------------------------------------------------------------------------

// Builds the world matrix of a CoppeliaSim pose.  CoppeliaSim stores
// quaternions as (w, x, y, z) and rotations follow the right-handed
// convention used by CQuaternion::getMatrix().
Mat4 poseToMatrix(const float quaternion[4], const float position[3]);

Vec3 linearFromSrgb(const Vec3& srgb);
Vec3 linearFromSrgb(const float rgb[3]);

} // namespace modern
