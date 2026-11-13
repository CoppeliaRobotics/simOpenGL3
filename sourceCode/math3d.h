// Small vector/matrix library replacing QVector2D/3D/4D, QMatrix3x3 and
// QMatrix4x4.
//
// The API intentionally mirrors the Qt classes *and their conventions*, because
// the rest of the renderer is written against them and, more importantly,
// because the pipeline depends on those conventions:
//
//   * Mat4 stores column-major data (index = column * 4 + row), so constData()
//     can be handed straight to glUniformMatrix4fv(..., GL_FALSE, ...) and
//     glLoadMatrixf();
//   * operator()(row, col) and the transform helpers follow Qt's semantics
//     (translate/rotate/scale post-multiply, lookAt/perspective/ortho replace);
//   * normalMatrix() is the inverse transpose of the upper-left 3x3.
//
// The port was validated operation by operation against Qt itself; see
// tools/mathParity in the history of this change.

#pragma once

#include <cmath>
#include <cstring>

namespace modern
{

// ---------------------------------------------------------------------------
// Vec2
// ---------------------------------------------------------------------------
struct Vec2
{
    Vec2() = default;
    Vec2(float xValue, float yValue) : m_x(xValue), m_y(yValue) {}

    float x() const { return m_x; }
    float y() const { return m_y; }

    float m_x = 0.0f;
    float m_y = 0.0f;
};

// ---------------------------------------------------------------------------
// Vec3
// ---------------------------------------------------------------------------
struct Vec3
{
    Vec3() = default;
    Vec3(float xValue, float yValue, float zValue) : m_x(xValue), m_y(yValue), m_z(zValue) {}

    float x() const { return m_x; }
    float y() const { return m_y; }
    float z() const { return m_z; }
    void setX(float value) { m_x = value; }
    void setY(float value) { m_y = value; }
    void setZ(float value) { m_z = value; }

    float lengthSquared() const { return m_x * m_x + m_y * m_y + m_z * m_z; }
    float length() const { return std::sqrt(lengthSquared()); }

    Vec3 normalized() const
    {
        const float len = length();
        if (len <= 0.0f)
            return Vec3();
        return Vec3(m_x / len, m_y / len, m_z / len);
    }

    static float dotProduct(const Vec3& a, const Vec3& b)
    {
        return a.m_x * b.m_x + a.m_y * b.m_y + a.m_z * b.m_z;
    }

    static Vec3 crossProduct(const Vec3& a, const Vec3& b)
    {
        return Vec3(a.m_y * b.m_z - a.m_z * b.m_y, a.m_z * b.m_x - a.m_x * b.m_z,
                    a.m_x * b.m_y - a.m_y * b.m_x);
    }

    Vec3 operator+(const Vec3& other) const
    {
        return Vec3(m_x + other.m_x, m_y + other.m_y, m_z + other.m_z);
    }
    Vec3 operator-(const Vec3& other) const
    {
        return Vec3(m_x - other.m_x, m_y - other.m_y, m_z - other.m_z);
    }
    Vec3 operator-() const { return Vec3(-m_x, -m_y, -m_z); }
    Vec3 operator*(float factor) const { return Vec3(m_x * factor, m_y * factor, m_z * factor); }
    Vec3 operator/(float divisor) const
    {
        return Vec3(m_x / divisor, m_y / divisor, m_z / divisor);
    }
    Vec3& operator+=(const Vec3& other)
    {
        m_x += other.m_x;
        m_y += other.m_y;
        m_z += other.m_z;
        return *this;
    }
    Vec3& operator-=(const Vec3& other)
    {
        m_x -= other.m_x;
        m_y -= other.m_y;
        m_z -= other.m_z;
        return *this;
    }
    bool operator==(const Vec3& other) const
    {
        return m_x == other.m_x && m_y == other.m_y && m_z == other.m_z;
    }

    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_z = 0.0f;
};

inline Vec3 operator*(float factor, const Vec3& value)
{
    return value * factor;
}

// ---------------------------------------------------------------------------
// Vec4
// ---------------------------------------------------------------------------
struct Vec4
{
    Vec4() = default;
    Vec4(float xValue, float yValue, float zValue, float wValue)
        : m_x(xValue), m_y(yValue), m_z(zValue), m_w(wValue)
    {
    }
    explicit Vec4(const Vec3& vector, float wValue = 1.0f)
        : m_x(vector.x()), m_y(vector.y()), m_z(vector.z()), m_w(wValue)
    {
    }
    explicit Vec4(const float values[4])
        : m_x(values[0]), m_y(values[1]), m_z(values[2]), m_w(values[3])
    {
    }

