#pragma once

/**
 * @file DestructionCommand.h
 * @brief Bounded damage/fracture commands and durable typed terminal results.
 */

#include "Horo/Destruction/DestructionStateMachine.h"
#include "Horo/Math/SceneMath.h"

#include <compare>
#include <cstdint>

namespace Horo::Destruction {
    /** @brief Version of the portable damage/fracture command contract. */
    inline constexpr std::uint32_t CurrentDestructionCommandContractVersion = 1;

    struct DestructionAuthorityIdentityTag;
    struct DestructionAuthorityRevisionTag;
    struct DestructionCapabilityRevisionTag;

    /** @brief Stable identity of the gameplay or replication authority issuing commands. */
    using DestructionAuthorityId = DestructionStableIdentity<DestructionAuthorityIdentityTag>;
    /** @brief Non-wrapping publication revision of one authority grant. */
    using DestructionAuthorityRevision = DestructionStableIdentity<DestructionAuthorityRevisionTag>;
    /** @brief Non-wrapping publication revision of an immutable capability snapshot. */
    using DestructionCapabilityRevision = DestructionStableIdentity<DestructionCapabilityRevisionTag>;

    /** @brief Closed authority capabilities; values are backend-neutral and independently granted. */
    enum class DestructionCommandCapability : std::uint8_t {
        Damage,
        Impact,
        Explosion,
        Collision,
        Script,
        ExplicitFracture,
        Count,
    };

    /** @brief Fixed-width set of command capabilities copied into an immutable authority grant. */
    struct DestructionCommandCapabilitySet final {
        std::uint32_t bits{}; /**< One bit per known DestructionCommandCapability value. */

        /** @brief Tests one known capability. @param capability Capability to query. @return True when granted. */
        [[nodiscard]] constexpr bool Contains(const DestructionCommandCapability capability) const noexcept {
            const auto index = static_cast<std::uint8_t>(capability);
            return index < static_cast<std::uint8_t>(DestructionCommandCapability::Count) && (bits & (std::uint32_t{1} << index)) != 0U;
        }

        [[nodiscard]] constexpr auto operator<=>(const DestructionCommandCapabilitySet &) const noexcept = default;
    };

    /** @brief Creates the fixed bit for one command capability. */
    template <DestructionCommandCapability Capability>
    inline constexpr std::uint32_t DestructionCommandCapabilityBit = std::uint32_t{1} << static_cast<std::uint8_t>(Capability);

    /** @brief Immutable proof of who may issue which command kinds. */
    struct DestructionAuthorityGrant final {
        DestructionAuthorityId authority{};             /**< Stable product authority identity. */
        DestructionAuthorityRevision revision{};        /**< Exact grant publication revision. */
        DestructionCommandCapabilitySet capabilities{}; /**< Explicitly granted command kinds. */

        /** @brief Checks representation and known capability bits. @return True for a usable immutable grant. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const DestructionAuthorityGrant &) const noexcept = default;
    };

    /** @brief Absolute portable ceilings that no product command profile may widen. */
    struct DestructionCommandHardLimits final {
        static constexpr float Damage = 1000000.0F;        /**< Absolute canonical damage ceiling. */
        static constexpr float Impulse = 1000000.0F;       /**< Absolute canonical impulse ceiling. */
        static constexpr float ExplosionRadius = 10000.0F; /**< Absolute scene-space radius ceiling. */
    };

    /** @brief Finite product limits applied before a command enters a queue or starts work. */
    struct DestructionCommandLimits final {
        float maximumDamage{DestructionCommandHardLimits::Damage};                   /**< Maximum canonical health units in one command. */
        float maximumImpulse{DestructionCommandHardLimits::Impulse};                 /**< Maximum canonical impulse magnitude. */
        float maximumExplosionRadius{DestructionCommandHardLimits::ExplosionRadius}; /**< Maximum radius in scene units. */

