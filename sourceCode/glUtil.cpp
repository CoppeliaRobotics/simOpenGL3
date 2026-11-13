#include "glUtil.h"
#include "log.h"

#include <cmath>
#include <cstring>

namespace modern
{
namespace
{

const char* glErrorName(GLenum error)
{
    switch (error)
    {
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    default: return "GL_UNKNOWN_ERROR";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

bool checkGLError(GLFuncs& gl, const char* stage)
{
    const GLenum error = gl.glGetError();
    if (error == GL_NO_ERROR)
        return false;
    logWarning("GL error %s during %s", glErrorName(error), stage);
    return true;
}

void logGLError(GLFuncs& gl, const char* stage)
{
    while (checkGLError(gl, stage))
    {
    }
}

// ---------------------------------------------------------------------------
// GpuBuffer
// ---------------------------------------------------------------------------

void GpuBuffer::create(GLFuncs& gl)
{
    if (m_id == 0)
        gl.glGenBuffers(1, &m_id);
}

void GpuBuffer::destroy(GLFuncs& gl)
{
    if (m_id != 0)
    {
        gl.glDeleteBuffers(1, &m_id);
        m_id = 0;
        m_capacity = 0;
    }
}

void GpuBuffer::allocate(GLFuncs& gl, GLenum target, GLsizeiptr bytes, GLenum usage)
{
    create(gl);
    gl.glBindBuffer(target, m_id);
    gl.glBufferData(target, bytes, nullptr, usage);
    m_capacity = bytes;
}

void GpuBuffer::upload(GLFuncs& gl, GLenum target, GLintptr offset, GLsizeiptr bytes,
                       const void* data)
{
    gl.glBindBuffer(target, m_id);
    gl.glBufferSubData(target, offset, bytes, data);
}

void GpuBuffer::uploadStreaming(GLFuncs& gl, GLenum target, GLsizeiptr bytes, const void* data)
{
    create(gl);
    gl.glBindBuffer(target, m_id);
    if (bytes > m_capacity)
    {
        gl.glBufferData(target, bytes, data, GL_STREAM_DRAW);
        m_capacity = bytes;
    }
    else
    {
        // Orphan the previous storage first: the driver can then hand us a
        // fresh block instead of waiting for the in-flight draw calls.
        gl.glBufferData(target, m_capacity, nullptr, GL_STREAM_DRAW);
        if (bytes > 0)
            gl.glBufferSubData(target, 0, bytes, data);
    }
}

void GpuBuffer::bind(GLFuncs& gl, GLenum target) const
{
    gl.glBindBuffer(target, m_id);
}

void GpuBuffer::bindBase(GLFuncs& gl, GLenum target, GLuint index) const
{
    gl.glBindBufferBase(target, index, m_id);
}

// ---------------------------------------------------------------------------
// GpuTexture
// ---------------------------------------------------------------------------

void GpuTexture::create2D(GLFuncs& gl, int width, int height, GLenum internalFormat,
                          GLenum format, GLenum type, const void* pixels, bool generateMipmaps)
{
    if (m_id == 0)
        gl.glGenTextures(1, &m_id);

    m_target = GL_TEXTURE_2D;
    m_width = width;
    m_height = height;
    m_layers = 1;

    gl.glBindTexture(GL_TEXTURE_2D, m_id);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GLint(internalFormat), width, height, 0, format, type,
                    pixels);

    if (generateMipmaps)
    {
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gl.glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.glBindTexture(GL_TEXTURE_2D, 0);
}

void GpuTexture::createDepthArray(GLFuncs& gl, int width, int height, int layers,
                                  bool comparisonMode)
{
    if (m_id == 0)
        gl.glGenTextures(1, &m_id);

    m_target = GL_TEXTURE_2D_ARRAY;
    m_width = width;
    m_height = height;
    m_layers = layers;

    gl.glBindTexture(GL_TEXTURE_2D_ARRAY, m_id);
    gl.glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, width, height, layers, 0,
                    GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

    // Hardware PCF: the comparison is done by the sampler, and GL_LINEAR
    // turns every tap into a free 2x2 average of the comparison results.
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE,
                       comparisonMode ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void GpuTexture::destroy(GLFuncs& gl)
{
    if (m_id != 0)
    {
        gl.glDeleteTextures(1, &m_id);
        m_id = 0;
        m_width = m_height = m_layers = 0;
    }
}

void GpuTexture::bind(GLFuncs& gl, GLuint unit) const
{
    gl.glActiveTexture(GL_TEXTURE0 + unit);
    gl.glBindTexture(m_target, m_id);
}

// ---------------------------------------------------------------------------
// GpuFramebuffer
// ---------------------------------------------------------------------------

void GpuFramebuffer::create(GLFuncs& gl)
{
    if (m_id == 0)
        gl.glGenFramebuffers(1, &m_id);
}

void GpuFramebuffer::destroy(GLFuncs& gl)
{
    if (m_id != 0)
    {
        gl.glDeleteFramebuffers(1, &m_id);
        m_id = 0;
    }
}

void GpuFramebuffer::bind(GLFuncs& gl) const
{
    gl.glBindFramebuffer(GL_FRAMEBUFFER, m_id);
}

void GpuFramebuffer::bindDefault(GLFuncs& gl)
{
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GpuFramebuffer::attachColorTexture(GLFuncs& gl, GLenum target, GLuint texture, int level)
{
    bind(gl);
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, texture, level);
}

void GpuFramebuffer::attachDepthTexture(GLFuncs& gl, GLenum target, GLuint texture, int level,
                                        int layer)
{
    bind(gl);
    if (layer >= 0)
        gl.glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture, level, layer);
    else
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, texture, level);
}

void GpuFramebuffer::attachColorRenderbuffer(GLFuncs& gl, GLuint renderbuffer)
{
    bind(gl);
    gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                 renderbuffer);
}

void GpuFramebuffer::attachDepthRenderbuffer(GLFuncs& gl, GLuint renderbuffer)
{
    bind(gl);
    gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                 renderbuffer);
}

void GpuFramebuffer::attachDepthLayer(GLFuncs& gl, GLuint texture, int layer)
{
    bind(gl);
    gl.glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture, 0, layer);
}

