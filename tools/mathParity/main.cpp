// Throw-away validation tool: checks math3d against Qt's vector/matrix classes
// operation by operation, so that the Qt removal cannot silently change the
// pipeline's matrices.
//
// Build and run once; every operation must report "ok".

#include <QGuiApplication>
#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <QDebug>
#include <QVector2D>

#include <cmath>
#include <cstdio>
#include <random>

#include "math3d.h"

using namespace modern;

namespace
{

int failures = 0;

void compare(const char* what, float mine, float theirs)
{
    const float scale = std::max(1.0f, std::max(std::fabs(mine), std::fabs(theirs)));
    if (std::fabs(mine - theirs) > 1e-4f * scale)
    {
        std::printf("FAIL %-34s mine=%12.6f theirs=%12.6f\n", what, mine, theirs);
        ++failures;
    }
}

void compareMatrix(const char* what, const Mat4& mine, const QMatrix4x4& theirs)
{
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            compare(what, mine(row, column), theirs(row, column));
    for (int i = 0; i < 16; ++i)
    {
        const float scale = std::max(1.0f, std::fabs(theirs.constData()[i]));
        if (std::fabs(mine.constData()[i] - theirs.constData()[i]) > 1e-4f * scale)
        {
            std::printf("FAIL %-30s constData[%2d]\n", what, i);
            ++failures;
        }
    }
}

void compareMatrix3(const char* what, const Mat3& mine, const QMatrix3x3& theirs)
{
    for (int i = 0; i < 9; ++i)
    {
        const float scale = std::max(1.0f, std::fabs(theirs.constData()[i]));
        if (std::fabs(mine.constData()[i] - theirs.constData()[i]) > 1e-4f * scale)
        {
            std::printf("FAIL %-30s constData[%d] mine=%f theirs=%f\n", what, i,
                        mine.constData()[i], theirs.constData()[i]);
            ++failures;
        }
    }
}

QMatrix4x4 toQt(const Mat4& m)
{
    QMatrix4x4 result;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            result(row, column) = m(row, column);
    return result;
}

Mat4 fromQt(const QMatrix4x4& m)
{
    Mat4 result;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            result(row, column) = m(row, column);
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);

    std::mt19937 generator(12345);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> positive(0.1f, 4.0f);
    std::uniform_real_distribution<float> angle(-180.0f, 180.0f);

    // --- transforms --------------------------------------------------------
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        const Vec3 eye(unit(generator) * 6.0f, unit(generator) * 6.0f, unit(generator) * 6.0f);
        const Vec3 center(unit(generator), unit(generator), unit(generator));
        Vec3 up(0.0f, 1.0f, 0.0f);
        if (std::fabs(Vec3::dotProduct((center - eye).normalized(), up)) > 0.9f)
            up = Vec3(1.0f, 0.0f, 0.0f);

        const QVector3D qEye(eye.x(), eye.y(), eye.z());
        const QVector3D qCenter(center.x(), center.y(), center.z());
        const QVector3D qUp(up.x(), up.y(), up.z());

