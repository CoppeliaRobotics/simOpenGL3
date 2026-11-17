#version 330 core

// Point clouds from the engine, drawn as GL_POINTS.  Points are unlit: the
// colour is whatever the object carries (or the handle in a colour coded frame).
//
// The camera block is used rather than a plain uniform because the fragment
// stage needs the clip planes to write the same linear depth the shaded pass
// writes - otherwise the depth test compares two different ranges and the points
// disappear behind everything.

#include "common.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uModel;
uniform float uPointSize;


out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_PointSize = uPointSize;
    gl_Position = uViewProjection * (uModel * vec4(aPosition, 1.0));
}

