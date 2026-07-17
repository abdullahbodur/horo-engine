#pragma once

/**
 * @file PrimitiveMesh.h
 * @brief Versioned procedural primitive descriptors, deterministic generation, and bounded caching.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Render/Mesh.h"
#include "Horo/Runtime/Scene/PrimitiveCatalog.h"

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
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

/** @brief Authored, serializable primitive recipe; it contains no process-local render handle. */
struct PrimitiveMeshDescriptor
{
    PrimitiveMeshType type{PrimitiveMeshType::Box};
    PrimitiveMeshVersion version{};
    PrimitiveMeshParameters parameters{BoxMeshParameters{}};

    /** @brief Creates the canonical version-one descriptor for @p type. @param type Built-in mesh type. @return
     * Matching descriptor and default typed payload. */
    [[nodiscard]] static PrimitiveMeshDescriptor Defaults(PrimitiveMeshType type) noexcept;
    [[nodiscard]] constexpr bool operator==(const PrimitiveMeshDescriptor &) const noexcept = default;
};

/** @brief Stateless deterministic generator for built-in procedural meshes. */
class PrimitiveMeshGenerator final
{
  public:
    /**
     * @brief Validates and generates one immutable triangle mesh.
     * @param descriptor Versioned authored primitive recipe.
     * @return Generated position/normal/UV/index data, or a typed primitive error.
     */
    [[nodiscard]] static Result<Render::MeshData> Generate(const PrimitiveMeshDescriptor &descriptor);
};

/** @brief Copyable lifetime pin for one cache-owned immutable mesh. */
class PrimitiveMeshLease final
{
  public:
    PrimitiveMeshLease() = default;
    PrimitiveMeshLease(PrimitiveMeshLease &&) noexcept = default;
    PrimitiveMeshLease &operator=(PrimitiveMeshLease &&) noexcept = default;
    PrimitiveMeshLease(const PrimitiveMeshLease &) noexcept = default;
    PrimitiveMeshLease &operator=(const PrimitiveMeshLease &) noexcept = default;

    [[nodiscard]] Render::MeshResourceId Id() const noexcept
    {
        return m_id;
    }
    /** @brief Returns pinned immutable mesh data. @pre This lease is valid. */
    [[nodiscard]] const Render::MeshData &Data() const noexcept
    {
        assert(m_data != nullptr);
        return *m_data;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_id.IsValid() && m_data != nullptr;
    }

  private:
    friend class PrimitiveMeshCache;
    PrimitiveMeshLease(Render::MeshResourceId id, std::shared_ptr<const Render::MeshData> data) noexcept
        : m_id(id), m_data(std::move(data))
    {
    }
    Render::MeshResourceId m_id{};
    std::shared_ptr<const Render::MeshData> m_data;
};

/** @brief Item and byte limits for the process-local procedural mesh cache. */
struct PrimitiveMeshCacheLimits
{
    std::size_t maximumItems{128};
    std::size_t maximumBytes{64U * 1024U * 1024U};
};

/** @brief Thread-safe bounded cache keyed by the complete versioned primitive descriptor. */
class PrimitiveMeshCache final
{
  public:
    /** @brief Creates a cache with explicit bounded budgets. @param limits Maximum resident item and CPU-byte counts.
     */
    explicit PrimitiveMeshCache(PrimitiveMeshCacheLimits limits = {});
    ~PrimitiveMeshCache();
    PrimitiveMeshCache(PrimitiveMeshCache &&) noexcept;
    PrimitiveMeshCache &operator=(PrimitiveMeshCache &&) noexcept;
    PrimitiveMeshCache(const PrimitiveMeshCache &) = delete;
    PrimitiveMeshCache &operator=(const PrimitiveMeshCache &) = delete;

    /** @brief Acquires or generates a mesh and pins it against eviction for the lease lifetime. */
    [[nodiscard]] Result<PrimitiveMeshLease> Acquire(const PrimitiveMeshDescriptor &descriptor);
    /** @brief Returns the current resident descriptor count. */
    [[nodiscard]] std::size_t ItemCount() const noexcept;
    /** @brief Returns resident CPU vertex and index bytes. */
    [[nodiscard]] std::size_t ByteCount() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Horo::Runtime
