#version 330 core

// Depth-only vertex shader, shared by the shadow pass (uLightSpaceMatrix is
// the light's view-projection) and the optional camera depth prepass
// (uLightSpaceMatrix is the camera's view-projection).
//
// Geometry is instanced exactly like in the forward pass, so the same VAO and
// the same per-instance stream are reused for every pass.

layout(location = 0) in vec3 aPosition;

layout(location = 3) in vec4 aModel0;
layout(location = 4) in vec4 aModel1;
layout(location = 5) in vec4 aModel2;
layout(location = 6) in vec4 aModel3;

uniform mat4 uLightSpaceMatrix;

void main()
{
    mat4 model = mat4(aModel0, aModel1, aModel2, aModel3);
    gl_Position = uLightSpaceMatrix * model * vec4(aPosition, 1.0);
}
