#version 330 core

// Marker pass vertex shader.
//
// Markers are annotations: their geometry is tessellated on the CPU into
// points, lines and triangles, so all this does is project it and hand the
// lighting inputs to the fragment stage.

#include "common.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in float aLit;

out vec4 vColor;
out vec3 vNormal;
out vec3 vWorldPosition;
out float vLit;

void main()
{
    vColor = aColor;
    vNormal = aNormal;
    vWorldPosition = aPosition;
    vLit = aLit;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}

