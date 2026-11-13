#version 330 core

// Colour coded pass fragment shader: the handle bits arrive as the vertex
// colour and are written without lighting, tone mapping or any transfer
// function, so the values read back are exactly the handles.

in vec4 vColor;

out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor.rgb, 1.0);
}
