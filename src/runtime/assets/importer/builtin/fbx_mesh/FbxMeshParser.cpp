#include "FbxMeshParser.h"

#include "../../../AssetErrors.h"

#include <ufbx.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Horo::Assets {
namespace {
constexpr std::size_t kMaximumSourceBytes = 512U * 1024U * 1024U;
constexpr std::size_t kMaximumParserMemoryBytes = 256U * 1024U * 1024U;
constexpr std::size_t kMaximumVertices = 1'000'000U;
constexpr std::size_t kMaximumTriangleIndices = 6'000'000U;

struct SceneDeleter {
    void operator()(ufbx_scene *scene) const noexcept {
        ufbx_free_scene(scene);
    }
};

[[nodiscard]] Result<FbxMeshGeometry> FbxFailure(const ufbx_error &error) {
    const std::string message{
        error.description.data != nullptr ? error.description.data : "",
        error.description.length,
    };
    return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed, message));
}
} // namespace

/** @copydoc ParseFbxMesh */
Result<FbxMeshGeometry> ParseFbxMesh(const std::span<const std::uint8_t> source) {
    if (source.empty() || source.size() > kMaximumSourceBytes)
        return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed));

    ufbx_load_opts options{};
    options.ignore_animation = true;
    options.ignore_embedded = true;
    options.skip_mesh_parts = true;
    options.skip_skin_vertices = true;
    options.node_depth_limit = 128;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    options.temp_allocator.memory_limit = kMaximumParserMemoryBytes;
    options.result_allocator.memory_limit = kMaximumParserMemoryBytes;

    ufbx_error error{};
    std::unique_ptr<ufbx_scene, SceneDeleter> scene{
        ufbx_load_memory(source.data(), source.size(), &options, &error)};
    if (!scene)
        return FbxFailure(error);

    FbxMeshGeometry result;
    std::vector<std::uint32_t> triangulatedCorners;
    for (const ufbx_node *node : scene->nodes) {
        const ufbx_mesh *mesh = node != nullptr ? node->mesh : nullptr;
        if (mesh == nullptr || mesh->vertices.count == 0 || mesh->faces.count == 0)
            continue;
        if (result.positions.size() > kMaximumVertices - mesh->vertices.count)
            return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed));

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(result.positions.size());
        result.positions.reserve(result.positions.size() + mesh->vertices.count);
        for (const ufbx_vec3 localPosition : mesh->vertices) {
            const ufbx_vec3 worldPosition =
                ufbx_transform_position(&node->geometry_to_world, localPosition);
            const std::array<float, 3> position{
                static_cast<float>(worldPosition.x),
                static_cast<float>(worldPosition.y),
                static_cast<float>(worldPosition.z),
            };
            if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
                !std::isfinite(position[2])) {
                return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed));
            }
            result.positions.push_back(position);
        }

        if (mesh->max_face_triangles > std::numeric_limits<std::size_t>::max() / 3U) {
            return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed));
        }
        triangulatedCorners.resize(mesh->max_face_triangles * 3U);
        for (const ufbx_face face : mesh->faces) {
            if (face.num_indices < 3)
                continue;
            const std::uint32_t triangleCount = ufbx_triangulate_face(
                triangulatedCorners.data(), triangulatedCorners.size(), mesh, face);
            const std::size_t cornerCount = static_cast<std::size_t>(triangleCount) * 3U;
            if (result.triangleIndices.size() > kMaximumTriangleIndices - cornerCount) {
                return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed));
            }
            for (std::size_t corner = 0; corner < cornerCount; ++corner) {
                const std::uint32_t polygonCorner = triangulatedCorners[corner];
                if (polygonCorner >= mesh->vertex_indices.count)
                    return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed));
                const std::uint32_t logicalVertex = mesh->vertex_indices[polygonCorner];
                if (logicalVertex >= mesh->vertices.count)
                    return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed));
                result.triangleIndices.push_back(baseVertex + logicalVertex);
            }
        }
    }

    if (result.positions.empty() || result.triangleIndices.empty())
        return Result<FbxMeshGeometry>::Failure(MakeError(ImportErrors::FbxMalformed));
    return Result<FbxMeshGeometry>::Success(std::move(result));
}
} // namespace Horo::Assets