void GpuFramebuffer::disableColorAttachment(GLFuncs& gl)
{
    bind(gl);
    const GLenum none = GL_NONE;
    gl.glDrawBuffers(1, &none);
    gl.glReadBuffer(GL_NONE);
}

bool GpuFramebuffer::isComplete(GLFuncs& gl, const char* tag) const
{
    bind(gl);
    const GLenum status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE)
        return true;

    const char* reason = "unknown";
    switch (status)
    {
    case GL_FRAMEBUFFER_UNDEFINED: reason = "GL_FRAMEBUFFER_UNDEFINED"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: reason = "INCOMPLETE_ATTACHMENT"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: reason = "MISSING_ATTACHMENT"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: reason = "INCOMPLETE_DRAW_BUFFER"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: reason = "INCOMPLETE_READ_BUFFER"; break;
    case GL_FRAMEBUFFER_UNSUPPORTED: reason = "FRAMEBUFFER_UNSUPPORTED"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: reason = "INCOMPLETE_MULTISAMPLE"; break;
    default: break;
    }
    logWarning("framebuffer %s incomplete: %s", tag, reason);
    return false;
}

// ---------------------------------------------------------------------------
// GpuRenderbuffer
// ---------------------------------------------------------------------------

void GpuRenderbuffer::create(GLFuncs& gl)
{
    if (m_id == 0)
        gl.glGenRenderbuffers(1, &m_id);
}

