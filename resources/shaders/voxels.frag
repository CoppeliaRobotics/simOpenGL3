#version 330 core

// Oc-tree voxels are lit like the rest of the scene - diffuse only, with the
// scene ambient term - and write the same linear depth as the shaded pass.

#include "common.glsl"

in vec4 vColor;
in vec3 vNormal;
in vec3 vWorldPosition;

// 0 in a colour coded frame: the handle colour must reach the image untouched.
in float vLit;

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

        lighting += light.colorIntensity.rgb * max(dot(normal, lightDirection), 0.0) * attenuation;
    }

    gl_FragDepth = lineariseWindowDepth(gl_FragCoord.z);
    FragColor = vec4(vColor.rgb * mix(vec3(1.0), lighting, vLit), vColor.a);
}


