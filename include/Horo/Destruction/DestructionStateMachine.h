#pragma once

/**
 * @file DestructionStateMachine.h
 * @brief Revision-fenced destruction state transitions and detached completion candidates.
 */

#include "Horo/Destruction/DestructibleDescriptor.h"

#include <compare>
#include <cstdint>
#include <optional>

namespace Horo::Destruction {
    /** @brief Closed semantic lifecycle for the foundational destructible health state. */
    enum class DestructionStatePhase : std::uint8_t {
        Intact,
        Damaged,
        Destroyed,
    };

    /** @brief Exact immutable state visible from one destruction-owner publication. */
    struct DestructionStateSnapshot final {
        DestructionHandle target{};                                 /**< Exact world and destructible generation. */
        FractureArtifactContentIdentity content{};                  /**< Exact immutable fracture content. */
        DestructionConfigurationRevision configurationRevision{};   /**< Exact admitted descriptor publication. */
        DestructionFeatureSet effectiveFeatures{};                  /**< Explicitly resolved feature set; never a fallback tier. */
        DestructionStateRevision revision{};                        /**< Monotonic revision within target.generation. */
        DestructionStatePhase phase{DestructionStatePhase::Intact}; /**< Semantic state; not runtime representation lifecycle. */
        float health{};                                             /**< Finite canonical health in [0, maximumHealth]. */

        [[nodiscard]] constexpr auto operator<=>(const DestructionStateSnapshot &) const noexcept = default;
    };

    /** @brief Bounded command kinds supported by the foundational state machine. */
    enum class DestructionStateCommandKind : std::uint8_t {
        ApplyDamage,
        Destroy,
    };

    /** @brief Validated command for one exact target generation and expected semantic revision. */
    class DestructionStateCommand final {
    public:
        /**
         * @brief Creates a finite positive damage command.
         * @param id Owner-issued idempotency identity bound to the exact target generation. Values for newly
         * accepted commands must increase monotonically; exact retry uses the same complete command.
         * @param expectedRevision Exact semantic revision from which the command may transition.
         * @param damage Finite positive canonical health units.
         * @return Validated command, IdentityInvalid, or InvalidDamage.
         */
        [[nodiscard]] static Result<DestructionStateCommand> Damage(DestructionCommandId id, DestructionStateRevision expectedRevision,
                                                                    float damage);

        /**
         * @brief Creates an explicit terminal destruction command.
         * @param id Owner-issued idempotency identity bound to the exact target generation. Values for newly
         * accepted commands must increase monotonically; exact retry uses the same complete command.
         * @param expectedRevision Exact semantic revision from which the command may transition.
         * @return Validated command or IdentityInvalid.
         */
        [[nodiscard]] static Result<DestructionStateCommand> Destroy(DestructionCommandId id, DestructionStateRevision expectedRevision);

        /** @brief Returns the exact idempotency identity. @return Immutable command identity. */
        [[nodiscard]] constexpr const DestructionCommandId &Id() const noexcept {
            return id_;
        }

        /** @brief Returns the expected source revision. @return Non-zero semantic revision. */
        [[nodiscard]] constexpr DestructionStateRevision ExpectedRevision() const noexcept {
            return expectedRevision_;
        }

        /** @brief Returns the command kind. @return ApplyDamage or Destroy. */
        [[nodiscard]] constexpr DestructionStateCommandKind Kind() const noexcept {
            return kind_;
        }

        /** @brief Returns canonical damage. @return Positive value for ApplyDamage and zero for Destroy. */
        [[nodiscard]] constexpr float DamageAmount() const noexcept {
            return damage_;
        }

        [[nodiscard]] constexpr auto operator<=>(const DestructionStateCommand &) const noexcept = default;

    private:
        DestructionStateCommand(DestructionCommandId id, DestructionStateRevision expectedRevision, DestructionStateCommandKind kind,
                                float damage) noexcept;

        DestructionCommandId id_{};
        DestructionStateRevision expectedRevision_{};
        DestructionStateCommandKind kind_{DestructionStateCommandKind::ApplyDamage};
        float damage_{};
    };

    /** @brief Detached candidate status; cancellation never mutates the active state. */
    enum class DestructionTransitionStatus : std::uint8_t {
        Prepared,
        Duplicate,
        Cancelled,
    };

    /** @brief Immutable completion candidate fenced to one exact source snapshot. */
    class DestructionStateTransition final {
    public:
        /** @brief Returns the normalized source command. @return Immutable command value. */
        [[nodiscard]] constexpr const DestructionStateCommand &Command() const noexcept {
            return command_;
        }