        {
            Mat4 mine;
            mine.lookAt(eye, center, up);
            QMatrix4x4 theirs;
            theirs.lookAt(qEye, qCenter, qUp);
            compareMatrix("lookAt", mine, theirs);
        }
        {
            Mat4 mine;
            const float deg = angle(generator);
            const Vec3 axis = Vec3(unit(generator), unit(generator), unit(generator)).normalized();
            mine.rotate(deg, axis);
            QMatrix4x4 theirs;
            theirs.rotate(deg, QVector3D(axis.x(), axis.y(), axis.z()));
            compareMatrix("rotate", mine, theirs);
        }
        {
            const float scaleX = positive(generator);
            const float scaleY = positive(generator);
            const float scaleZ = positive(generator);
            Mat4 mine;
            mine.translate(eye.x(), eye.y(), eye.z());
            mine.scale(scaleX, scaleY, scaleZ);
            QMatrix4x4 theirs;
            theirs.translate(qEye);
            theirs.scale(scaleX, scaleY, scaleZ);
            compareMatrix("translate+scale", mine, theirs);
        }
    }

    {
        Mat4 mine;
        mine.perspective(45.0f, 1.6f, 0.1f, 50.0f);
        QMatrix4x4 theirs;
        theirs.perspective(45.0f, 1.6f, 0.1f, 50.0f);
        compareMatrix("perspective", mine, theirs);
    }
    {
        Mat4 mine;
        mine.ortho(-2.0f, 3.0f, -1.5f, 2.5f, 0.1f, 30.0f);
        QMatrix4x4 theirs;
        theirs.ortho(-2.0f, 3.0f, -1.5f, 2.5f, 0.1f, 30.0f);
        compareMatrix("ortho", mine, theirs);
    }

    // --- products, inverse, normal matrix ----------------------------------
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        QMatrix4x4 a;
        a.translate(unit(generator) * 4.0f, unit(generator) * 4.0f, unit(generator) * 4.0f);
        a.rotate(angle(generator), QVector3D(unit(generator), unit(generator), unit(generator)));
        a.scale(positive(generator), positive(generator), positive(generator));

        QMatrix4x4 b;
        b.perspective(40.0f + positive(generator) * 10.0f, 0.5f + positive(generator), 0.05f, 80.0f);

        const Mat4 mineA = fromQt(a);
        const Mat4 mineB = fromQt(b);

        compareMatrix("a*b", mineA * mineB, a * b);

        {
            const Mat4 inverse = mineA.inverted();
            compareMatrix("inverted(rigid+scale)", inverse, a.inverted());
        }
        {
            const Mat4 inverse = mineB.inverted();
            compareMatrix("inverted(perspective)", inverse, b.inverted());
        }
        compareMatrix3("normalMatrix", mineA.normalMatrix(), a.normalMatrix());

        {
            const Vec3 point(unit(generator) * 3.0f, unit(generator) * 3.0f, unit(generator) * 3.0f);
            const Vec3 mine = mineA * point;
            const QVector3D theirs = a * QVector3D(point.x(), point.y(), point.z());
            compare("map(Vec3)", mine.x(), theirs.x());
            compare("map(Vec3)", mine.y(), theirs.y());
            compare("map(Vec3)", mine.z(), theirs.z());
        }
        {
            const Vec4 vector(unit(generator), unit(generator), unit(generator), 1.0f);
            const Vec4 mine = mineB * vector;
            const QVector4D theirs =
                b * QVector4D(vector.x(), vector.y(), vector.z(), vector.w());
            compare("mat*vec4", mine.x(), theirs.x());
            compare("mat*vec4", mine.w(), theirs.w());
        }
        {
            const Vec4 mine = mineA.column(3);
            const QVector4D theirs = a.column(3);
            compare("column(3)", mine.x(), theirs.x());
            compare("column(3)", mine.w(), theirs.w());
        }
    }

    // --- pose conversion used by the plugin --------------------------------
    {
        const float quaternion[4] = {0.9238795f, 0.0f, 0.3826834f, 0.0f};
        const float position[3] = {1.5f, -2.0f, 0.25f};

        Mat4 mine;
        mine = Mat4(); // poseToMatrix equivalent
        const float w = quaternion[0], x = quaternion[1], y = quaternion[2], z = quaternion[3];
        mine(0, 0) = 1.0f - 2.0f * (y * y + z * z);
        mine(0, 1) = 2.0f * (x * y - z * w);
        mine(0, 2) = 2.0f * (x * z + y * w);
        mine(1, 0) = 2.0f * (x * y + z * w);
        mine(1, 1) = 1.0f - 2.0f * (x * x + z * z);
        mine(1, 2) = 2.0f * (y * z - x * w);
        mine(2, 0) = 2.0f * (x * z - y * w);
        mine(2, 1) = 2.0f * (y * z + x * w);
        mine(2, 2) = 1.0f - 2.0f * (x * x + y * y);
        mine(0, 3) = position[0];
        mine(1, 3) = position[1];
        mine(2, 3) = position[2];

        // [R | P] with the translation in world space, which is what a pose is.
        QMatrix4x4 theirs;
        theirs.rotate(QQuaternion(w, x, y, z));
        theirs(0, 3) = position[0];
        theirs(1, 3) = position[1];
        theirs(2, 3) = position[2];

        compareMatrix("poseToMatrix", mine, theirs);
    }

    // --- TEMPORARY PROBE ---
    {
        Mat4 mine;
        mine.rotate(90.0f, 0.0f, 0.0f, 1.0f);
        QMatrix4x4 theirs;
        theirs.rotate(90.0f, 0.0f, 0.0f, 1.0f);
        std::printf("mine   : ");
        for (int i = 0; i < 16; ++i) std::printf("%7.3f ", mine.constData()[i]);
        std::printf("\nqt     : ");
        for (int i = 0; i < 16; ++i) std::printf("%7.3f ", theirs.constData()[i]);
        std::printf("\n");
        std::printf("mine(0,1)=%f qt(0,1)=%f   mine(1,0)=%f qt(1,0)=%f\n",
                    mine(0,1), theirs(0,1), mine(1,0), theirs(1,0));
    }
    // --- END PROBE ---

    if (failures == 0)
        std::printf("MATH PARITY OK\n");
    else
        std::printf("MATH PARITY FAILED: %d mismatches\n", failures);
    return failures == 0 ? 0 : 1;
}



