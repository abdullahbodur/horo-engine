#pragma once

/**
 * @file SaveTestCompositions.h
 * @brief Null and deterministic in-memory save composition primitives.
 */

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Save/SaveNamespace.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Horo::Runtime {
    namespace SaveCompositionDetail {
        struct DeterministicMockState;
    }
    /** @brief Persistence support exposed by an explicitly selected save composition. */
    enum class SaveCompositionCapability : std::uint8_t {
        Unsupported,
        DeterministicMemory
    };

    /** @brief Operations understood by the deterministic composition primitive. */
    enum class SaveCompositionOperationKind : std::uint8_t {
        Store,
        Load,
        Remove
    };

    /** @brief Observable lifecycle of one deterministic operation. */
    enum class SaveCompositionOperationState : std::uint8_t {
        Queued,
        Waiting,
        Completed,
        Failed,
        Cancelled
    };

    /** @brief Whether an operation made its in-memory publication visible. */
    enum class SaveCompositionCommitOutcome : std::uint8_t {
        NotCommitted,
        Committed
    };

    /** @brief Deliberate fault selected by a test before operation admission. */
    enum class SaveCompositionFault : std::uint8_t {
        None,
        InjectedFailure
    };

    /** @brief Non-zero process-local identity allocated by one deterministic composition. */
    struct SaveCompositionOperationId final {
        std::uint64_t value{}; /**< Zero is reserved for an invalid identity. */

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const SaveCompositionOperationId &) const noexcept = default;
    };

    /** @brief Logical in-memory object address with no path or platform identity. */
    struct SaveCompositionAddress final {
        CanonicalSaveNamespaceKey nameSpace; /**< Canonical typed namespace key. */
        SaveGameSlotId slot;                 /**< Stable logical slot identity. */

        [[nodiscard]] constexpr auto operator<=>(const SaveCompositionAddress &) const noexcept = default;
    };

    /** @brief Explicit finite bounds for one deterministic composition. */
    struct DeterministicSaveCompositionLimits final {
        std::size_t maximumOperations{};     /**< Maximum retained queued and terminal operations. */
        std::size_t maximumStoredObjects{};  /**< Maximum distinct committed addresses. */
        std::size_t maximumBytesPerObject{}; /**< Maximum bytes copied by one store operation. */
    };

    /** @brief Owned inert request copied at deterministic operation admission. */
    struct SaveCompositionRequest final {
        SaveCompositionOperationKind kind{SaveCompositionOperationKind::Store}; /**< Requested pure storage action. */
        SaveCompositionAddress address;                                         /**< Typed logical object address. */
        std::vector<std::byte> bytes;                                           /**< Store payload; empty for load/remove. */
        std::uint32_t delaySteps{};                                             /**< Explicit scheduler steps before evaluation. */
        SaveCompositionFault fault{SaveCompositionFault::None};                 /**< Optional deterministic pre-commit failure. */
        CancellationToken cancellation;                                         /**< Cooperative cancellation observed before commit. */
    };

    /** @brief Immutable owned projection of one admitted operation. */
    struct SaveCompositionOperationSnapshot final {
        SaveCompositionOperationId operation;                                            /**< Composition-local operation identity. */
        SaveCompositionOperationKind kind{SaveCompositionOperationKind::Store};          /**< Admitted operation kind. */
        SaveCompositionOperationState state{SaveCompositionOperationState::Queued};      /**< Current or terminal state. */
        SaveCompositionCommitOutcome commit{SaveCompositionCommitOutcome::NotCommitted}; /**< Publication evidence. */
        std::optional<Error> error;                                                      /**< Stable terminal failure, when present. */
        std::vector<std::byte> bytes; /**< Owned load result; empty for other operations. */
    };

    /**
     * @brief Inert unsupported composition for products or hosts without save persistence.
     *
     * The object owns no storage and never accesses a filesystem, cloud provider, user directory,
     * service locator, or ambient registry.
     */
    class NullSaveComposition final {
    public:
        /** @brief Reports explicit persistence absence. @return Unsupported. */
        [[nodiscard]] constexpr SaveCompositionCapability Capability() const noexcept {
            return SaveCompositionCapability::Unsupported;
        }

        /** @brief Rejects every operation without retaining its request. @return SaveErrors::CompositionUnsupported. */
        [[nodiscard]] Result<SaveCompositionOperationId> Submit(SaveCompositionRequest request) const;

        /** @brief Performs no work. @return False. */
        [[nodiscard]] constexpr bool AdvanceOne() const noexcept {
            return false;
        }

        /** @brief Finds no operation because admission never succeeds. @return Empty. */
        [[nodiscard]] std::optional<SaveCompositionOperationSnapshot> Snapshot(SaveCompositionOperationId operation) const noexcept;

        /** @brief Reports that no object can be persisted. @return Zero. */
        [[nodiscard]] constexpr std::size_t StoredObjectCount() const noexcept {
            return 0;
        }
    };

    /**
     * @brief Caller-driven bounded in-memory save composition for deterministic tests.
     *
     * Admission only copies inert values. `AdvanceOne` is the sole execution authority and
     * processes the oldest non-terminal operation. The class performs no filesystem, cloud,
     * platform, job-system, clock, registry, or lifecycle work. It is not thread-safe; its
     * owning test or headless composition serializes access.
     */
    class DeterministicMockSaveComposition final {
    public:
        /** @brief Releases all queued operations and in-memory objects. */
        ~DeterministicMockSaveComposition();
        /** @brief Transfers exclusive deterministic state ownership. */
        DeterministicMockSaveComposition(DeterministicMockSaveComposition &&) noexcept;
        /** @brief Replaces this object's deterministic state with exclusive moved ownership. */
        DeterministicMockSaveComposition &operator=(DeterministicMockSaveComposition &&) noexcept;
        DeterministicMockSaveComposition(const DeterministicMockSaveComposition &) = delete;
        DeterministicMockSaveComposition &operator=(const DeterministicMockSaveComposition &) = delete;

        /** @brief Reports deterministic memory support. @return DeterministicMemory. */
        [[nodiscard]] constexpr SaveCompositionCapability Capability() const noexcept {
            return SaveCompositionCapability::DeterministicMemory;
        }

        /** @brief Validates and copies a request into the bounded FIFO. @param request Owned request.
         * @return Non-zero identity, or a stable invalid/capacity error. No operation executes during admission.
         */
        [[nodiscard]] Result<SaveCompositionOperationId> Submit(SaveCompositionRequest request);
        /** @brief Advances exactly one scheduler step for the oldest non-terminal operation.
         * @return True when an operation consumed a wait or evaluation step; false when idle.
         */
        [[nodiscard]] bool AdvanceOne();
        /** @brief Copies the current immutable operation projection. @param operation Identity to inspect.
         * @return Owned snapshot, or empty for an unknown identity.
         */
        [[nodiscard]] std::optional<SaveCompositionOperationSnapshot> Snapshot(SaveCompositionOperationId operation) const;
        /** @brief Copies committed bytes for an address. @param address Logical address.
         * @return Owned bytes, or empty when no object is committed at the address.
         */
        [[nodiscard]] std::optional<std::vector<std::byte>> ObjectSnapshot(const SaveCompositionAddress &address) const;
        /** @brief Reports committed object count. @return Count no greater than the configured bound. */
        [[nodiscard]] std::size_t StoredObjectCount() const noexcept;

    private:
        explicit DeterministicMockSaveComposition(std::unique_ptr<SaveCompositionDetail::DeterministicMockState> state) noexcept;
        friend Result<std::unique_ptr<DeterministicMockSaveComposition>> CreateDeterministicMockSaveComposition(
            DeterministicSaveCompositionLimits limits);

        std::unique_ptr<SaveCompositionDetail::DeterministicMockState> state_;
    };

    /** @brief Creates an empty deterministic composition after validating all finite positive bounds.
     * @param limits Explicit operation, object, and payload capacities.
     * @return Owned composition or SaveErrors::CompositionConfigurationInvalid/allocation failure.
     */
    [[nodiscard]] Result<std::unique_ptr<DeterministicMockSaveComposition>> CreateDeterministicMockSaveComposition(
        DeterministicSaveCompositionLimits limits);
}  // namespace Horo::Runtime
