#version 330 core

// Forward pass vertex shader.
//
// Every mesh is drawn instanced: the static attributes (position, normal, uv)
// come from the per-geometry vertex buffer, everything that differs between
// two copies of the same geometry comes from the per-instance stream.
//
// The normal matrix is computed on the CPU and shipped per instance, because
// CoppeliaSim bakes scaling into the object pose and "mat3(transpose(inverse(model)))"
// per vertex (what the legacy renderer did) is both wrong for skewed poses and
// expensive.

#include "common.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 3) in vec4 aModel0;
layout(location = 4) in vec4 aModel1;
layout(location = 5) in vec4 aModel2;
layout(location = 6) in vec4 aModel3;

layout(location = 7) in vec4 aNormal0;
layout(location = 8) in vec4 aNormal1;
layout(location = 9) in vec4 aNormal2;

layout(location = 10) in vec4 aDiffuseOpacity;
layout(location = 11) in vec4 aSpecularShininess;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;

flat out vec3 vDiffuse;
flat out vec3 vSpecular;
flat out float vShininess;
flat out float vOpacity;

void main()
{
    mat4 model = mat4(aModel0, aModel1, aModel2, aModel3);
    mat3 normalMatrix = mat3(aNormal0.xyz, aNormal1.xyz, aNormal2.xyz);

    vec4 worldPosition = model * vec4(aPosition, 1.0);

    vWorldPosition = worldPosition.xyz;
    vNormal = normalMatrix * aNormal;
    vTexCoord = aTexCoord;

    vDiffuse = aDiffuseOpacity.rgb;
    vOpacity = aDiffuseOpacity.a;
    vSpecular = aSpecularShininess.rgb;
    vShininess = aSpecularShininess.w;

    gl_Position = uViewProjection * worldPosition;
}
