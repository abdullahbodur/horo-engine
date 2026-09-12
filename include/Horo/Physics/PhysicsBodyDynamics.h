#pragma once

/** @file PhysicsBodyDynamics.h
 * @brief Fixed-tick rigid-body force, impulse, gravity and velocity command contracts.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"
#include "Horo/Physics/PhysicsBodyDescriptor.h"
#include "Horo/Physics/PhysicsDeterminismPolicy.h"
#include "Horo/Physics/PhysicsWorldSettings.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace Horo::Physics {
    /** @brief Version of the deterministic body-dynamics command contract. */
    inline constexpr std::uint32_t PhysicsBodyDynamicsProtocolVersion = 1;
    /** @brief CanonicalV1 absolute force-component limit in newtons. */
    inline constexpr float MaximumPhysicsForceNewtons = 1.0e9F;
    /** @brief CanonicalV1 absolute impulse-component limit in newton-seconds. */
    inline constexpr float MaximumPhysicsImpulseNewtonSeconds = 1.0e8F;
    /** @brief CanonicalV1 absolute torque-component limit in newton-meters. */
    inline constexpr float MaximumPhysicsTorqueNewtonMeters = 1.0e9F;
    /** @brief CanonicalV1 absolute angular-impulse-component limit in newton-meter-seconds. */
    inline constexpr float MaximumPhysicsAngularImpulseNewtonMeterSeconds = 1.0e8F;
    /** @brief CanonicalV1 maximum per-body gravity multiplier. */
    inline constexpr float MaximumPhysicsGravityScale = 100.0F;

    /** @brief Explicit sleeping-body behavior for one admitted dynamics command. */
    enum class PhysicsWakePolicy : std::uint8_t {
        PreserveSleeping,
        WakeIfSleeping
    };

    /** @brief Whether a velocity command replaces or adds to the current solver value. */
    enum class PhysicsVelocityControlMode : std::uint8_t {
        Set,
        Add
    };

    /** @brief Force integrated by the exact tick; point is absolute in the world's local-origin frame. */
    struct PhysicsLinearForce final {
        Math::Vec3 newtons{};                       /**< Signed world-axis force components. */
        std::optional<Math::Vec3> applicationPoint; /**< Empty selects center of mass. */
    };

    /** @brief Instantaneous impulse; point is absolute in the world's local-origin frame, never body-relative. */
    struct PhysicsLinearImpulse final {
        Math::Vec3 newtonSeconds{};                 /**< Signed world-axis impulse components. */
        std::optional<Math::Vec3> applicationPoint; /**< Empty selects center of mass. */
    };

    /** @brief Torque integrated by the exact owning fixed tick. */
    struct PhysicsTorque final {
        Math::Vec3 newtonMeters{}; /**< Signed world-axis torque components. */
    };

    /** @brief Instantaneous angular impulse. */
    struct PhysicsAngularImpulse final {
        Math::Vec3 newtonMeterSeconds{}; /**< Signed world-axis angular impulse components. */
    };

    /** @brief Set or additive linear velocity control in meters per second. */
    struct PhysicsLinearVelocityControl final {
        Math::Vec3 metersPerSecond{};                                     /**< Replacement or additive velocity. */
        PhysicsVelocityControlMode mode{PhysicsVelocityControlMode::Set}; /**< Explicit mutation semantics. */
    };

    /** @brief Set or additive angular velocity control in radians per second. */
    struct PhysicsAngularVelocityControl final {
        Math::Vec3 radiansPerSecond{};                                    /**< Replacement or additive angular velocity. */
        PhysicsVelocityControlMode mode{PhysicsVelocityControlMode::Set}; /**< Explicit mutation semantics. */
    };

    /** @brief Per-body multiplier applied to the immutable world gravity vector. */
    struct PhysicsGravityScaleControl final {
        float scale{1.0F}; /**< Finite non-negative multiplier; zero disables gravity for this body. */
    };

    /** @brief Closed backend-neutral payload set applied during the fixed-tick dynamic-input phase. */
    using PhysicsBodyDynamicsPayload =
        std::variant<PhysicsLinearForce, PhysicsLinearImpulse, PhysicsTorque, PhysicsAngularImpulse, PhysicsLinearVelocityControl,
                     PhysicsAngularVelocityControl, PhysicsGravityScaleControl>;

    /** @brief Complete deterministic identity and payload for one body-dynamics command. */
    struct PhysicsBodyDynamicsCommand final {
        std::uint32_t protocolVersion{PhysicsBodyDynamicsProtocolVersion}; /**< Exact ordering/payload contract. */
        std::uint64_t simulationTick{};                                    /**< One-based consuming fixed tick. */
        std::uint64_t sceneGeneration{};                                   /**< Exact owning scene generation. */
        BodyHandle body;                                                   /**< Non-owning target in one world generation. */
        PhysicsCommandSourceId source;                                     /**< Stable producer authority. */
        std::uint64_t sourceSequence{};                                    /**< Non-zero source-owned per-tick position. */
        PhysicsWakePolicy wake{PhysicsWakePolicy::WakeIfSleeping};         /**< Explicit sleeping-body policy. */
        PhysicsBodyDynamicsPayload payload;                                /**< Closed typed physical operation. */
    };

    /**
     * @brief Orders dynamics commands independently of producer insertion or worker-completion order.
     * @param left First complete command.
     * @param right Second complete command.
     * @return True when left precedes right by protocol, tick, world, scene, body slot, source and sequence.
     */
    [[nodiscard]] bool PhysicsBodyDynamicsOrderLess(const PhysicsBodyDynamicsCommand &left,
                                                    const PhysicsBodyDynamicsCommand &right) noexcept;

    /**
     * @brief Validates a body-dynamics command against one exact fixed-tick admission frame.
     * @param command Immutable command owned by the caller until queue admission succeeds.
     * @param expectedWorld Active world generation receiving the command.
     * @param expectedSceneGeneration Non-zero scene generation owning the exact tick.
     * @param expectedSimulationTick One-based fixed tick whose dynamic-input phase will consume the command.
     * @param motion Current body motion authority; only Dynamic admits physical inputs.
     * @param motionSafety Validated body-specific velocity ceilings.
     * @param localHalfExtentMeters Positive validated local-world bound for optional application points.
     * @return Success or a stable handle, order, descriptor or unsupported-operation error.
     * @pre The receiving owner separately proves that the body is a live dynamic body and retains bounded queue storage.
     * @post The command is unchanged. Success performs no solver work, wake transition or queue admission.
     */
    [[nodiscard]] Result<void> ValidatePhysicsBodyDynamicsCommand(const PhysicsBodyDynamicsCommand &command, PhysicsWorldId expectedWorld,
                                                                  std::uint64_t expectedSceneGeneration,
                                                                  std::uint64_t expectedSimulationTick, PhysicsMotionType motion,
                                                                  const PhysicsMotionSafety &motionSafety,
                                                                  float localHalfExtentMeters = MaximumPhysicsLocalHalfExtentMeters);
}  // namespace Horo::Physics
