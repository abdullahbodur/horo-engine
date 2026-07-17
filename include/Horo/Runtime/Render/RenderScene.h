#pragma once

/**
 * @file RenderScene.h
 * @brief Backend-neutral static-mesh scene submission contracts.
 */

#include "Horo/Runtime/Render/Mesh.h"

#include <cmath>
#include <cstdint>
#include <span>

namespace Horo::Render
{
/** @brief Generation-safe identity of one immutable mesh resource. */
struct RenderMeshHandle
{
    MeshResourceId id;
    std::uint32_t generation{0};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return id.IsValid() && generation != 0; }
    [[nodiscard]] constexpr auto operator<=>(const RenderMeshHandle &) const noexcept = default;
};

/** @brief Generation-safe identity of one frontend-owned offscreen target. */
struct RenderTargetHandle
{
    std::uint32_t index{0};
    std::uint32_t generation{0};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return index != 0 && generation != 0; }
    [[nodiscard]] constexpr auto operator<=>(const RenderTargetHandle &) const noexcept = default;
};

/** @brief Supported backend-neutral camera projection families. */
enum class RenderProjectionKind : std::uint8_t
{
    Perspective,
    Orthographic,
};

/** @brief Validated projection values converted to API clip depth by each backend. */
struct RenderProjectionDescriptor
{
    RenderProjectionKind kind{RenderProjectionKind::Perspective};
    float verticalFovRadians{0.9599311F};
    float orthographicHeight{8.0F};
    float nearPlane{0.1F};
    float farPlane{100.0F};

    [[nodiscard]] bool IsValid() const noexcept
    {
        const bool common = std::isfinite(nearPlane) && std::isfinite(farPlane) && nearPlane > 0.0F &&
                            farPlane > nearPlane;
        if (!common)
            return false;
        if (kind == RenderProjectionKind::Perspective)
            return std::isfinite(verticalFovRadians) && verticalFovRadians > 0.0F &&
                   verticalFovRadians < 3.14159265358979323846F;
        return kind == RenderProjectionKind::Orthographic && std::isfinite(orthographicHeight) &&
               orthographicHeight > 0.0F;
    }
};

/** @brief World-space camera values for one render view. */
struct RenderCameraView
{
    Math::Vec3 position{};
    Math::Vec3 target{0.0F, 0.0F, -1.0F};
    Math::Vec3 up{0.0F, 1.0F, 0.0F};
    RenderProjectionDescriptor projection{};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Math::IsFinite(position) && Math::IsFinite(target) && Math::IsFinite(up) &&
               Math::Length(target - position) > Math::DefaultEpsilon &&
               Math::Length(up) > Math::DefaultEpsilon && projection.IsValid();
    }
};

/** @brief Non-owning immutable CPU resource pinned by the producer's owning snapshot. */
struct RenderMeshResourceView
{
    RenderMeshHandle handle;
    std::span<const MeshVertex> vertices;
    std::span<const std::uint32_t> indices;
    Math::Aabb localBounds;

    [[nodiscard]] bool IsValid() const noexcept
    {
        if (!handle.IsValid() || vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
            !localBounds.IsValid())
            return false;
        for (const MeshVertex &vertex : vertices)
            if (!Math::IsFinite(vertex.position) || !Math::IsFinite(vertex.normal) || !Math::IsFinite(vertex.uv))
                return false;
        for (const std::uint32_t index : indices)
            if (index >= vertices.size())
                return false;
        return true;
    }
};

/** @brief Generic per-instance presentation values resolved before backend execution. */
struct RenderInstancePresentation
{
    Math::Vec3 tint{1.0F, 1.0F, 1.0F};
    float tintStrength{0.0F};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Math::IsFinite(tint) && std::isfinite(tintStrength) && tintStrength >= 0.0F &&
               tintStrength <= 1.0F;
    }
};

/** @brief One static-mesh draw instance with no authored-scene or editor identity. */
struct RenderStaticMeshInstance
{
    RenderMeshHandle mesh;
    Math::Mat4 localToWorld{Math::Mat4::Identity()};
    Math::Aabb localBounds{{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
    MaterialBindingId material{CoreDefaultMaterial};
    RenderInstancePresentation presentation{};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return mesh.IsValid() && Math::IsFinite(localToWorld) && localBounds.IsValid() && material.IsValid() &&
               presentation.IsValid();
    }
};

/** @brief Synchronous non-owning scene view consumed during one Execute call. */
struct RenderSceneView
{
    RenderCameraView camera;
    std::span<const RenderMeshResourceView> meshResources;
    std::span<const RenderStaticMeshInstance> instances;

    [[nodiscard]] bool IsValid() const noexcept
    {
        if (!camera.IsValid())
            return false;
        for (const RenderMeshResourceView &resource : meshResources)
            if (!resource.IsValid())
                return false;
        for (const RenderStaticMeshInstance &instance : instances)
            if (!instance.IsValid())
                return false;
        return true;
    }
};
} // namespace Horo::Render
