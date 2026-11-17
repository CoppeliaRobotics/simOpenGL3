#version 330 core

#include "common.glsl"

in vec4 vColor;

out vec4 FragColor;

void main()
{
    // Same depth encoding as the shaded pass and the markers.
    gl_FragDepth = lineariseWindowDepth(gl_FragCoord.z);
    FragColor = vColor;
}
