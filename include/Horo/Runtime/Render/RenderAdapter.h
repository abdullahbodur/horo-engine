#pragma once

/**
 * @file RenderAdapter.h
 * @brief Backend-neutral adapter discovery, selection, and device-creation contracts.
 */

#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Render {
    /** @brief Stable backend-scoped adapter identity with no native handle semantics. */
    class RenderAdapterId {
    public:
        RenderAdapterId() = default;

        /** @brief Creates an identity from a backend-published stable value. */
        explicit RenderAdapterId(std::string value) : value_(std::move(value)) {}

        /** @brief Returns the backend-published stable value. */
        [[nodiscard]] const std::string &Value() const noexcept {
            return value_;
        }

        /** @brief Reports whether the value is a bounded printable identifier. */
        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] auto operator<=>(const RenderAdapterId &) const noexcept = default;

    private:
        std::string value_;
    };

    /** @brief Backend-neutral physical or logical device category. */
    enum class RenderAdapterKind : std::uint8_t {
        Integrated,
        Discrete,
        Virtual,
        Software,
        Unknown,
    };

    /** @brief Current device-creation availability reported during discovery. */
    enum class RenderAdapterAvailability : std::uint8_t {
        Available,
        Unavailable,
    };

    /** @brief Immutable facts published for one discovered adapter. */
    struct RenderAdapterProperties {
        RenderAdapterId id;                                                             /**< Stable identity within the selected backend. */
        std::string displayName;                                                        /**< Bounded user-facing diagnostic name. */
        RenderAdapterKind kind{RenderAdapterKind::Unknown};                             /**< Backend-neutral device category. */
        RenderAdapterAvailability availability{RenderAdapterAvailability::Unavailable}; /**< Creation availability. */
        std::uint64_t dedicatedVideoMemoryBytes{0}; /**< Advisory dedicated-memory fact, or zero if unknown. */
        bool supportsPresentation{false};           /**< Whether host presentation creation may be attempted. */

        /** @brief Reports whether identities, text, and enum values are well formed. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /** @brief Finite synchronous discovery limits supplied by the host. */
    struct RenderAdapterDiscoveryRequest {
        std::uint32_t maxAdapters{16}; /**< Maximum records the backend may publish. */

        /** @brief Reports whether the request is finite and within the engine hard limit. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return maxAdapters > 0 && maxAdapters <= 64;
        }
    };

    /** @brief Owned immutable discovery result ordered by adapter identity. */
    struct RenderAdapterSnapshot {
        std::uint64_t revision{0};                     /**< Non-zero backend-lifetime discovery revision. */
        std::vector<RenderAdapterProperties> adapters; /**< Strictly identity-sorted unique records. */

        /** @brief Reports whether the revision and every bounded ordered record are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /** @brief Host selection constraints; omitted fields never invent a fallback. */
    struct RenderAdapterSelectionRequest {
        std::optional<RenderAdapterId> requiredAdapter; /**< Exact adapter identity when explicitly selected. */
        std::optional<RenderAdapterKind> requiredKind;  /**< Exact kind constraint when product policy requires one. */
        bool requirePresentation{false};                /**< Reject adapters without presentation support. */
        bool allowSoftware{false};                      /**< Permit software adapters to satisfy the request. */

        /** @brief Reports whether optional identities and enum values are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /** @brief Selected adapter plus the discovery revision on which selection was based. */
    struct RenderAdapterSelection {
        RenderAdapterProperties adapter;    /**< Copy of the selected immutable facts. */
        std::uint64_t discoveryRevision{0}; /**< Revision that device creation must revalidate. */
    };

    /** @brief Stable category attached to a typed device-creation failure diagnostic. */
    enum class RenderDeviceCreationFailureKind : std::uint8_t {
        AdapterUnavailable,
        RequirementsUnsupported,
        DriverRejected,
        OutOfMemory,
        DeviceLost,
        Cancelled,
        ShuttingDown,
        Unknown,
    };

    /** @brief Bounded native-free diagnostic preserved when device creation fails. */
    struct RenderDeviceCreationFailure {
        RenderAdapterId adapter;                                                        /**< Adapter whose creation attempt failed. */
        RenderDeviceCreationFailureKind kind{RenderDeviceCreationFailureKind::Unknown}; /**< Stable failure category. */
        std::string message;                                                            /**< Actionable bounded backend diagnostic. */
        bool retryable{false}; /**< Whether the same explicit selection may be retried. */

        /** @brief Reports whether identity, category, and diagnostic text are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /**
     * @brief Selects one adapter deterministically from a validated snapshot.
     * @param snapshot Owned discovery facts in canonical identity order.
     * @param request Exact host/product constraints.
     * @return Selected adapter and revision, or a typed validation/unavailable failure.
     */
    [[nodiscard]] Result<RenderAdapterSelection> SelectRenderAdapter(const RenderAdapterSnapshot &snapshot,
                                                                     const RenderAdapterSelectionRequest &request);

    /**
     * @brief Backend discovery port invoked synchronously on the render-capable owner thread.
     *
     * Implementations bound work by request.maxAdapters, perform no device creation, and
     * return owned records. Stop closes admission idempotently; later discovery returns a
     * typed stopped failure. Because discovery is synchronous, it owns no background work
     * and has no implicit cancellation or worker-thread lifetime.
     */
    class IRenderAdapterDiscovery {
    public:
        virtual ~IRenderAdapterDiscovery() = default;

        /** @brief Discovers bounded adapter facts without creating a device or surface. */
        [[nodiscard]] virtual Result<RenderAdapterSnapshot> Discover(const RenderAdapterDiscoveryRequest &request) = 0;

        /** @brief Idempotently closes discovery admission before backend shutdown. */
        virtual void Stop() noexcept = 0;
    };
}  // namespace Horo::Render