    float x() const { return m_x; }
    float y() const { return m_y; }
    float z() const { return m_z; }
    float w() const { return m_w; }
    void setW(float value) { m_w = value; }
    void setX(float value) { m_x = value; }
    void setY(float value) { m_y = value; }
    void setZ(float value) { m_z = value; }

    Vec3 toVector3D() const { return Vec3(m_x, m_y, m_z); }

    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_z = 0.0f;
    float m_w = 0.0f;
};

inline Vec4 operator+(const Vec4& a, const Vec4& b)
{
    return Vec4(a.x() + b.x(), a.y() + b.y(), a.z() + b.z(), a.w() + b.w());
}

inline Vec4 operator-(const Vec4& a, const Vec4& b)
{
    return Vec4(a.x() - b.x(), a.y() - b.y(), a.z() - b.z(), a.w() - b.w());
}

inline Vec4 operator*(const Vec4& value, float factor)
{
    return Vec4(value.x() * factor, value.y() * factor, value.z() * factor, value.w() * factor);
}

template <typename T>
constexpr T clampValue(T value, T low, T high)
{
    return value < low ? low : (value > high ? high : value);
}

static_assert(sizeof(Vec4) == 16, "Vec4 must stay tightly packed for std140");

// ---------------------------------------------------------------------------
// Mat3 (column-major, index = column * 3 + row)
// ---------------------------------------------------------------------------
struct Mat3
{
    Mat3() { setToIdentity(); }
    explicit Mat3(const float values[9]) { std::memcpy(m, values, sizeof(m)); }

    void setToIdentity()
    {
        std::memset(m, 0, sizeof(m));
        m[0] = m[4] = m[8] = 1.0f;
    }

    float& operator()(int row, int column) { return m[column * 3 + row]; }
    float operator()(int row, int column) const { return m[column * 3 + row]; }

    const float* constData() const { return m; }

    static Mat3 fromColumns(const Vec3& c0, const Vec3& c1, const Vec3& c2)
    {
        Mat3 result;
        result.m[0] = c0.x();
        result.m[1] = c0.y();
        result.m[2] = c0.z();
        result.m[3] = c1.x();
        result.m[4] = c1.y();
        result.m[5] = c1.z();
        result.m[6] = c2.x();
        result.m[7] = c2.y();
        result.m[8] = c2.z();
        return result;
    }

    float m[9];
};

static_assert(sizeof(Mat3) == 36, "Mat3 must stay tightly packed");

// ---------------------------------------------------------------------------
// Mat4 (column-major, index = column * 4 + row)
// ---------------------------------------------------------------------------
inline Vec3 operator*(const Mat3& matrix, const Vec3& vector)
{
    return Vec3(matrix(0, 0) * vector.x() + matrix(0, 1) * vector.y() + matrix(0, 2) * vector.z(),
                matrix(1, 0) * vector.x() + matrix(1, 1) * vector.y() + matrix(1, 2) * vector.z(),
                matrix(2, 0) * vector.x() + matrix(2, 1) * vector.y() + matrix(2, 2) * vector.z());
}

struct Mat4;
Vec4 operator*(const Mat4& matrix, const Vec4& vector);
Vec3 operator*(const Mat4& matrix, const Vec3& vector);

struct Mat4
{
    Mat4() { setToIdentity(); }
    explicit Mat4(const float values[16]) { std::memcpy(m, values, sizeof(m)); }

    void setToIdentity()
    {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    bool isIdentity() const
    {
        for (int i = 0; i < 16; ++i)
        {
            const float expected = (i % 5 == 0) ? 1.0f : 0.0f;
            if (m[i] != expected)
                return false;
        }
        return true;
    }

    float& operator()(int row, int column) { return m[column * 4 + row]; }
    float operator()(int row, int column) const { return m[column * 4 + row]; }

    const float* constData() const { return m; }

    Vec4 column(int index) const { return Vec4(m[index * 4 + 0], m[index * 4 + 1],
                                               m[index * 4 + 2], m[index * 4 + 3]); }

    // --- transforms (post-multiply, like Qt) -------------------------------
    void translate(float x, float y, float z);
    void translate(const Vec3& offset) { translate(offset.x(), offset.y(), offset.z()); }
    void scale(float x, float y, float z);
    void scale(const Vec3& factors) { scale(factors.x(), factors.y(), factors.z()); }
    void rotate(float angleDegrees, float x, float y, float z);
    void rotate(float angleDegrees, const Vec3& axis)
    {
        rotate(angleDegrees, axis.x(), axis.y(), axis.z());
    }

    // --- projections (replace, like Qt) ------------------------------------
    void perspective(float verticalFieldOfViewDegrees, float aspectRatio, float nearPlane,
                     float farPlane);
    void ortho(float left, float right, float bottom, float top, float nearPlane, float farPlane);
    void frustum(float left, float right, float bottom, float top, float nearPlane, float farPlane);
    void lookAt(const Vec3& eye, const Vec3& center, const Vec3& up);

    Vec3 map(const Vec3& point) const { return (*this) * point; }

    Mat4 inverted() const;
    Mat3 normalMatrix() const;

    Mat4& operator*=(const Mat4& other);

    float m[16];
};

static_assert(sizeof(Mat4) == 64, "Mat4 must stay tightly packed");

inline Mat4 operator*(const Mat4& a, const Mat4& b)
{
    Mat4 result;
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a(row, k) * b(k, column);
            result(row, column) = sum;
        }
    }
    return result;
}

