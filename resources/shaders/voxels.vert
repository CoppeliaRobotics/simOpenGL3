#version 330 core

// Oc-tree voxels: one instanced cube per point.  The cube is a unit cube in the
// object's frame, scaled by the voxel size, so an instance carries the centre,
// the edge length, the colour and whether it is lit.

#include "common.glsl"

layout(location = 0) in vec3 aPosition; // unit cube, -0.5 .. 0.5
layout(location = 4) in vec3 aNormal;   // unit cube face normal

layout(location = 1) in vec3 aCenter;
layout(location = 2) in float aSize;
layout(location = 3) in vec4 aColor;
layout(location = 5) in float aLit;

uniform mat4 uModel;

out vec4 vColor;
out vec3 vNormal;
out vec3 vWorldPosition;
out float vLit;

void main()
{
    vColor = aColor;
    vLit = aLit;

    vec3 local = aCenter + aPosition * aSize;
    vec4 world = uModel * vec4(local, 1.0);

    vWorldPosition = world.xyz;
    // Objects are placed rigidly (scaling lives in voxelSize), so the model
    // matrix rotates normals without distorting them.
    vNormal = mat3(uModel) * aNormal;

    gl_Position = uViewProjection * world;
}
