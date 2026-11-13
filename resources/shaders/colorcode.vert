#version 330 core

// Colour coded pass vertex shader.
//
// Used when CoppeliaSim asks for a colour coded frame (a vision sensor in
// "color coded" render mode): every instance carries its object handle as a
// colour and the fragment colour is written unchanged, so the read-back image
// can be decoded back into handles.

layout(location = 0) in vec3 aPosition;

layout(location = 3) in vec4 aModel0;
layout(location = 4) in vec4 aModel1;
layout(location = 5) in vec4 aModel2;
layout(location = 6) in vec4 aModel3;
layout(location = 12) in vec4 aColorCode;

uniform mat4 uViewProjection;

out vec4 vColor;

void main()
{
    mat4 model = mat4(aModel0, aModel1, aModel2, aModel3);
    vColor = aColorCode;
    gl_Position = uViewProjection * (model * vec4(aPosition, 1.0));
}
