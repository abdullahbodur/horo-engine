#pragma once

/**
 * @file PrimitiveMesh.h
 * @brief Versioned procedural primitive descriptors, deterministic generation, and bounded caching.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Render/Mesh.h"
#include "Horo/Runtime/Scene/PrimitiveMeshDescriptor.h"

#include <cassert>
#include <memory>

namespace Horo::Runtime
{
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
