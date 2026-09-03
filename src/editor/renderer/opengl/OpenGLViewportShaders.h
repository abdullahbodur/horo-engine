#pragma once

namespace Horo::Editor::Detail {
    inline constexpr char ViewportVertexShader[] = R"glsl(#version 150 core
in vec3 aPosition;
in vec3 aNormal;
in vec2 aUv;
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec4 vShadowPosition;
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uShadowViewProjection;
void main()
{
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    mat3 modelBasis = mat3(uModel);
    vec3 inverseRow0 = cross(modelBasis[1], modelBasis[2]);
    vec3 inverseRow1 = cross(modelBasis[2], modelBasis[0]);
    vec3 inverseRow2 = cross(modelBasis[0], modelBasis[1]);
    float determinant = dot(modelBasis[0], inverseRow0);
    float inverseDeterminant = abs(determinant) > 0.0000001 ? 1.0 / determinant : 1.0;
    mat3 normalMatrix = mat3(inverseRow0 * inverseDeterminant,
                             inverseRow1 * inverseDeterminant,
                             inverseRow2 * inverseDeterminant);
    vWorldNormal = normalize(normalMatrix * aNormal);
    vShadowPosition = uShadowViewProjection * worldPosition;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)glsl";

    inline constexpr char ViewportFragmentShader[] = R"glsl(#version 150 core
const int MaxLights = 16;
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec4 vShadowPosition;
out vec4 outColor;
uniform vec3 uCameraPosition;
uniform int uLightCount;
uniform vec4 uLightPositionKind[MaxLights];
uniform vec4 uLightDirectionRange[MaxLights];
uniform vec4 uLightColorIntensity[MaxLights];
uniform vec2 uLightCone[MaxLights];
uniform sampler2D uShadowMap;
uniform int uShadowEnabled;
uniform int uShadowLightIndex;
uniform vec3 uSelectionColor;
uniform float uSelectionStrength;
float directionalShadow(vec3 normal, vec3 lightDirection)
{
    if (uShadowEnabled == 0)
        return 1.0;
    vec3 projected = vShadowPosition.xyz / vShadowPosition.w;
    vec3 shadowCoordinate = projected * 0.5 + 0.5;
    if (shadowCoordinate.x <= 0.0 || shadowCoordinate.x >= 1.0 ||
        shadowCoordinate.y <= 0.0 || shadowCoordinate.y >= 1.0 ||
        shadowCoordinate.z <= 0.0 || shadowCoordinate.z >= 1.0)
        return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float bias = max(0.0025 * (1.0 - dot(normal, lightDirection)), 0.00045);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            float closestDepth = texture(uShadowMap, shadowCoordinate.xy + vec2(x, y) * texel).r;
            visibility += shadowCoordinate.z - bias <= closestDepth ? 1.0 : 0.0;
        }
    return visibility / 9.0;
}
void main()
{
    vec3 normal = normalize(vWorldNormal);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 baseColor = vec3(0.68, 0.70, 0.74);
    vec3 lighting = baseColor * 0.08;
    for (int index = 0; index < uLightCount; ++index)
    {
        int kind = int(uLightPositionKind[index].w + 0.5);
        vec3 lightDirection;
        float attenuation = 1.0;
        if (kind == 0)
        {
            lightDirection = normalize(-uLightDirectionRange[index].xyz);
        }
        else
        {
            vec3 toLight = uLightPositionKind[index].xyz - vWorldPosition;
            float distanceToLight = length(toLight);
            lightDirection = distanceToLight > 0.0001 ? toLight / distanceToLight : normal;
            float range = max(uLightDirectionRange[index].w, 0.0001);
            float normalizedDistance = distanceToLight / range;
            float rangeFade = max(1.0 - normalizedDistance * normalizedDistance, 0.0);
            attenuation = rangeFade * rangeFade;
            if (kind == 2)
            {
                float coneCosine = dot(normalize(uLightDirectionRange[index].xyz), -lightDirection);
                attenuation *= smoothstep(uLightCone[index].y, uLightCone[index].x, coneCosine);
            }
        }
        float diffuse = max(dot(normal, lightDirection), 0.0);
        vec3 halfDirection = normalize(lightDirection + viewDirection);
        float specular = pow(max(dot(normal, halfDirection), 0.0), 48.0) * 0.18;
        vec3 radiance = uLightColorIntensity[index].rgb * uLightColorIntensity[index].a * attenuation;
        float visibility = index == uShadowLightIndex ? directionalShadow(normal, lightDirection) : 1.0;
        lighting += radiance * (baseColor * diffuse + specular) * visibility;
    }
    vec3 mapped = lighting / (lighting + vec3(1.0));
    vec3 displayColor = pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(mix(displayColor, uSelectionColor, uSelectionStrength), 1.0);
}
)glsl";

    inline constexpr char ShadowVertexShader[] = R"glsl(#version 150 core
in vec3 aPosition;
uniform mat4 uShadowMvp;
void main()
{
    gl_Position = uShadowMvp * vec4(aPosition, 1.0);
}
)glsl";

    inline constexpr char ShadowFragmentShader[] = R"glsl(#version 150 core
void main() {}
)glsl";
}  // namespace Horo::Editor::Detail
