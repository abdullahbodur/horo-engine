#include "Horo/Physics/PhysicsBodyDynamics.h"

#include "Horo/Physics/PhysicsErrors.h"

#include <cmath>
#include <tuple>
#include <type_traits>

namespace Horo::Physics {
    namespace {
        /** @brief Checks one finite vector against a symmetric per-component command limit. */
        [[nodiscard]] bool BoundedVector(const Math::Vec3 value, const float maximumAbsolute) noexcept {
            return Math::IsFinite(value) && std::abs(value.x) <= maximumAbsolute && std::abs(value.y) <= maximumAbsolute &&
                   std::abs(value.z) <= maximumAbsolute;
        }

        /** @brief Checks finite vector magnitude with double intermediates against one speed ceiling. */
        [[nodiscard]] bool BoundedMagnitude(const Math::Vec3 value, const float maximum) noexcept {
            if (!Math::IsFinite(value))
                return false;
            const double squared =
                static_cast<double>(value.x) * value.x + static_cast<double>(value.y) * value.y + static_cast<double>(value.z) * value.z;
            return squared <= static_cast<double>(maximum) * maximum;
        }

        /** @brief Checks a velocity-control mode without interpreting unknown values. */
        [[nodiscard]] bool KnownVelocityMode(const PhysicsVelocityControlMode mode) noexcept {
            return mode == PhysicsVelocityControlMode::Set || mode == PhysicsVelocityControlMode::Add;
        }

