#pragma once

/**
 * @file Mesh.h
 * @brief Backend-neutral immutable triangle-mesh data shared by runtime producers and render extraction.
 */

#include "Horo/Math/SceneMath.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Horo::Render
{
    /** @brief Vertex layout required by the current generic static-mesh render contract. */
    struct MeshVertex
    {
        Math::Vec3 position;
        Math::Vec3 normal;
        Math::Vec2 uv;

        [[nodiscard]] constexpr auto operator<=>(const MeshVertex&) const noexcept = default;
    };

    /** @brief Owning immutable-source payload produced before backend upload. */
    struct MeshData
    {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        Math::Aabb localBounds;

        /** @brief Reports whether this is finite indexed triangle data with valid index references. */
        [[nodiscard]] bool IsValid() const noexcept
        {
            if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 || !localBounds.IsValid())
                return false;
            for (const MeshVertex& vertex : vertices)
            {
                if (!Math::IsFinite(vertex.position) || !Math::IsFinite(vertex.normal) || !Math::IsFinite(vertex.uv))
                    return false;
            }
            for (const std::uint32_t index : indices)
            {
                if (index >= vertices.size())
                    return false;
            }
            return true;
        }

        /** @brief Returns the CPU bytes represented by vertices and indices. */
        [[nodiscard]] std::size_t ByteSize() const noexcept
        {
            return vertices.size() * sizeof(MeshVertex) + indices.size() * sizeof(std::uint32_t);
        }
    };

    /** @brief Process-local immutable mesh identity; never serialized into authored scene data. */
    struct MeshResourceId
    {
        std::uint64_t value{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const MeshResourceId&) const noexcept = default;
    };

    /** @brief Typed logical material binding resolved by extraction without exposing backend handles. */
    struct MaterialBindingId
    {
        std::string_view value;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return !value.empty();
        }

        [[nodiscard]] constexpr auto operator<=>(const MaterialBindingId&) const noexcept = default;
    };

    inline constexpr MaterialBindingId CoreDefaultMaterial{"core.materials.default"};
} // namespace Horo::Render
