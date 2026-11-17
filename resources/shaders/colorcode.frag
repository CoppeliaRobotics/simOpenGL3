#version 330 core

// Colour coded pass fragment shader: the handle bits arrive as the vertex
// colour and are written without lighting, tone mapping or any transfer
// function, so the values read back are exactly the handles.

in vec4 vColor;

out vec4 FragColor;

// x near, y far, z 1 for a perspective camera (see common.glsl)
uniform vec4 uClipPlanes;

void main()
{
    // Same linearisation as the shaded pass, so the depth image is identical
    // whichever mode the sensor uses.  gl_FragDepth is always assigned: a
    // conditional write leaves it undefined on the other path.
    gl_FragDepth = gl_FragCoord.z;
    if (uClipPlanes.z >= 0.5)
    {
        float nearPlane = uClipPlanes.x;
        float farPlane = uClipPlanes.y;
        float range = farPlane - nearPlane;
        float depth = gl_FragCoord.z;
        gl_FragDepth = ((nearPlane * farPlane / range) / (farPlane / range - depth) - nearPlane) /
                       range;
    }

    FragColor = vec4(vColor.rgb, 1.0);
}