inline Mat4& Mat4::operator*=(const Mat4& other)
{
    *this = *this * other;
    return *this;
}

inline Vec4 operator*(const Mat4& matrix, const Vec4& vector)
{
    return Vec4(matrix(0, 0) * vector.x() + matrix(0, 1) * vector.y() +
                    matrix(0, 2) * vector.z() + matrix(0, 3) * vector.w(),
                matrix(1, 0) * vector.x() + matrix(1, 1) * vector.y() +
                    matrix(1, 2) * vector.z() + matrix(1, 3) * vector.w(),
                matrix(2, 0) * vector.x() + matrix(2, 1) * vector.y() +
                    matrix(2, 2) * vector.z() + matrix(2, 3) * vector.w(),
                matrix(3, 0) * vector.x() + matrix(3, 1) * vector.y() +
                    matrix(3, 2) * vector.z() + matrix(3, 3) * vector.w());
}

// Matches QMatrix4x4 * QVector3D: treats the vector as a point and divides by w
// when the result is not affine.
inline Vec3 operator*(const Mat4& matrix, const Vec3& vector)
{
    const float x = matrix(0, 0) * vector.x() + matrix(0, 1) * vector.y() +
                    matrix(0, 2) * vector.z() + matrix(0, 3);
    const float y = matrix(1, 0) * vector.x() + matrix(1, 1) * vector.y() +
                    matrix(1, 2) * vector.z() + matrix(1, 3);
    const float z = matrix(2, 0) * vector.x() + matrix(2, 1) * vector.y() +
                    matrix(2, 2) * vector.z() + matrix(2, 3);
    const float w = matrix(3, 0) * vector.x() + matrix(3, 1) * vector.y() +
                    matrix(3, 2) * vector.z() + matrix(3, 3);
    if (w == 1.0f || w == 0.0f)
        return Vec3(x, y, z);
    return Vec3(x / w, y / w, z / w);
}

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------
inline void Mat4::translate(float x, float y, float z)
{
    Mat4 translation;
    translation(0, 3) = x;
    translation(1, 3) = y;
    translation(2, 3) = z;
    *this = *this * translation;
}

inline void Mat4::scale(float x, float y, float z)
{
    Mat4 scaling;
    scaling(0, 0) = x;
    scaling(1, 1) = y;
    scaling(2, 2) = z;
    *this = *this * scaling;
}

inline void Mat4::rotate(float angleDegrees, float x, float y, float z)
{
    float axisX = x;
    float axisY = y;
    float axisZ = z;
    const float axisLength = std::sqrt(axisX * axisX + axisY * axisY + axisZ * axisZ);
    if (axisLength <= 0.0f)
        return;
    axisX /= axisLength;
    axisY /= axisLength;
    axisZ /= axisLength;

    const float radians = angleDegrees * 3.14159265358979323846f / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float oneMinusCosine = 1.0f - cosine;

    Mat4 rotation;
    rotation(0, 0) = oneMinusCosine * axisX * axisX + cosine;
    rotation(0, 1) = oneMinusCosine * axisX * axisY - sine * axisZ;
    rotation(0, 2) = oneMinusCosine * axisX * axisZ + sine * axisY;
    rotation(1, 0) = oneMinusCosine * axisX * axisY + sine * axisZ;
    rotation(1, 1) = oneMinusCosine * axisY * axisY + cosine;
    rotation(1, 2) = oneMinusCosine * axisY * axisZ - sine * axisX;
    rotation(2, 0) = oneMinusCosine * axisX * axisZ - sine * axisY;
    rotation(2, 1) = oneMinusCosine * axisY * axisZ + sine * axisX;
    rotation(2, 2) = oneMinusCosine * axisZ * axisZ + cosine;

    *this = *this * rotation;
}

inline void Mat4::perspective(float verticalFieldOfViewDegrees, float aspectRatio, float nearPlane,
                              float farPlane)
{
    const float radians = verticalFieldOfViewDegrees * 3.14159265358979323846f / 180.0f;
    const float focal = 1.0f / std::tan(radians * 0.5f);

    setToIdentity();
    m[0] = focal / aspectRatio;
    m[5] = focal;
    m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    m[11] = -1.0f;
    m[14] = 2.0f * farPlane * nearPlane / (nearPlane - farPlane);
    m[15] = 0.0f;
}

