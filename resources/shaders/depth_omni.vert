#version 330 core

// Depth-only vertex shader for omnidirectional (point) lights.
//
// The world position is needed because the fragment shader stores a linear
// distance-to-light instead of the projected depth: that makes the six cube
// faces directly comparable and lets the forward pass compute the reference
// depth from the light distance alone, without per-face matrices.

layout(location = 0) in vec3 aPosition;

layout(location = 3) in vec4 aModel0;
layout(location = 4) in vec4 aModel1;
layout(location = 5) in vec4 aModel2;
layout(location = 6) in vec4 aModel3;

uniform mat4 uLightSpaceMatrix;

out vec3 vWorldPosition;

void main()
{
    mat4 model = mat4(aModel0, aModel1, aModel2, aModel3);
    vec4 worldPosition = model * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    gl_Position = uLightSpaceMatrix * worldPosition;
}
