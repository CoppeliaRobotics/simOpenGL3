#version 330 core

// Forward pass fragment shader: Blinn-Phong with per-light shadow maps
// sampled from a single depth texture array.
//
// The material model is deliberately the same one the legacy OpenGL3 renderer
// used (CoppeliaSim's shapes only carry diffuse/specular), so scenes keep
// their look; what changes is that everything is evaluated in linear space,
// shadows come from one sampler, and the number of lights is no longer capped
// at five.

#include "common.glsl"

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;

flat in vec3 vDiffuse;
flat in vec3 vSpecular;
flat in float vShininess;
flat in float vOpacity;

uniform sampler2D uAlbedo;
uniform sampler2DArrayShadow uShadowMap;
uniform int uApplyMode;     // 0 modulate, 1 decal, 2 add
uniform int uPcfRadius;     // texels between PCF taps
uniform int uShadowEnabled;

out vec4 FragColor;

// ---------------------------------------------------------------------------
// Shadows
//
// Both functions return a *visibility* factor: 1.0 means "fully lit", 0.0
// means "fully shadowed".  Getting this convention wrong flips the lighting
// inside the shadow frustum, which is easy to miss because the out-of-frustum
// path still looks right.
// ---------------------------------------------------------------------------

float sampleShadowDirectionalOrSpot(GpuLight light, vec3 normal, vec3 lightDirection)
{
    vec4 lightClip = light.lightSpace * vec4(vWorldPosition, 1.0);
    vec3 projected = lightClip.xyz / lightClip.w * 0.5 + 0.5;

    if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0)
        return 1.0; // outside of the shadow map: the light is not occluded

    float bias = max(light.shadow.z * (1.0 - dot(normal, lightDirection)), light.shadow.y);
    float reference = projected.z - bias;
    float layer = light.shadow.x;

    vec2 texel = float(max(uPcfRadius, 1)) / vec2(textureSize(uShadowMap, 0).xy);
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
            sum += texture(uShadowMap, vec4(projected.xy + vec2(x, y) * texel, layer, reference));
    }
    return sum / 9.0;
}

float sampleShadowOmnidirectional(GpuLight light, vec3 normal, vec3 lightDirection)
{
    vec3 toFragment = vWorldPosition - light.positionKind.xyz;
    float distanceToLight = length(toFragment);
    vec3 absolute = abs(toFragment);

    // Standard cube map face selection; the depth pass renders each face with
    // the matching lookAt basis, so the two stay consistent.
    int face;
    vec2 st;
    if (absolute.x >= absolute.y && absolute.x >= absolute.z)
    {
        if (toFragment.x > 0.0) { face = 0; st = vec2(-toFragment.z, -toFragment.y) / absolute.x; }
        else                    { face = 1; st = vec2( toFragment.z, -toFragment.y) / absolute.x; }
    }
    else if (absolute.y >= absolute.z)
    {
        if (toFragment.y > 0.0) { face = 2; st = vec2( toFragment.x,  toFragment.z) / absolute.y; }
        else                    { face = 3; st = vec2( toFragment.x, -toFragment.z) / absolute.y; }
    }
    else
    {
        if (toFragment.z > 0.0) { face = 4; st = vec2( toFragment.x, -toFragment.y) / absolute.z; }
        else                    { face = 5; st = vec2(-toFragment.x, -toFragment.y) / absolute.z; }
    }

    vec2 uv = st * 0.5 + 0.5;
    float nearPlane = light.shadow.w;
    float farPlane = light.directionRange.w;
    float bias = max(light.shadow.z * (1.0 - dot(normal, lightDirection)), light.shadow.y);
    float reference = (distanceToLight - nearPlane) / max(farPlane - nearPlane, 1e-5) - bias;
    float layer = light.shadow.x + float(face);

    if (reference > 1.0)
        return 1.0; // beyond the light's range: not occluded

    vec2 texel = float(max(uPcfRadius, 1)) / vec2(textureSize(uShadowMap, 0).xy);
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
            sum += texture(uShadowMap, vec4(uv + vec2(x, y) * texel, layer, reference));
    }
    return sum / 9.0;
}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------

vec3 evaluateLighting(vec3 albedo, vec3 normal, vec3 viewDirection)
{
    vec3 result = vec3(0.0);
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

            float denominator = light.attenuation.x + light.attenuation.y * distance +
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
        if (nDotL <= 0.0 || attenuation <= 0.0)
            continue;

        vec3 halfway = normalize(lightDirection + viewDirection);
        float specular = pow(max(dot(normal, halfway), 0.0), max(vShininess, 1.0));

        float visibility = 1.0;
        if (uShadowEnabled != 0)
        {
            visibility = (type == LIGHT_OMNI)
                             ? sampleShadowOmnidirectional(light, normal, lightDirection)
                             : sampleShadowDirectionalOrSpot(light, normal, lightDirection);
        }

        // Fixed function Blinn-Phong: the diffuse term is material diffuse
        // times light diffuse, the highlight is material specular times *light
        // specular* (not the light diffuse, which is what it used to be).
        vec3 diffuseTerm = light.colorIntensity.rgb * vDiffuse * albedo * nDotL;
        vec3 specularTerm = light.specularColor.rgb * vSpecular * specular;
        result += (diffuseTerm + specularTerm) * attenuation * visibility;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Fog
// ---------------------------------------------------------------------------

float fogFactor()
{
    if (uFogParams.w < 0.5)
        return 0.0;

    float distance = length(vWorldPosition - uViewPosition.xyz);
    int type = int(uFogParams.x);
    if (type == 0) // linear
        return clamp((distance - uFogParams.y) / max(uFogParams.z - uFogParams.y, 1e-5), 0.0, 1.0);
    if (type == 1) // exponential
        return clamp(1.0 - exp(-uFogParams.z * distance), 0.0, 1.0);
    return clamp(1.0 - exp(-uFogParams.z * uFogParams.z * distance * distance), 0.0, 1.0);
}

// ---------------------------------------------------------------------------

void main()
{
    vec4 texel = texture(uAlbedo, vTexCoord);

    // Texture apply modes follow CoppeliaSim's semantics: MODULATE tints the
    // surface, DECAL and ADD are composited after lighting.
    vec3 albedo = (uApplyMode == 0) ? texel.rgb : vec3(1.0);

    vec3 normal = normalize(vNormal);
    vec3 viewDirection = normalize(uViewPosition.xyz - vWorldPosition);
    if (dot(normal, viewDirection) < 0.0)
        normal = -normal; // no back-face culling on this shape

    vec3 ambient = uSceneAmbient.rgb * vDiffuse * albedo;
    vec3 color = ambient + evaluateLighting(albedo, normal, viewDirection);

    if (uApplyMode == 1)
        color = mix(color, texel.rgb, texel.a);
    else if (uApplyMode == 2)
        color += texel.rgb * texel.a;

    color = mix(color, uFogColor.rgb, fogFactor());

    gl_FragDepth = lineariseWindowDepth(gl_FragCoord.z);
    FragColor = vec4(color, vOpacity);
}




