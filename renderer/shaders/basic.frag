#version 410 core

in vec3 v_worldPos;
in vec3 v_worldNormal;
in vec2 v_uv;

uniform vec4      u_color      = vec4(1.0);
uniform float     u_roughness  = 0.5;
uniform float     u_metallic   = 0.0;
uniform vec3      u_cameraPos;

// glTF-aligned PBR texture slots (HORO-67). Each map is gated by a u_has*Map int
// so unused slots stay free. Texture binding order is set by the renderer backends:
//   slot 0 = albedoMap, slot 1 = normalMap, slot 2 = metallicRoughnessMap,
//   slot 3 = emissiveMap,   slot 4 = occlusionMap.
uniform sampler2D u_albedoMap;
uniform sampler2D u_normalMap;
uniform sampler2D u_metallicRoughnessMap;
uniform sampler2D u_emissiveMap;
uniform sampler2D u_occlusionMap;
uniform int       u_hasTexture              = 0; /* legacy alias for albedo presence */
uniform int       u_hasNormalMap            = 0;
uniform int       u_hasMetallicRoughnessMap = 0;
uniform int       u_hasEmissiveMap          = 0;
uniform int       u_hasOcclusionMap         = 0;
uniform float     u_uvScale    = 1.0;

struct Light {
    int   type;        // 0 = directional, 1 = point
    vec3  position;
    vec3  direction;
    vec3  color;       // pre-multiplied by intensity
    float radius;
};
uniform Light u_lights[8];
uniform int   u_lightCount;

out vec4 FragColor;

vec3 CalcLight(Light L, vec3 pos, vec3 N, vec3 V, vec3 albedo, float rough, float metal)
{
    vec3  L_dir;
    float attenuation = 1.0;
    if (L.type == 0) {                          // directional
        L_dir = normalize(-L.direction);
    } else {                                    // point
        vec3  toLight = L.position - pos;
        float d       = length(toLight);
        if (d > L.radius) return vec3(0.0);
        L_dir = toLight / d;
        attenuation = 1.0 / (1.0 + 0.09 * d + 0.032 * d * d);
    }
    float diff      = max(dot(N, L_dir), 0.0);
    vec3  H         = normalize(L_dir + V);
    float shininess = mix(8.0, 256.0, 1.0 - rough);
    float spec      = pow(max(dot(N, H), 0.0), shininess) * (1.0 - rough) * (1.0 - rough);
    vec3  specColor = mix(vec3(0.04), albedo, metal);

    return attenuation * L.color * (diff * albedo + spec * specColor);
}

void main()
{
    vec2 uv     = v_uv * u_uvScale;

    vec3 N = normalize(v_worldNormal);
    if (u_hasNormalMap != 0) {
        // HORO-67 plumbing: without per-vertex tangents we cannot build a TBN
        // matrix, so tangent-space normal maps will not light correctly. The
        // sample is still applied so authoring round-trips work; proper TBN
        // shading is tracked as a follow-up.
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
        roughness = mr.g;  // glTF convention
        metallic  = mr.b;
    }

    float ao = 1.0;
    if (u_hasOcclusionMap != 0)
        ao = texture(u_occlusionMap, uv).r;

    vec3 emissive = vec3(0.0);
    if (u_hasEmissiveMap != 0)
        emissive = texture(u_emissiveMap, uv).rgb;

    vec3 result = 0.05 * ao * albedo;  // ambient

    for (int i = 0; i < u_lightCount; ++i)
        result += CalcLight(u_lights[i], v_worldPos, N, V, albedo, roughness, metallic);

    result += emissive;

    // Reinhard tone map
    result = result / (result + 1.0);

    FragColor = vec4(result, u_color.a);
}
