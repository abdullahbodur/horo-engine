#version 410 core

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in vec2  a_uv;
layout(location = 3) in ivec4 a_boneIndices;   // -1 = unused slot
layout(location = 4) in vec4  a_boneWeights;   // sum of active weights == 1.0

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat4 u_boneMatrices[64];   // skinning palette: inverseBindPose * localTransform

out vec3 v_worldPos;
out vec3 v_worldNormal;
out vec2 v_uv;

void main()
{
    // -----------------------------------------------------------------------
    // GPU Skinning
    // Accumulate the weighted bone transforms for this vertex.
    // We iterate over all 4 influence slots and skip any with index == -1
    // or a weight of 0.0, so sparse rigs don't pay for unused slots.
    // -----------------------------------------------------------------------
    mat4 skinMat    = mat4(0.0);
    float totalWeight = 0.0;

    // PERF: Unroll is intentionally avoided; the loop body is ALU-light and
    // the branch on index/weight lets the driver predicate cleanly on most
    // hardware. A fully unrolled version with mix() would require all 4
    // matrix fetches regardless of sparsity.
    for (int i = 0; i < 4; ++i)
    {
        int   idx    = a_boneIndices[i];
        float weight = a_boneWeights[i];

        if (idx >= 0 && weight > 0.0)
        {
            skinMat     += weight * u_boneMatrices[idx];
            totalWeight += weight;
        }
    }

    // SYNC: If no valid bone influence was found (all indices are -1),
    // fall back to the identity transform so the vertex renders correctly
    // for un-skinned draw calls reusing this shader path.
    if (totalWeight < 1e-4)
        skinMat = mat4(1.0);

    // -----------------------------------------------------------------------
    // Transform: bind-pose -> world space
    // Apply skinMat first (bone deformation in local/bind space), then
    // u_model (object -> world).  Normal uses the upper-left 3x3 of skinMat;
    // the full per-model normalMat handles non-uniform scale on the model
    // matrix side.
    // -----------------------------------------------------------------------
    vec4 skinnedPos    = skinMat * vec4(a_position, 1.0);
    vec3 skinnedNormal = normalize(mat3(skinMat) * a_normal);

    vec4 worldPos  = u_model * skinnedPos;
    v_worldPos     = worldPos.xyz;

    // PERF: transpose(inverse()) is computed per-vertex here to match
    // basic.vert behaviour.  For a production path, upload a pre-computed
    // normalMatrix uniform instead.
    mat3 normalMat = transpose(inverse(mat3(u_model)));
    v_worldNormal  = normalize(normalMat * skinnedNormal);

    v_uv = a_uv;

    gl_Position = u_projection * u_view * worldPos;
}