        /** @brief Validates one closed dynamics payload and body-specific speed policy. */
        [[nodiscard]] Result<void> ValidatePayload(const PhysicsBodyDynamicsPayload &payload, const PhysicsMotionSafety &motionSafety,
                                                   const float localHalfExtentMeters) {
            return std::visit([&motionSafety, localHalfExtentMeters](const auto &value) -> Result<void> {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, PhysicsLinearForce>) {
                    if (!BoundedVector(value.newtons, MaximumPhysicsForceNewtons) ||
                        (value.applicationPoint.has_value() && !BoundedVector(*value.applicationPoint, localHalfExtentMeters)))
                        return Result<void>::Failure(
                            MakeError(PhysicsErrors::DescriptorInvalid, "Linear force or application point is invalid."));
                } else if constexpr (std::is_same_v<T, PhysicsLinearImpulse>) {
                    if (!BoundedVector(value.newtonSeconds, MaximumPhysicsImpulseNewtonSeconds) ||
                        (value.applicationPoint.has_value() && !BoundedVector(*value.applicationPoint, localHalfExtentMeters)))
                        return Result<void>::Failure(
                            MakeError(PhysicsErrors::DescriptorInvalid, "Linear impulse or application point is invalid."));
                } else if constexpr (std::is_same_v<T, PhysicsTorque>) {
                    if (!BoundedVector(value.newtonMeters, MaximumPhysicsTorqueNewtonMeters))
                        return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Torque is invalid."));
                } else if constexpr (std::is_same_v<T, PhysicsAngularImpulse>) {
                    if (!BoundedVector(value.newtonMeterSeconds, MaximumPhysicsAngularImpulseNewtonMeterSeconds))
                        return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Angular impulse is invalid."));
                } else if constexpr (std::is_same_v<T, PhysicsLinearVelocityControl>) {
                    if (!KnownVelocityMode(value.mode))
                        return Result<void>::Failure(
                            MakeError(PhysicsErrors::OperationUnsupported, "Unknown linear velocity control mode."));
                    if (!BoundedMagnitude(value.metersPerSecond, motionSafety.maximumLinearSpeed))
                        return Result<void>::Failure(
                            MakeError(PhysicsErrors::DescriptorInvalid, "Linear velocity control exceeds its admitted limit."));
                } else if constexpr (std::is_same_v<T, PhysicsAngularVelocityControl>) {
                    if (!KnownVelocityMode(value.mode))
                        return Result<void>::Failure(
                            MakeError(PhysicsErrors::OperationUnsupported, "Unknown angular velocity control mode."));
                    if (!BoundedMagnitude(value.radiansPerSecond, motionSafety.maximumAngularSpeed))
                        return Result<void>::Failure(
                            MakeError(PhysicsErrors::DescriptorInvalid, "Angular velocity control exceeds its admitted limit."));
                } else if constexpr (std::is_same_v<T, PhysicsGravityScaleControl>) {
                    if (!std::isfinite(value.scale) || value.scale < 0.0F || value.scale > MaximumPhysicsGravityScale)
                        return Result<void>::Failure(
                            MakeError(PhysicsErrors::DescriptorInvalid, "Gravity scale must be finite and within profile bounds."));
                }
                return Result<void>::Success();
            }, payload);
        }
    }  // namespace

    /** @copydoc PhysicsBodyDynamicsOrderLess */
    bool PhysicsBodyDynamicsOrderLess(const PhysicsBodyDynamicsCommand &left, const PhysicsBodyDynamicsCommand &right) noexcept {
        return std::tuple{left.protocolVersion, left.simulationTick,  left.body.world.Value(),
                          left.sceneGeneration, left.body.slot.index, left.body.slot.generation,
                          left.source,          left.sourceSequence} <
               std::tuple{right.protocolVersion, right.simulationTick,  right.body.world.Value(),
                          right.sceneGeneration, right.body.slot.index, right.body.slot.generation,
                          right.source,          right.sourceSequence};
    }

    /** @copydoc ValidatePhysicsBodyDynamicsCommand */
    Result<void> ValidatePhysicsBodyDynamicsCommand(const PhysicsBodyDynamicsCommand &command, const PhysicsWorldId expectedWorld,
                                                    const std::uint64_t expectedSceneGeneration, const std::uint64_t expectedSimulationTick,
                                                    const PhysicsMotionType motion, const PhysicsMotionSafety &motionSafety,
                                                    const float localHalfExtentMeters) {
        if (command.protocolVersion != PhysicsBodyDynamicsProtocolVersion || command.simulationTick == 0 || command.sceneGeneration == 0 ||
            command.sourceSequence == 0 || !command.source.IsValid())
            return Result<void>::Failure(
                MakeError(PhysicsErrors::CommandOrderInvalid, "Body dynamics command identity or ordering evidence is incomplete."));
        if (const Result<void> owner = ValidatePhysicsHandleOwner(command.body, expectedWorld); owner.HasError())
            return owner;
        if (command.simulationTick != expectedSimulationTick || command.sceneGeneration != expectedSceneGeneration)
            return Result<void>::Failure(
                MakeError(PhysicsErrors::CommandOrderInvalid, "Body dynamics command targets another fixed-tick admission frame."));
        if (motion != PhysicsMotionType::Dynamic) {
            if (motion == PhysicsMotionType::Static || motion == PhysicsMotionType::Kinematic)
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::OperationUnsupported, "Only dynamic bodies admit force and velocity commands."));
            return Result<void>::Failure(MakeError(PhysicsErrors::OperationUnsupported, "Unknown body motion authority."));
        }
        if (command.wake != PhysicsWakePolicy::PreserveSleeping && command.wake != PhysicsWakePolicy::WakeIfSleeping)
            return Result<void>::Failure(MakeError(PhysicsErrors::OperationUnsupported, "Unknown body wake policy."));
        if (!std::isfinite(motionSafety.maximumLinearSpeed) || motionSafety.maximumLinearSpeed < 0.0F ||
            motionSafety.maximumLinearSpeed > MaximumPhysicsLinearSpeed || !std::isfinite(motionSafety.maximumAngularSpeed) ||
            motionSafety.maximumAngularSpeed < 0.0F || motionSafety.maximumAngularSpeed > MaximumPhysicsAngularSpeed)
            return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Body velocity limits are invalid."));
        if (!std::isfinite(localHalfExtentMeters) || localHalfExtentMeters <= 0.0F ||
            localHalfExtentMeters > MaximumPhysicsLocalHalfExtentMeters)
            return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Local world bounds are invalid."));
        return ValidatePayload(command.payload, motionSafety, localHalfExtentMeters);
    }
}  // namespace Horo::Physics
