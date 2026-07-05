#version 410 core

in vec3 v_worldPos;
in vec3 v_worldNormal;
in vec2 v_uv;

uniform vec4      u_color      = vec4(1.0);
uniform float     u_roughness  = 0.5;
uniform float     u_metallic   = 0.0;
uniform vec3      u_cameraPos;
uniform mat4      u_view;

// glTF-aligned PBR texture slots (HORO-67); see basic.frag for slot order.
uniform sampler2D u_albedoMap;
uniform sampler2D u_normalMap;
uniform sampler2D u_metallicRoughnessMap;
uniform sampler2D u_emissiveMap;
uniform sampler2D u_occlusionMap;
uniform int       u_hasTexture              = 0;
uniform int       u_hasNormalMap            = 0;
uniform int       u_hasMetallicRoughnessMap = 0;
uniform int       u_hasEmissiveMap          = 0;
uniform int       u_hasOcclusionMap         = 0;
uniform float     u_uvScale    = 1.0;

struct Light {
    int   type;        // 0 directional, 1 point, 2 spot, 3 rect, 4 sky
    vec3  position;
    vec3  direction;
    vec3  color;       // pre-multiplied by intensity
    float radius;
};
uniform Light u_lights[8];
uniform int   u_lightCount;
uniform sampler2DShadow u_shadowMap;
uniform mat4      u_shadowLightSpaceMatrices[4];
uniform float     u_shadowCascadeSplits[4];
uniform float     u_shadowTexelWorldSizes[4];
uniform int       u_shadowCascadeCount = 0;
uniform int       u_shadowUsesAtlas = 0;
uniform int       u_shadowEnabled = 0;
uniform int       u_shadowLightIndex = -1;
uniform samplerCube u_pointShadowMap;
uniform int         u_pointShadowEnabled = 0;
uniform int         u_pointShadowLightIndex = -1;
uniform float       u_pointShadowFarPlane = 1.0;

out vec4 FragColor;

vec3 CalcLight(Light L, vec3 pos, vec3 N, vec3 V, vec3 albedo, float rough, float metal)
{
    if (L.type == 4) {
        float sky = 0.35 + 0.65 * max(N.y, 0.0);
        return L.color * albedo * sky;
    }

    vec3  L_dir;
    float attenuation = 1.0;
    if (L.type == 0) {
        L_dir = normalize(-L.direction);
    } else {
        vec3  toLight = L.position - pos;
        float d       = length(toLight);
        if (d > L.radius) return vec3(0.0);
        L_dir = toLight / d;
        float dOverR = d / max(L.radius, 0.001);
        float window = max(0.0, 1.0 - dOverR * dOverR * dOverR * dOverR);
        attenuation = (window * window) / (1.0 + d * d * 0.08);
        if (L.type == 2) {
            float cone = dot(normalize(L.direction), normalize(pos - L.position));
            float spot = smoothstep(cos(radians(24.0)), cos(radians(14.0)), cone);
            attenuation *= spot;
        } else if (L.type == 3) {
            float facing = max(dot(normalize(L.direction), normalize(pos - L.position)), 0.0);
            attenuation *= facing;
        }
    }
    float diff      = max(dot(N, L_dir), 0.0);
    vec3  H         = normalize(L_dir + V);
    float shininess = mix(8.0, 256.0, 1.0 - rough);
    float spec      = pow(max(dot(N, H), 0.0), shininess) * (1.0 - rough) * (1.0 - rough);
    vec3  specColor = mix(vec3(0.04), albedo, metal);

    vec3 diffuse = diff * albedo;
    return attenuation * L.color * (diffuse + spec * specColor);
}

vec2 ShadowAtlasOffset(int cascade)
{
    if (u_shadowUsesAtlas == 0)
        return vec2(0.0);
    return vec2(float(cascade % 2), float(cascade / 2)) * 0.5;
}