inline void Mat4::ortho(float left, float right, float bottom, float top, float nearPlane,
                        float farPlane)
{
    setToIdentity();
    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = -2.0f / (farPlane - nearPlane);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    m[15] = 1.0f;
}

inline void Mat4::frustum(float left, float right, float bottom, float top, float nearPlane,
                          float farPlane)
{
    setToIdentity();
    m[0] = 2.0f * nearPlane / (right - left);
    m[5] = 2.0f * nearPlane / (top - bottom);
    m[8] = (right + left) / (right - left);
    m[9] = (top + bottom) / (top - bottom);
    m[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    m[11] = -1.0f;
    m[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);
    m[15] = 0.0f;
}

inline void Mat4::lookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
{
    const Vec3 forward = (center - eye).normalized();
    const Vec3 side = Vec3::crossProduct(forward, up).normalized();
    const Vec3 upVector = Vec3::crossProduct(side, forward);

    setToIdentity();
    m[0] = side.x();
    m[1] = upVector.x();
    m[2] = -forward.x();
    m[3] = 0.0f;

    m[4] = side.y();
    m[5] = upVector.y();
    m[6] = -forward.y();
    m[7] = 0.0f;

    m[8] = side.z();
    m[9] = upVector.z();
    m[10] = -forward.z();
    m[11] = 0.0f;

    m[12] = -Vec3::dotProduct(side, eye);
    m[13] = -Vec3::dotProduct(upVector, eye);
    m[14] = Vec3::dotProduct(forward, eye);
    m[15] = 1.0f;
}

inline Mat4 Mat4::inverted() const
{
    // Cofactor expansion; the array is column-major, which is the same layout
    // the classic row-major formulation works on once transposed, so the
    // indices below are used symmetrically.
    Mat4 result;
    result.m[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
                  m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    result.m[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
                  m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    result.m[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
                  m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    result.m[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
                   m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

    result.m[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
                  m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    result.m[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
                  m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    result.m[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
                  m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    result.m[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
                   m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

    result.m[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
                  m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    result.m[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
                  m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    result.m[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
                   m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    result.m[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
                   m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

    result.m[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
                  m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    result.m[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
                  m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    result.m[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
                   m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    result.m[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
                   m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float determinant = m[0] * result.m[0] + m[1] * result.m[4] + m[2] * result.m[8] +
                              m[3] * result.m[12];

    if (std::fabs(determinant) <= 1e-12f)
        return Mat4(); // singular: Qt returns the identity as well

    const float inverseDeterminant = 1.0f / determinant;
    for (int i = 0; i < 16; ++i)
        result.m[i] *= inverseDeterminant;
    return result;
}

inline Mat3 Mat4::normalMatrix() const
{
    // Inverse transpose of the upper-left 3x3.  CoppeliaSim bakes scaling into
    // the poses it sends, so approximating this with the rotation alone gives
    // visibly wrong normals on scaled shapes.
    //
    // (a,b,c) (d,e,f) (g,h,i) are the three *columns* of A, so the standard
    // row-major 3x3 inverse formula applied to them yields inv(A^T) = inv(A)^T,
    // which is exactly the normal matrix.
    const float a = m[0], b = m[1], c = m[2];
    const float d = m[4], e = m[5], f = m[6];
    const float g = m[8], h = m[9], i = m[10];

    const float n00 = e * i - f * h;
    const float n01 = c * h - b * i;
    const float n02 = b * f - c * e;
    const float n10 = f * g - d * i;
    const float n11 = a * i - c * g;
    const float n12 = c * d - a * f;
    const float n20 = d * h - e * g;
    const float n21 = b * g - a * h;
    const float n22 = a * e - b * d;

    const float determinant = a * n00 + b * n10 + c * n20;
    if (std::fabs(determinant) <= 1e-12f)
        return Mat3();

    const float inverseDeterminant = 1.0f / determinant;
    Mat3 result;
    result(0, 0) = n00 * inverseDeterminant;
    result(0, 1) = n01 * inverseDeterminant;
    result(0, 2) = n02 * inverseDeterminant;
    result(1, 0) = n10 * inverseDeterminant;
    result(1, 1) = n11 * inverseDeterminant;
    result(1, 2) = n12 * inverseDeterminant;
    result(2, 0) = n20 * inverseDeterminant;
    result(2, 1) = n21 * inverseDeterminant;
    result(2, 2) = n22 * inverseDeterminant;
    return result;
}

} // namespace modern
