#pragma once

/**
 * @file RenderBackendRegistry.h
 * @brief Host-owned registry for explicitly linked renderer backend modules.
 */

#include "Horo/Runtime/Render/RenderBackend.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Horo::Render
{
/**
 * @brief Registry-owned provider of inert renderer backend instances.
 *
 * Providers may capture backend-specific platform services without exposing
 * native handles through RenderApi. Create must not acquire a graphics device,
 * context, surface, or other runtime resource; acquisition belongs to
 * IRenderBackend::Initialize. Borrowed provider dependencies must outlive the
 * provider, and dependencies borrowed by a returned backend must outlive that
 * backend. The registry may invoke Create zero or more times, serially on the
 * composition thread; const does not imply thread safety. Each invocation must
 * return an independent inert backend. Provider exceptions are translated by
 * the registry.
 */
class IRenderBackendProvider
{
  public:
    virtual ~IRenderBackendProvider() = default;

    /** @brief Constructs one inert backend instance. */
    [[nodiscard]] virtual Result<std::unique_ptr<IRenderBackend>> Create() const = 0;
};

/** @brief Owned backend identity, presentation name, and inert instance provider. */
struct RenderBackendDescriptor
{
    RenderBackendId id;
    std::string displayName;
    std::unique_ptr<IRenderBackendProvider> provider;
};

/**
 * @brief Deterministic composition-time registry of renderer backend modules.
 *
 * The composition root owns this registry. Registration is explicit and must
 * finish before Seal. The registry performs no device, context, window, or
 * thread initialization.
 *
 * @note This type is intentionally not thread-safe. Build and seal it on the
 * composition thread before renderer worker threads start.
 */
class RenderBackendRegistry
{
  public:
    /**
     * @brief Registers one backend descriptor by value.
     * @param descriptor Move-only descriptor consumed on entry. On success the
     * registry retains its provider; on validation failure the provider is destroyed
     * before this call returns.
     * @return Success, or a typed error for invalid, duplicate, or late registration.
     */
    [[nodiscard]] Result<void> Register(RenderBackendDescriptor descriptor);

    /**
     * @brief Prevents later registration and enables backend creation.
     * @return Success. Repeated calls are idempotent.
     */
    [[nodiscard]] Result<void> Seal() noexcept;

    /**
     * @brief Creates an inert backend instance from a sealed registry on the composition thread.
     * @param id Stable identity selected by host policy.
     * @return Owned backend, or a typed error when unsealed, unknown, null, or when provider creation fails or throws.
     */
    [[nodiscard]] Result<std::unique_ptr<IRenderBackend>> Create(const RenderBackendId &id) const;

    /**
     * @brief Returns descriptors in deterministic registration order.
     * @return Non-owning view invalidated by a successful Register call and
     * stable for the remaining registry lifetime after Seal.
     */
    [[nodiscard]] std::span<const RenderBackendDescriptor> Descriptors() const noexcept;

    /** @brief Reports whether registration has been permanently closed. */
    [[nodiscard]] bool IsSealed() const noexcept
    {
        return sealed_;
    }

  private:
    std::vector<RenderBackendDescriptor> descriptors_;
    bool sealed_{false};
};
} // namespace Horo::Render
