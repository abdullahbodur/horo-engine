#version 410 core

in vec3 v_worldPos;
in vec3 v_worldNormal;
in vec2 v_uv;

uniform vec4      u_color      = vec4(1.0);
uniform float     u_roughness  = 0.5;
uniform float     u_metallic   = 0.0;
uniform vec3      u_cameraPos;
uniform sampler2D u_albedoMap;
uniform int       u_hasTexture = 0;
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
    if (L.type == 0) {
        L_dir = normalize(-L.direction);
    } else {
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
    vec3 N      = normalize(v_worldNormal);
    vec3 V      = normalize(u_cameraPos - v_worldPos);
    vec3 albedo = (u_hasTexture != 0)
                  ? texture(u_albedoMap, v_uv * u_uvScale).rgb * u_color.rgb
                  : u_color.rgb;
    vec3 result = 0.05 * albedo;

    for (int i = 0; i < u_lightCount; ++i)
        result += CalcLight(u_lights[i], v_worldPos, N, V, albedo, u_roughness, u_metallic);

    result = result / (result + 1.0);

    FragColor = vec4(result, u_color.a);
}
