#version 330 core

in vec3 vWorldPosition;

uniform vec3 uLightPosition;
uniform float uNearPlane;
uniform float uFarPlane;

void main()
{
    float distanceToLight = length(vWorldPosition - uLightPosition);
    float normalised = (distanceToLight - uNearPlane) / max(uFarPlane - uNearPlane, 1e-5);
    gl_FragDepth = clamp(normalised, 0.0, 1.0);
}