float SampleShadowCascade(int cascade, vec3 pos, vec3 N, vec3 lightDir)
{
    float normalOffset = u_shadowTexelWorldSizes[cascade] *
                         mix(0.75, 2.25, 1.0 - max(dot(N, lightDir), 0.0));
    vec4 lightSpace = u_shadowLightSpaceMatrices[cascade] *
                      vec4(pos + N * normalOffset, 1.0);
    vec3 projCoords = lightSpace.xyz / lightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0)
        return 1.0;

    float atlasScale = u_shadowUsesAtlas != 0 ? 0.5 : 1.0;
    vec2 localTexel = 1.0 / (vec2(textureSize(u_shadowMap, 0)) * atlasScale);
    vec2 margin = localTexel * 3.5;
    vec2 atlasOffset = ShadowAtlasOffset(cascade);
    float slopeBias = 1.0 - max(dot(N, lightDir), 0.0);
    float referenceDepth = projCoords.z - max(0.00012, 0.0009 * slopeBias);

    float visibility = 0.0;
    float weightSum = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float weight = float(3 - abs(x)) * float(3 - abs(y));
            vec2 localUv = clamp(
                projCoords.xy + vec2(float(x), float(y)) * localTexel,
                margin, vec2(1.0) - margin);
            vec2 atlasUv = atlasOffset + localUv * atlasScale;
            visibility += texture(u_shadowMap,
                                  vec3(atlasUv, referenceDepth)) * weight;
            weightSum += weight;
        }
    }
    return visibility / weightSum;
}

float ShadowVisibility(int lightIndex, Light L, vec3 pos, vec3 N)
{
    if (u_shadowEnabled == 0 || lightIndex != u_shadowLightIndex ||
        u_shadowCascadeCount <= 0)
        return 1.0;

    vec3 lightDir;
    if (L.type == 0)
        lightDir = normalize(-L.direction);
    else if (L.type == 2)
        lightDir = normalize(L.position - pos);
    else
        return 1.0;

    if (L.type == 2)
        return SampleShadowCascade(0, pos, N, lightDir);

    float viewDepth = -(u_view * vec4(pos, 1.0)).z;
    if (viewDepth > u_shadowCascadeSplits[u_shadowCascadeCount - 1])
        return 1.0;

    int cascade = 0;
    for (int i = 0; i < 3; ++i) {
        if (i + 1 < u_shadowCascadeCount &&
            viewDepth > u_shadowCascadeSplits[i])
            cascade = i + 1;
    }

    float visibility = SampleShadowCascade(cascade, pos, N, lightDir);
    if (cascade + 1 < u_shadowCascadeCount) {
        float previousSplit =
            cascade == 0 ? 0.0 : u_shadowCascadeSplits[cascade - 1];
        float blendWidth =
            (u_shadowCascadeSplits[cascade] - previousSplit) * 0.12;
        float blend = smoothstep(
            u_shadowCascadeSplits[cascade] - blendWidth,
            u_shadowCascadeSplits[cascade], viewDepth);
        if (blend > 0.0) {
            float nextVisibility =
                SampleShadowCascade(cascade + 1, pos, N, lightDir);
            visibility = mix(visibility, nextVisibility, blend);
        }
    }
    return mix(0.08, 1.0, visibility);
}

