#pragma once
#include <memory>

#include "math/Vec4.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"

namespace Horo {
    class Material {
    public:
        Vec4 color = {1, 1, 1, 1};
        float roughness = 0.5f;
        float metallic = 0.0f;

        std::shared_ptr<Texture> albedoMap;             /**< Optional baseColor texture; falls back to u_color when null. */
        std::shared_ptr<Texture> normalMap;             /**< Optional tangent-space normal map. */
        std::shared_ptr<Texture> metallicRoughnessMap;  /**< Optional combined metallic-roughness map (glTF: G=roughness, B=metallic). */
        std::shared_ptr<Texture> emissiveMap;           /**< Optional emissive texture. */
        std::shared_ptr<Texture> occlusionMap;          /**< Optional ambient-occlusion texture (glTF: R channel). */

        float uvScale =
                1.0f; // texture tiling multiplier (>1 = more tiles = zoomed out)

        std::shared_ptr<Shader>
        shader; // shared resource handle used by renderer backends

        bool HasShader() const { return shader && shader->IsValid(); }
    };
} // namespace Horo
