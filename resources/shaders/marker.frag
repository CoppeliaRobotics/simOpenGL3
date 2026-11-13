#version 330 core

// Marker pass fragment shader.
//
// Markers are lit like the rest of the scene - diffuse only, with the scene
// ambient term - but without shadows and without specular, which is what
// CoppeliaSim's own marker rendering does (it sets specular, emission and
// shininess to zero and lets the scene lights shade the marker colour).

#include "common.glsl"

in vec4 vColor;
in vec3 vNormal;
in vec3 vWorldPosition;
in float vLit;

// 0 in a colour coded frame: the handle colour must reach the image untouched.
// The per-vertex flag carries sim_markeropts_notshaded.
uniform int uLit;

out vec4 FragColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lighting = uSceneAmbient.rgb;

    int lightCount = int(uLightHeader.x);
    for (int i = 0; i < lightCount; ++i)
    {
        GpuLight light = uLights[i];
        int type = int(light.positionKind.w);

        vec3 lightDirection;
        float attenuation = 1.0;

        if (type == LIGHT_DIR)
        {
            lightDirection = normalize(-light.directionRange.xyz);
        }
        else
        {
            vec3 toLight = light.positionKind.xyz - vWorldPosition;
            float distance = length(toLight);
            lightDirection = toLight / max(distance, 1e-5);

            float denominator = light.attenuation.x +
                                light.attenuation.y * distance +
                                light.attenuation.z * distance * distance;
            attenuation = 1.0 / max(denominator, 1e-5);

            if (type == LIGHT_SPOT)
            {
                float theta = dot(lightDirection, normalize(-light.directionRange.xyz));
                float epsilon = max(light.colorIntensity.w - light.attenuation.w, 1e-4);
                attenuation *= clamp((theta - light.attenuation.w) / epsilon, 0.0, 1.0);
            }
        }

        float nDotL = max(dot(normal, lightDirection), 0.0);
        lighting += light.colorIntensity.rgb * nDotL * attenuation;
    }

    // Markers are opaque: any alpha in the source colour is ignored.
    float lit = vLit * float(uLit);
    FragColor = vec4(vColor.rgb * mix(vec3(1.0), lighting, lit), 1.0);
}