float PointShadowVisibility(int lightIndex, Light L, vec3 pos, vec3 N)
{
    if (u_pointShadowEnabled == 0 || lightIndex != u_pointShadowLightIndex ||
        L.type != 1)
        return 1.0;

    vec3 lightDir = normalize(L.position - pos);
    float surfaceAngle = max(dot(N, lightDir), 0.0);
    float unoffsetDistance = length(pos - L.position);
    float texelWorldSize =
        max(unoffsetDistance, 0.001) * 2.0 /
        float(textureSize(u_pointShadowMap, 0).x);
    float normalOffset =
        texelWorldSize * mix(1.5, 4.0, 1.0 - surfaceAngle);
    vec3 receiverPos = pos + N * normalOffset;
    vec3 lightToFrag = receiverPos - L.position;
    float currentDistance = length(lightToFrag);
    if (currentDistance > L.radius)
        return 1.0;

    float bias =
        max(texelWorldSize * mix(1.25, 3.0, 1.0 - surfaceAngle), 0.0015);
    vec3 axis = abs(lightDir.y) < 0.98 ? vec3(0.0, 1.0, 0.0)
                                      : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(axis, lightDir));
    vec3 bitangent = cross(lightDir, tangent);
    float sourceRadius = clamp(L.radius * 0.004, 0.025, 0.10);

    float blockerDistance = 0.0;
    int blockerCount = 0;
    for (int i = 0; i < 24; ++i) {
        float sampleIndex = float(i) + 0.5;
        float radius = sqrt(sampleIndex / 24.0);
        float angle = sampleIndex * 2.39996323;
        vec2 disk = vec2(cos(angle), sin(angle)) * radius * sourceRadius;
        vec3 sampleVector =
            lightToFrag + tangent * disk.x + bitangent * disk.y;
        float closestDistance = texture(u_pointShadowMap, sampleVector).r;
        if (length(sampleVector) - bias > closestDistance) {
            blockerDistance += closestDistance;
            blockerCount += 1;
        }
    }
    if (blockerCount == 0)
        return 1.0;

    blockerDistance /= float(blockerCount);
    float penumbra =
        max(currentDistance - blockerDistance, 0.0) /
        max(blockerDistance, 0.001);
    float diskRadius =
        clamp(texelWorldSize * 1.5 + sourceRadius * penumbra,
              texelWorldSize * 1.5,
              max(sourceRadius * 1.5, texelWorldSize * 2.0));
    int filterSampleCount = penumbra < 0.04 ? 24 : 48;
    float shadow = 0.0;
    for (int i = 0; i < 48; ++i) {
        if (i >= filterSampleCount)
            break;
        float sampleIndex = float(i) + 0.5;
        float radius = sqrt(sampleIndex / float(filterSampleCount));
        float angle = sampleIndex * 2.39996323;
        vec2 disk = vec2(cos(angle), sin(angle)) * radius * diskRadius;
        vec3 sampleVector =
            lightToFrag + tangent * disk.x + bitangent * disk.y;
        float closestDistance = texture(u_pointShadowMap, sampleVector).r;
        shadow += length(sampleVector) - bias > closestDistance ? 1.0 : 0.0;
    }
    shadow /= float(filterSampleCount);
    return 1.0 - shadow * 0.92;
}

void main()
{
    vec2 uv = v_uv * u_uvScale;

    vec3 N = normalize(v_worldNormal);
    if (u_hasNormalMap != 0) {
        // HORO-67 plumbing: TBN tangent-space transform deferred (no vertex tangents yet).
        vec3 nMap = texture(u_normalMap, uv).rgb * 2.0 - 1.0;
        N = normalize(nMap);
    }

    vec3 V      = normalize(u_cameraPos - v_worldPos);
    vec3 albedo = (u_hasTexture != 0)
                  ? texture(u_albedoMap, uv).rgb * u_color.rgb
                  : u_color.rgb;

    float roughness = u_roughness;
    float metallic  = u_metallic;
    if (u_hasMetallicRoughnessMap != 0) {
        vec3 mr   = texture(u_metallicRoughnessMap, uv).rgb;
        roughness = mr.g;
        metallic  = mr.b;
    }

    float ao = 1.0;
    if (u_hasOcclusionMap != 0)
        ao = texture(u_occlusionMap, uv).r;

    vec3 emissive = vec3(0.0);
    if (u_hasEmissiveMap != 0)
        emissive = texture(u_emissiveMap, uv).rgb;

    vec3 result = vec3(0.0);

    for (int i = 0; i < u_lightCount; ++i) {
        vec3 contribution = CalcLight(u_lights[i], v_worldPos, N, V, albedo, roughness, metallic);
        float visibility = ShadowVisibility(i, u_lights[i], v_worldPos, N) *
                           PointShadowVisibility(i, u_lights[i], v_worldPos, N);
        result += contribution * visibility;
    }

    result += emissive;

    result = result / (result + vec3(1.0));
    result = pow(result, vec3(1.0 / 2.2));

    FragColor = vec4(result, u_color.a);
}
