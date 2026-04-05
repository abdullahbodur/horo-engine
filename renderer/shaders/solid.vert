#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
out vec3 v_worldPos;
out vec3 v_worldNormal;
out vec2 v_uv;
void main() {
    vec4 wp       = u_model * vec4(a_position, 1.0);
    gl_Position   = u_projection * u_view * wp;
    v_worldPos    = wp.xyz;
    v_worldNormal = mat3(u_model) * a_normal;
    v_uv          = a_uv;
}