        /** @brief Validates finite positive ceilings. @return True when all ceilings are usable. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const DestructionCommandLimits &) const noexcept = default;
    };

    /** @brief Closed typed command kinds accepted by the portable boundary. */
    enum class DestructionCommandKind : std::uint8_t {
        Impact,
        Explosion,
        Collision,
        Damage,
        Script,
    };

    /** @brief Explicit script intent; scripts receive no authority beyond their grant. */
    enum class DestructionScriptIntent : std::uint8_t {
        ApplyDamage,
        Fracture,
    };

    /** @brief Supported deterministic radial attenuation choices. */
    enum class DestructionExplosionFalloff : std::uint8_t {
        Constant,
        Linear,
    };

    /** @brief Common command fence and authority evidence. */
    struct DestructionCommandHeader final {
        std::uint32_t contractVersion{CurrentDestructionCommandContractVersion}; /**< Portable schema version. */
        DestructionCommandId id{};                                               /**< Exact target generation and idempotency value. */
        DestructionStateRevision expectedRevision{};                             /**< Exact source semantic revision. */
        DestructionCapabilityRevision capabilityRevision{};                      /**< Exact immutable capability publication. */
        std::uint64_t eligibleSimulationTick{};                                  /**< Non-zero earliest fixed tick for admission. */
        DestructionAuthorityGrant authority{};                                   /**< Exact issuer grant copied by value. */

        [[nodiscard]] constexpr auto operator<=>(const DestructionCommandHeader &) const noexcept = default;
    };

    /** @brief Fixed-size normalized command payload; fields unused by the selected kind remain zero. */
    struct DestructionCommandPayload final {
        Math::Vec3 position{};  /**< Horo scene-space impact point or explosion center. */
        Math::Vec3 direction{}; /**< Horo scene-space impact direction or collision normal. */
        float damage{};         /**< Positive canonical health units. */
        float impulse{};        /**< Positive canonical impulse magnitude when applicable. */
        float radius{};         /**< Positive explosion radius only for Explosion. */
        DestructionExplosionFalloff falloff{DestructionExplosionFalloff::Constant}; /**< Explosion attenuation policy. */
        DestructionScriptIntent scriptIntent{DestructionScriptIntent::ApplyDamage}; /**< Script-only typed intent. */

        [[nodiscard]] constexpr auto operator<=>(const DestructionCommandPayload &) const noexcept = default;
    };

    /** @brief Immutable validated command with no borrowed memory or native handle. */
    class DestructionCommand final {
    public:
        /** @brief Creates an impact command. @param header Common generation/revision/authority fence.
         * @param position Finite scene-space impact position. @param direction Finite non-zero direction.
         * @param impulse Finite positive impulse. @param damage Finite positive health damage.
         * @return Command or a typed malformed/bounded-input failure.
         */
        [[nodiscard]] static Result<DestructionCommand> Impact(DestructionCommandHeader header, Math::Vec3 position, Math::Vec3 direction,
                                                               float impulse, float damage);
        /** @brief Creates an explosion command. @param header Common command fence. @param center Finite scene-space center.
         * @param radius Finite positive radius. @param damage Finite positive peak damage. @param falloff Typed attenuation.
         * @return Command or a typed malformed/bounded-input failure.
         */
        [[nodiscard]] static Result<DestructionCommand> Explosion(DestructionCommandHeader header, Math::Vec3 center, float radius,
                                                                  float damage, DestructionExplosionFalloff falloff);
        /** @brief Creates normalized post-step collision evidence as a command. @param header Common command fence.
         * @param position Finite contact point. @param normal Finite non-zero canonical normal.
         * @param impulse Finite positive impulse. @param damage Finite positive policy-resolved damage.
         * @return Command or a typed malformed/bounded-input failure.
         */
        [[nodiscard]] static Result<DestructionCommand> Collision(DestructionCommandHeader header, Math::Vec3 position, Math::Vec3 normal,
                                                                  float impulse, float damage);
        /** @brief Creates direct admitted damage. @param header Common command fence. @param damage Finite positive health damage.
         * @return Command or a typed malformed/bounded-input failure.
         */
        [[nodiscard]] static Result<DestructionCommand> Damage(DestructionCommandHeader header, float damage);
        /** @brief Creates a script-originated typed intent. @param header Common command fence. @param intent Explicit script intent.
         * @param damage Positive damage for ApplyDamage and zero for Fracture.
         * @return Command or a typed malformed/bounded-input failure.
         */
        [[nodiscard]] static Result<DestructionCommand> Script(DestructionCommandHeader header, DestructionScriptIntent intent,
                                                               float damage = 0.0F);

