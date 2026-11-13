#version 330 core

// Attribute-less full screen triangle.  No vertex buffer, no VAO attributes:
// the classic "gl_VertexID" trick, drawn with glDrawArrays(GL_TRIANGLES, 0, 3).

out vec2 vUv;

void main()
{
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
