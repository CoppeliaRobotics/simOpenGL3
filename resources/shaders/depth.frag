#version 330 core

// Depth prepass and shadow layers both use this program.
//
// gl_FragDepth is assigned unconditionally: a shader that only writes it on some
// paths leaves it undefined on the others, which silently produced garbage shadow
// maps.  The clip planes select the conversion - they are cleared to zero for the
// shadow passes, where plain window space depth is wanted.

// x near, y far, z 1 for a perspective camera, 0 for plain depth
uniform vec4 uClipPlanes;

void main()
{
    if (uClipPlanes.z < 0.5)
    {
        gl_FragDepth = gl_FragCoord.z;
        return;
    }

    float nearPlane = uClipPlanes.x;
    float farPlane = uClipPlanes.y;
    float range = farPlane - nearPlane;
    float depth = gl_FragCoord.z;
    gl_FragDepth = ((nearPlane * farPlane / range) / (farPlane / range - depth) - nearPlane) / range;
}