        /** @brief Returns the exact common fence. @return Borrowed immutable header. */
        [[nodiscard]] const DestructionCommandHeader &Header() const noexcept;
        /** @brief Returns the closed command kind. @return Typed command kind. */
        [[nodiscard]] DestructionCommandKind Kind() const noexcept;
        /** @brief Returns the fixed-size normalized payload. @return Borrowed immutable payload. */
        [[nodiscard]] const DestructionCommandPayload &Payload() const noexcept;
        /** @brief Returns the kind or underlying script-intent capability required by this command. @return Required capability bit. */
        [[nodiscard]] DestructionCommandCapability RequiredCapability() const noexcept;

        [[nodiscard]] constexpr auto operator<=>(const DestructionCommand &) const noexcept = default;

    private:
        DestructionCommand(DestructionCommandHeader header, DestructionCommandKind kind, DestructionCommandPayload payload) noexcept;
        /** @brief Constructs a command after validating its common header. */
        [[nodiscard]] static Result<DestructionCommand> Create(DestructionCommandHeader header, DestructionCommandKind kind,
                                                               DestructionCommandPayload payload);
        DestructionCommandHeader header_{};
        DestructionCommandKind kind_{DestructionCommandKind::Damage};
        DestructionCommandPayload payload_{};
    };

    /** @brief Durable terminal disposition; consumers never infer it from error text. */
    enum class DestructionCommandOutcome : std::uint8_t {
        Succeeded,
        Rejected,
        Cancelled,
        Unsupported,
        Failed,
    };

    /** @brief Typed terminal reason retained with the command identity and revision evidence. */
    enum class DestructionCommandTerminalReason : std::uint8_t {
        None,
        InvalidState,
        AuthorityDenied,
        LimitExceeded,
        CapabilityUnavailable,
        StaleGeneration,
        StaleRevision,
        Replaced,
        Shutdown,
        Rollback,
        ExecutionFailure,
    };

    /** @brief Immutable durable terminal result independent of presentation strings and native providers. */
    class DestructionCommandResult final {
    public:
        /** @brief Creates a committed terminal success. @param command Exact command identity.
         * @param sourceRevision Revision from which work was prepared. @param committedRevision Published successor revision.
         * @return Durable result or ResultInvalid.
         */
        [[nodiscard]] static Result<DestructionCommandResult> Succeeded(DestructionCommandId command,
                                                                        DestructionStateRevision sourceRevision,
                                                                        DestructionStateRevision committedRevision);
        /** @brief Creates a typed non-success terminal result. @param command Exact command identity.
         * @param sourceRevision Revision from which work was requested. @param observedRevision Revision observed at termination.
         * @param outcome Rejected, Cancelled, Unsupported, or Failed. @param reason Outcome-compatible typed reason.
         * @return Durable result or ResultInvalid.
         */
        [[nodiscard]] static Result<DestructionCommandResult> Terminated(DestructionCommandId command,
                                                                         DestructionStateRevision sourceRevision,
                                                                         DestructionStateRevision observedRevision,
                                                                         DestructionCommandOutcome outcome,
                                                                         DestructionCommandTerminalReason reason);

        /** @brief Returns exact command identity. @return Immutable generation-fenced identity. */
        [[nodiscard]] constexpr const DestructionCommandId &Command() const noexcept {
            return command_;
        }

