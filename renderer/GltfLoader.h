#pragma once
#include <memory>
#include <string>
#include <vector>

#include "renderer/AnimationClip.h"
#include "renderer/Skeleton.h"
#include "renderer/SkinnedMesh.h"

namespace Monolith {
    class Texture;

    // Result of loading a single .glb / .gltf file.
    // The file may contain multiple meshes (skins share one skeleton) but this
    // loader extracts the first skin and merges all primitives of the first mesh.
    struct GltfLoadResult {
        std::shared_ptr<SkinnedMesh> mesh;
        std::shared_ptr<Skeleton> skeleton;
        std::vector<std::shared_ptr<AnimationClip> > clips;
        std::shared_ptr<Texture> albedoTexture; // may be null
    };

    namespace GltfLoader {
        // Load a .glb or .gltf file.
        // Returns an empty GltfLoadResult (null mesh) on failure; logs to stderr.
        GltfLoadResult Load(const std::string &path);
    } // namespace GltfLoader
} // namespace Monolith
