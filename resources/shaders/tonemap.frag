#version 330 core

// Final pass: exposure, tone mapping and transfer function, writing the 8 bit
// target that is read back for CoppeliaSim.
//
// The scene is lit in linear space and stored in an RGBA16F target, so this is
// where the HDR image becomes a displayable sRGB image.

in vec2 vUv;

uniform sampler2D uHdrColor;
uniform float uExposure;
uniform int uToneMapping; // 0 none, 1 Reinhard, 2 ACES (Narkowicz fit)
uniform int uGamma;       // 0 = encode to sRGB, 1 = pass through

out vec4 FragColor;

vec3 applyToneMapping(vec3 color)
{
    if (uToneMapping == 1)
        return color / (color + vec3(1.0));
    if (uToneMapping == 2)
    {
        const float a = 2.51;
        const float b = 0.03;
        const float c = 2.43;
        const float d = 0.59;
        const float e = 0.14;
        return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
    }
    return color;
}

vec3 encodeSrgb(vec3 color)
{
    color = clamp(color, 0.0, 1.0);
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

void main()
{
    vec3 color = texture(uHdrColor, vUv).rgb * uExposure;
    color = applyToneMapping(color);
    color = clamp(color, 0.0, 1.0);
    if (uGamma == 0)
        color = encodeSrgb(color);
    FragColor = vec4(color, 1.0);
}