        /** @brief Returns the source revision. @return Non-zero source semantic revision. */
        [[nodiscard]] constexpr DestructionStateRevision SourceRevision() const noexcept {
            return sourceRevision_;
        }

        /** @brief Returns the observed or committed terminal revision. @return Non-zero revision. */
        [[nodiscard]] constexpr DestructionStateRevision TerminalRevision() const noexcept {
            return terminalRevision_;
        }

        /** @brief Returns terminal disposition. @return One closed outcome value. */
        [[nodiscard]] constexpr DestructionCommandOutcome Outcome() const noexcept {
            return outcome_;
        }

        /** @brief Returns typed terminal reason. @return None only for success. */
        [[nodiscard]] constexpr DestructionCommandTerminalReason Reason() const noexcept {
            return reason_;
        }

        /** @brief Reports whether private candidate state must be discarded. @return True for every non-success outcome. */
        [[nodiscard]] constexpr bool RequiresRollback() const noexcept {
            return outcome_ != DestructionCommandOutcome::Succeeded;
        }

        [[nodiscard]] constexpr auto operator<=>(const DestructionCommandResult &) const noexcept = default;

    private:
        DestructionCommandResult(DestructionCommandId command, DestructionStateRevision sourceRevision,
                                 DestructionStateRevision terminalRevision, DestructionCommandOutcome outcome,
                                 DestructionCommandTerminalReason reason) noexcept;

        DestructionCommandId command_{};
        DestructionStateRevision sourceRevision_{};
        DestructionStateRevision terminalRevision_{};
        DestructionCommandOutcome outcome_{DestructionCommandOutcome::Failed};
        DestructionCommandTerminalReason reason_{DestructionCommandTerminalReason::ExecutionFailure};
    };

    /**
     * @brief Validates one command against exact current state, descriptor, authority and limits before mutation.
     * @param command Immutable candidate command.
     * @param snapshot Current published destruction snapshot.
     * @param descriptor Current immutable descriptor.
     * @param capabilityRevision Current immutable capability publication revision.
     * @param currentAuthority Current immutable grant for the submitting authority identity.
     * @param limits Finite per-command input ceilings.
     * @param admissionOpen False after shutdown begins.
     * @return Success or a stable typed rejection/unsupported/stale/shutdown error.
     * @post Performs fixed work, allocates nothing and mutates no owner state.
     */
    [[nodiscard]] Result<void> AdmitDestructionCommand(const DestructionCommand &command, const DestructionStateSnapshot &snapshot,
                                                       const DestructibleDescriptor &descriptor,
                                                       DestructionCapabilityRevision capabilityRevision,
                                                       const DestructionAuthorityGrant &currentAuthority,
                                                       const DestructionCommandLimits &limits, bool admissionOpen);

    /**
     * @brief Converts an admitted portable command to the foundational revisioned state command.
     * @param command Command already accepted by AdmitDestructionCommand.
     * @return Damage or explicit-destruction state command without losing identity/revision evidence.
     */
    [[nodiscard]] Result<DestructionStateCommand> ToDestructionStateCommand(const DestructionCommand &command);

    /**
     * @brief Revalidates a successful asynchronous completion before constructing its durable result.
     * @param command Exact completed command.
     * @param sourceRevision Revision used to prepare the completion.
     * @param committedRevision Proposed published successor revision.
     * @param currentTarget Current target identity at the owner safe point.
     * @param currentRevision Current semantic revision at the owner safe point.
     * @param admissionOpen False after shutdown begins.
     * @return Success result or typed stale/replaced/shutdown terminal result; malformed inputs return ResultInvalid.
     * @post Stale and shutdown completions require rollback and never report success.
     */
    [[nodiscard]] Result<DestructionCommandResult> ResolveDestructionCommandCompletion(
        const DestructionCommand &command, DestructionStateRevision sourceRevision, DestructionStateRevision committedRevision,
        DestructionHandle currentTarget, DestructionStateRevision currentRevision, bool admissionOpen);
}  // namespace Horo::Destruction
