#pragma once

/**
 * @file PrimitiveMeshDescriptor.h
 * @brief Backend-neutral authored descriptors for procedural primitive meshes.
 */

#include "Horo/Math/SceneMath.h"
#include "Horo/Runtime/Scene/PrimitiveCatalog.h"

#include <cstdint>
#include <variant>

namespace Horo::Runtime
{
/** @brief Schema version of a serialized procedural primitive descriptor. */
struct PrimitiveMeshVersion
{
    std::uint32_t value{1};
    [[nodiscard]] constexpr auto operator<=>(const PrimitiveMeshVersion &) const noexcept = default;
};
/** @brief Local dimensions of a centered procedural box. */
struct BoxMeshParameters
{
    Math::Vec3 size{1.0F, 1.0F, 1.0F};
    [[nodiscard]] constexpr auto operator<=>(const BoxMeshParameters &) const noexcept = default;
};
/** @brief Radius and UV-sphere tessellation counts. */
struct SphereMeshParameters
{
    float radius{0.5F};
    std::uint32_t slices{32};
    std::uint32_t stacks{16};
    [[nodiscard]] constexpr auto operator<=>(const SphereMeshParameters &) const noexcept = default;
};
/** @brief Total capsule dimensions and per-cap tessellation counts. */
struct CapsuleMeshParameters
{
    float radius{0.5F};
    float totalHeight{2.0F};
    std::uint32_t radialSegments{32};
    std::uint32_t hemisphereRings{8};
    [[nodiscard]] constexpr auto operator<=>(const CapsuleMeshParameters &) const noexcept = default;
};
/** @brief Centered cylinder dimensions and radial tessellation count. */
struct CylinderMeshParameters
{
    float radius{0.5F};
    float height{1.0F};
    std::uint32_t radialSegments{32};
    [[nodiscard]] constexpr auto operator<=>(const CylinderMeshParameters &) const noexcept = default;
};
/** @brief Base-centered cone dimensions and radial tessellation count. */
struct ConeMeshParameters
{
    float radius{0.5F};
    float height{1.0F};
    std::uint32_t radialSegments{32};
    [[nodiscard]] constexpr auto operator<=>(const ConeMeshParameters &) const noexcept = default;
};
/** @brief XZ dimensions of a positive-Y-facing plane. */
struct PlaneMeshParameters
{
    Math::Vec2 size{10.0F, 10.0F};
    [[nodiscard]] constexpr auto operator<=>(const PlaneMeshParameters &) const noexcept = default;
};
/** @brief XY dimensions of a positive-Z-facing quad. */
struct QuadMeshParameters
{
    Math::Vec2 size{1.0F, 1.0F};
    [[nodiscard]] constexpr auto operator<=>(const QuadMeshParameters &) const noexcept = default;
};

/** @brief Closed set of version-one procedural primitive parameter payloads. */
using PrimitiveMeshParameters =
    std::variant<BoxMeshParameters, SphereMeshParameters, CapsuleMeshParameters, CylinderMeshParameters,
                 ConeMeshParameters, PlaneMeshParameters, QuadMeshParameters>;

/** @brief Authored primitive recipe containing no process-local render handle. */
struct PrimitiveMeshDescriptor
{
    PrimitiveMeshType type{PrimitiveMeshType::Box};
    PrimitiveMeshVersion version{};
    PrimitiveMeshParameters parameters{BoxMeshParameters{}};
    /**
     * @brief Creates the version-one default descriptor for a supported mesh primitive.
     * @param type
     * Procedural mesh type whose matching parameter payload is selected.
     * @return Backend-neutral descriptor with
     * canonical default dimensions.
     */
    [[nodiscard]] static PrimitiveMeshDescriptor Defaults(PrimitiveMeshType type) noexcept;
    [[nodiscard]] constexpr bool operator==(const PrimitiveMeshDescriptor &) const noexcept = default;
};
} // namespace Horo::Runtime
