#pragma once

/**
 * @file Mesh.h
 * @brief Backend-neutral immutable triangle-mesh data shared by runtime producers and render extraction.
 */

#include "Horo/Math/SceneMath.h"
#include "Horo/Runtime/Render/RenderResourceDescriptors.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <string_view>
#include <vector>

namespace Horo::Render {
    /** @brief Backend-neutral index encoding for an immutable mesh. */
    enum class RenderIndexFormat : std::uint8_t {
        UInt16,
        UInt32,
    };

    /** @brief Backend-neutral primitive assembly supported by the baseline mesh contract. */
    enum class RenderPrimitiveTopology : std::uint8_t {
        Triangles,
    };

    /** @brief Immutable composite descriptor over exact resident buffer generations. */
    struct RenderMeshDescriptor {
        RenderBufferHandle vertexBuffer;
        RenderBufferHandle indexBuffer;
        std::uint32_t vertexStride{0};
        std::uint32_t vertexCount{0};
        RenderIndexFormat indexFormat{RenderIndexFormat::UInt32};
        std::uint32_t indexCount{0};
        RenderPrimitiveTopology topology{RenderPrimitiveTopology::Triangles};
        Math::Aabb localBounds;

        /** @brief Reports whether fields are structurally valid before dependency lookup. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            const bool indexFormatValid = indexFormat == RenderIndexFormat::UInt16 || indexFormat == RenderIndexFormat::UInt32;
            return vertexBuffer.IsValid() && indexBuffer.IsValid() && vertexStride > 0 && vertexCount > 0 && indexCount > 0 &&
                   indexCount % 3 == 0 && indexFormatValid && topology == RenderPrimitiveTopology::Triangles && localBounds.IsValid();
        }
    };

    /** @brief Vertex layout required by the current generic static-mesh render contract. */
    struct MeshVertex {
        Math::Vec3 position;
        Math::Vec3 normal;
        Math::Vec2 uv;

        [[nodiscard]] constexpr auto operator<=>(const MeshVertex &) const noexcept = default;
    };

    /** @brief Owning immutable-source payload produced before backend upload. */
    struct MeshData {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        Math::Aabb localBounds;

        /** @brief Reports whether this is finite indexed triangle data with valid index references. */
        [[nodiscard]] bool IsValid() const noexcept {
            if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 || !localBounds.IsValid())
                return false;
            if (const bool verticesValid = std::ranges::all_of(vertices,
                                                               [](const MeshVertex &vertex) noexcept {
                return Math::IsFinite(vertex.position) && Math::IsFinite(vertex.normal) && Math::IsFinite(vertex.uv);
            });
                !verticesValid)
                return false;
            return std::ranges::all_of(indices, [vertexCount = vertices.size()](const std::uint32_t index) noexcept {
                return index < vertexCount;
            });
        }

        /** @brief Returns the CPU bytes represented by vertices and indices. */
        [[nodiscard]] std::size_t ByteSize() const noexcept {
            return vertices.size() * sizeof(MeshVertex) + indices.size() * sizeof(std::uint32_t);
        }
    };

    /** @brief Process-local immutable mesh identity; never serialized into authored scene data. */
    struct MeshResourceId {
        std::uint64_t value{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const MeshResourceId &) const noexcept = default;
    };

    /** @brief Typed logical material binding resolved by extraction without exposing backend handles. */
    struct MaterialBindingId {
        std::string_view value;

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return !value.empty();
        }

        [[nodiscard]] constexpr auto operator<=>(const MaterialBindingId &) const noexcept = default;
    };

    inline constexpr MaterialBindingId CoreDefaultMaterial{"core.materials.default"};
}  // namespace Horo::Render