        /** @brief Returns the exact source snapshot. @return Snapshot against which work was prepared. */
        [[nodiscard]] constexpr const DestructionStateSnapshot &Source() const noexcept {
            return source_;
        }

        /** @brief Returns the proposed successor snapshot. @return Unpublished immutable successor. */
        [[nodiscard]] constexpr const DestructionStateSnapshot &Successor() const noexcept {
            return successor_;
        }

        /** @brief Returns preparation disposition. @return Prepared, Duplicate, or Cancelled. */
        [[nodiscard]] constexpr DestructionTransitionStatus Status() const noexcept {
            return status_;
        }

        /**
         * @brief Cancels detached work without changing either snapshot.
         * @return A cancelled candidate; repeated cancellation is idempotent.
         */
        [[nodiscard]] DestructionStateTransition Cancel() const noexcept;

    private:
        friend class DestructionStateMachine;

        DestructionStateTransition(DestructionStateCommand command, DestructionStateSnapshot source, DestructionStateSnapshot successor,
                                   DestructionTransitionStatus status) noexcept;

        DestructionStateCommand command_;
        DestructionStateSnapshot source_{};
        DestructionStateSnapshot successor_{};
        DestructionTransitionStatus status_{DestructionTransitionStatus::Prepared};
    };

    /**
     * @brief Immutable owner-safe destruction state and compare-by-generation/revision commit contract.
     * @details Preparation is thread-compatible and allocation-free. One DestructionRuntime owner serializes
     * Commit at its safe point and publishes the returned successor. The value owns no jobs, native handles,
     * callbacks, or global registration.
     */
    class DestructionStateMachine final {
    public:
        /**
         * @brief Creates an active Intact state from one admitted immutable descriptor.
         * @param descriptor Validated descriptor whose policy/content remain captured by value.
         * @param target Exact current world and destructible generation.
         * @param initialRevision Non-zero first semantic revision for this generation.
         * @return State machine or a typed invalid identity/state failure.
         */
        [[nodiscard]] static Result<DestructionStateMachine> Create(const DestructibleDescriptor &descriptor, DestructionHandle target,
                                                                    DestructionStateRevision initialRevision);

        /** @brief Returns the current immutable published state. @return Borrowed state snapshot. */
        [[nodiscard]] constexpr const DestructionStateSnapshot &Snapshot() const noexcept {
            return snapshot_;
        }

        /** @brief Reports command admission state. @return False after BeginShutdown. */
        [[nodiscard]] constexpr bool IsAdmissionOpen() const noexcept {
            return admissionOpen_;
        }

        /**
         * @brief Prepares a detached successor without mutating the active value.
         * @param command Validated command targeting this exact generation and revision.
         * @return Prepared/duplicate candidate or a typed stale, duplicate-conflict, terminal, shutdown, or revision failure.
         * @post Failure and cancellation leave this state unchanged.
         */
        [[nodiscard]] Result<DestructionStateTransition> Prepare(const DestructionStateCommand &command) const;

        /**
         * @brief Commits a detached candidate with exact generation/revision compare semantics.
         * @param transition Candidate returned by Prepare, possibly completed asynchronously.
         * @return Successor state, idempotent current state for an exact duplicate, or typed stale/cancelled failure.
         * @post At most one distinct candidate from any source revision can advance the active owner.
         */
        [[nodiscard]] Result<DestructionStateMachine> Commit(const DestructionStateTransition &transition) const;

        /**
         * @brief Replaces the runtime generation and descriptor as a fresh Intact publication.
         * @param replacement Exact next generation for the same world and authored destructible.
         * @param descriptor Validated replacement policy/content for the same authored destructible.
         * @return Fresh generation at revision one, or a typed owner/generation/shutdown failure.
         * @post Every candidate from the previous generation becomes stale.
         */
        [[nodiscard]] Result<DestructionStateMachine> Replace(DestructionHandle replacement,
                                                              const DestructibleDescriptor &descriptor) const;

        /**
         * @brief Idempotently closes command admission without discarding published state.
         * @return A shutdown-fenced value; already closed values are returned unchanged.
         */
        [[nodiscard]] DestructionStateMachine BeginShutdown() const noexcept;

    private:
        DestructionStateMachine(DestructionStateSnapshot snapshot, DestructionHealthPolicy healthPolicy,
                                std::optional<DestructionStateCommand> lastCommand, bool admissionOpen) noexcept;

        DestructionStateSnapshot snapshot_{};
        DestructionHealthPolicy healthPolicy_{};
        std::optional<DestructionStateCommand> lastCommand_;
        bool admissionOpen_{true};
    };
}  // namespace Horo::Destruction