void GpuRenderbuffer::allocate(GLFuncs& gl, GLenum internalFormat, int width, int height,
                               int samples)
{
    create(gl);
    gl.glBindRenderbuffer(GL_RENDERBUFFER, m_id);
    if (samples > 1)
        gl.glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internalFormat, width,
                                            height);
    else
        gl.glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);
    gl.glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void GpuRenderbuffer::destroy(GLFuncs& gl)
{
    if (m_id != 0)
    {
        gl.glDeleteRenderbuffers(1, &m_id);
        m_id = 0;
    }
}

// ---------------------------------------------------------------------------
// GpuProgram
// ---------------------------------------------------------------------------

GLuint GpuProgram::compile(GLFuncs& gl, GLenum type, const char* source, const char* debugName,
                           std::string* error)
{
    if (source == nullptr || source[0] == '\0')
    {
        *error = std::string(debugName) + ": empty shader source";
        return 0;
    }

    const GLuint shader = gl.glCreateShader(type);
    const GLint sourceLength = GLint(std::strlen(source));
    gl.glShaderSource(shader, 1, &source, &sourceLength);
    gl.glCompileShader(shader);

    GLint compiled = GL_FALSE;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE)
    {
        GLint logLength = 0;
        gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(std::size_t(logLength > 1 ? logLength : 1), '\0');
        gl.glGetShaderInfoLog(shader, GLsizei(log.size()), nullptr, &log[0]);
        *error = std::string(debugName) + ": " + log;
        gl.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool GpuProgram::link(GLFuncs& gl, GLuint vertexShader, GLuint fragmentShader, std::string* error)
{
    gl.glAttachShader(m_id, vertexShader);
    gl.glAttachShader(m_id, fragmentShader);
    gl.glLinkProgram(m_id);

    GLint linked = GL_FALSE;
    gl.glGetProgramiv(m_id, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE)
    {
        GLint logLength = 0;
        gl.glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(std::size_t(logLength > 1 ? logLength : 1), '\0');
        gl.glGetProgramInfoLog(m_id, GLsizei(log.size()), nullptr, &log[0]);
        *error = log;
        return false;
    }
    return true;
}

bool GpuProgram::build(GLFuncs& gl, const char* vertexSource, const char* fragmentSource,
                       const char* debugName)
{
    destroy(gl);

    std::string error;
    const GLuint vertexShader =
        compile(gl, GL_VERTEX_SHADER, vertexSource, debugName, &error);
    if (vertexShader == 0)
    {
        m_lastError = error;
        logError("%s vertex shader failed: %s", debugName, error.c_str());
        return false;
    }

    const GLuint fragmentShader =
        compile(gl, GL_FRAGMENT_SHADER, fragmentSource, debugName, &error);
    if (fragmentShader == 0)
    {
        gl.glDeleteShader(vertexShader);
        m_lastError = error;
        logError("%s fragment shader failed: %s", debugName, error.c_str());
        return false;
    }

    m_id = gl.glCreateProgram();
    const bool ok = link(gl, vertexShader, fragmentShader, &error);
    gl.glDeleteShader(vertexShader);
    gl.glDeleteShader(fragmentShader);

    if (!ok)
    {
        m_lastError = error;
        logError("%s link failed: %s", debugName, error.c_str());
        gl.glDeleteProgram(m_id);
        m_id = 0;
        return false;
    }

    m_uniforms.clear();
    m_lastError.clear();
    return true;
}

void GpuProgram::destroy(GLFuncs& gl)
{
    if (m_id != 0)
    {
        gl.glDeleteProgram(m_id);
        m_id = 0;
    }
    m_uniforms.clear();
}

void GpuProgram::use(GLFuncs& gl) const
{
    gl.glUseProgram(m_id);
}

int GpuProgram::location(GLFuncs& gl, const char* name)
{
    if (m_id == 0)
        return -1;

    const auto it = m_uniforms.find(name);
    if (it != m_uniforms.end())
        return it->second;

    const int uniformLocation = gl.glGetUniformLocation(m_id, name);
    m_uniforms.emplace(name, uniformLocation);
    return uniformLocation;
}

bool GpuProgram::bindUniformBlock(GLFuncs& gl, const char* blockName, GLuint binding)
{
    if (m_id == 0)
        return false;
    const GLuint blockIndex = gl.glGetUniformBlockIndex(m_id, blockName);
    if (blockIndex == GL_INVALID_INDEX)
    {
        logWarning("shader program %u has no active uniform block \"%s\"", m_id, blockName);
        return false;
    }
    gl.glUniformBlockBinding(m_id, blockIndex, binding);
    return true;
}

void GpuProgram::set(GLFuncs& gl, int location, float value) const
{
    if (location >= 0)
        gl.glUniform1f(location, value);
}

void GpuProgram::set(GLFuncs& gl, int location, int value) const
{
    if (location >= 0)
        gl.glUniform1i(location, value);
}

void GpuProgram::set(GLFuncs& gl, int location, const Vec3& value) const
{
    if (location >= 0)
        gl.glUniform3f(location, value.x(), value.y(), value.z());
}

void GpuProgram::set(GLFuncs& gl, int location, const Vec3& value, float w) const
{
    if (location >= 0)
        gl.glUniform4f(location, value.x(), value.y(), value.z(), w);
}

void GpuProgram::set(GLFuncs& gl, int location, const Mat4& value) const
{
    if (location >= 0)
        gl.glUniformMatrix4fv(location, 1, GL_FALSE, value.constData());
}

// ---------------------------------------------------------------------------
// PixelReader
// ---------------------------------------------------------------------------

void PixelReader::ensureCapacity(GLFuncs& gl, GLsizeiptr bytes)
{
    if (m_buffer.capacity() >= bytes)
        return;
    m_buffer.allocate(gl, GL_PIXEL_PACK_BUFFER, bytes, GL_STREAM_READ);
}

void PixelReader::destroy(GLFuncs& gl)
{
    m_buffer.destroy(gl);
}

void PixelReader::readPixels(GLFuncs& gl, GLuint framebuffer, int width, int height, GLenum format,
                             GLenum type, GLsizeiptr bytes, void* destination)
{
    if (bytes <= 0 || destination == nullptr)
        return;

    ensureCapacity(gl, bytes);
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, m_buffer.id());
    gl.glReadPixels(0, 0, width, height, format, type, nullptr);
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, m_buffer.id());
    void* mapped = gl.glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
    if (mapped != nullptr)
    {
        std::memcpy(destination, mapped, std::size_t(bytes));
        gl.glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// Maths
// ---------------------------------------------------------------------------

Mat4 poseToMatrix(const float quaternion[4], const float position[3])
{
    const float w = quaternion[0];
    const float x = quaternion[1];
    const float y = quaternion[2];
    const float z = quaternion[3];

    Mat4 m; // identity
    m(0, 0) = 1.0f - 2.0f * (y * y + z * z);
    m(0, 1) = 2.0f * (x * y - z * w);
    m(0, 2) = 2.0f * (x * z + y * w);
    m(1, 0) = 2.0f * (x * y + z * w);
    m(1, 1) = 1.0f - 2.0f * (x * x + z * z);
    m(1, 2) = 2.0f * (y * z - x * w);
    m(2, 0) = 2.0f * (x * z - y * w);
    m(2, 1) = 2.0f * (y * z + x * w);
    m(2, 2) = 1.0f - 2.0f * (x * x + y * y);
    m(0, 3) = position[0];
    m(1, 3) = position[1];
    m(2, 3) = position[2];
    return m;
}

Vec3 linearFromSrgb(const Vec3& srgb)
{
    const auto toLinear = [](float c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    return Vec3(toLinear(srgb.x()), toLinear(srgb.y()), toLinear(srgb.z()));
}

Vec3 linearFromSrgb(const float rgb[3])
{
    return linearFromSrgb(Vec3(rgb[0], rgb[1], rgb[2]));
}

} // namespace modern
