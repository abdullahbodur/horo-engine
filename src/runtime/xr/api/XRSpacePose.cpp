#include "Horo/XR/XRSpacePose.h"

#include "Horo/XR/XRErrors.h"

#include <cmath>
#include <utility>

namespace Horo::XR {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Reject(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool ValidSpace(const XRCoordinateSpace &space, const XRSessionId &activeSession,
                                      const XRWorldOriginRevision activeOriginRevision, Error &failure) {
            auto identity = ValidateXRSessionObject(space.id, activeSession);
            if (identity.HasError()) {
                failure = identity.ErrorValue();
                return false;
            }
            if (space.kind >= XRSpaceKind::Count || space.convention >= XRCoordinateConvention::Count) {
                failure = MakeError(XRErrors::OperationInvalid);
                return false;
            }
            if (space.convention != XRCoordinateConvention::RightHandedYUpNegativeZForward) {
                failure = MakeError(XRErrors::OperationUnsupported);
                return false;
            }
            if (!space.worldOriginRevision.IsValid() || !activeOriginRevision.IsValid()) {
                failure = MakeError(XRErrors::OperationInvalid);
                return false;
            }
            if (space.worldOriginRevision != activeOriginRevision) {
                failure = MakeError(XRErrors::OriginRevisionStale);
                return false;
            }
            return true;
        }

        template <typename Value, typename IsValid>
        [[nodiscard]] bool ValidComponent(const XRPoseComponent<Value> &component, IsValid &&isValid) {
            if (component.validity >= XRPoseComponentValidity::Count)
                return false;
            if (component.validity == XRPoseComponentValidity::Invalid)
                return !component.value.has_value();
            return component.value.has_value() && isValid(*component.value);
        }

        [[nodiscard]] bool UnitQuaternion(const Math::Quaternion value) noexcept {
            if (!Math::IsFinite(value))
                return false;
            const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
            return std::abs(lengthSquared - 1.0F) <= 0.0001F;
        }

        [[nodiscard]] bool ValidTime(const XRPoseDescriptor &descriptor) noexcept {
            if (!descriptor.time.runtimeSample.IsValid())
                return false;
            using enum XRPosePurpose;
            switch (descriptor.purpose) {
                case SimulationInput:
                    return descriptor.time.simulation.has_value() && descriptor.time.simulation->IsValid() &&
                           !descriptor.time.renderPrediction.has_value();
                case PresentationPrediction:
                    return !descriptor.time.simulation.has_value() && descriptor.time.renderPrediction.has_value() &&
                           descriptor.time.renderPrediction->IsValid();
                case Count:
                    return false;
            }
            return false;
        }

        [[nodiscard]] bool ValidLoss(const XRPoseComponents &components) noexcept {
            const bool positionPresent = components.positionMeters.validity != XRPoseComponentValidity::Invalid;
            const bool orientationPresent = components.orientation.validity != XRPoseComponentValidity::Invalid;
            const bool linearVelocityPresent = components.linearVelocityMetersPerSecond.validity != XRPoseComponentValidity::Invalid;
            const bool angularVelocityPresent = components.angularVelocityRadiansPerSecond.validity != XRPoseComponentValidity::Invalid;
            using enum XRTrackingLossState;
            switch (components.loss) {
                case None:
                    return positionPresent && orientationPresent && components.confidence != XRTrackingConfidence::None;
                case PositionLost:
                    return !positionPresent && !linearVelocityPresent && orientationPresent &&
                           components.confidence != XRTrackingConfidence::None;
                case OrientationLost:
                    return positionPresent && !orientationPresent && !angularVelocityPresent &&
                           components.confidence != XRTrackingConfidence::None;
                case FullyLost:
                    return !positionPresent && !orientationPresent && !linearVelocityPresent && !angularVelocityPresent &&
                           components.confidence == XRTrackingConfidence::None;
                case Count:
                    return false;
            }
            return false;
        }

        [[nodiscard]] bool ValidComponents(const XRPoseComponents &components) {
            if (components.confidence >= XRTrackingConfidence::Count)
                return false;
            const auto finiteVector = [](const Math::Vec3 value) {
                return Math::IsFinite(value);
            };
            return ValidComponent(components.positionMeters, finiteVector) && ValidComponent(components.orientation, UnitQuaternion) &&
                   ValidComponent(components.linearVelocityMetersPerSecond, finiteVector) &&
                   ValidComponent(components.angularVelocityRadiansPerSecond, finiteVector) && ValidLoss(components);
        }

        [[nodiscard]] XRPoseComponentValidity CombinedValidity(const XRPoseComponentValidity first,
                                                               const XRPoseComponentValidity second) noexcept {
            if (first == XRPoseComponentValidity::Invalid || second == XRPoseComponentValidity::Invalid)
                return XRPoseComponentValidity::Invalid;
            return first == XRPoseComponentValidity::Tracked && second == XRPoseComponentValidity::Tracked
                       ? XRPoseComponentValidity::Tracked
                       : XRPoseComponentValidity::Inferred;
        }

        [[nodiscard]] XRTrackingConfidence CombinedConfidence(const XRTrackingConfidence first,
                                                              const XRTrackingConfidence second) noexcept {
            if (first == XRTrackingConfidence::None || second == XRTrackingConfidence::None)
                return XRTrackingConfidence::None;
            return first == XRTrackingConfidence::Low || second == XRTrackingConfidence::Low ? XRTrackingConfidence::Low
                                                                                             : XRTrackingConfidence::High;
        }

        [[nodiscard]] XRTrackingLossState CombinedLoss(const XRTrackingLossState first, const XRTrackingLossState second) noexcept {
            if (first == XRTrackingLossState::FullyLost || second == XRTrackingLossState::FullyLost)
                return XRTrackingLossState::FullyLost;
            if (first == XRTrackingLossState::None)
                return second;
            if (second == XRTrackingLossState::None || first == second)
                return first;
            return XRTrackingLossState::FullyLost;
        }

        [[nodiscard]] XRPoseComponent<Math::Vec3> ComposeVector(const XRPoseComponent<Math::Vec3> &first,
                                                                const XRPoseComponent<Math::Vec3> &second,
                                                                const XRPoseComponent<Math::Quaternion> &rotation) {
            const auto validity = CombinedValidity(CombinedValidity(first.validity, second.validity), rotation.validity);
            if (validity == XRPoseComponentValidity::Invalid)
                return {};
            return {.value = rotation.value->Rotate(*first.value) + *second.value, .validity = validity};
        }

        [[nodiscard]] XRPoseComponent<Math::Vec3> ComposeLinearVelocity(const XRPoseComponents &first, const XRPoseComponents &second) {
            auto validity = CombinedValidity(first.linearVelocityMetersPerSecond.validity, second.linearVelocityMetersPerSecond.validity);
            validity = CombinedValidity(validity, second.orientation.validity);
            validity = CombinedValidity(validity, second.angularVelocityRadiansPerSecond.validity);
            validity = CombinedValidity(validity, first.positionMeters.validity);
            if (validity == XRPoseComponentValidity::Invalid)
                return {};

            const Math::Vec3 rotatedPosition = second.orientation.value->Rotate(*first.positionMeters.value);
            const Math::Vec3 referenceMotion = Math::Cross(*second.angularVelocityRadiansPerSecond.value, rotatedPosition);
            return {.value = second.orientation.value->Rotate(*first.linearVelocityMetersPerSecond.value) +
                             *second.linearVelocityMetersPerSecond.value + referenceMotion,
                    .validity = validity};
        }

        [[nodiscard]] XRPoseComponents ComposeComponents(const XRPoseComponents &first, const XRPoseComponents &second) {
            XRPoseComponents composed;
            composed.positionMeters = ComposeVector(first.positionMeters, second.positionMeters, second.orientation);
            const auto orientationValidity = CombinedValidity(first.orientation.validity, second.orientation.validity);
            if (orientationValidity != XRPoseComponentValidity::Invalid) {
                composed.orientation = {.value = *second.orientation.value * *first.orientation.value, .validity = orientationValidity};
            }
            composed.linearVelocityMetersPerSecond = ComposeLinearVelocity(first, second);
            composed.angularVelocityRadiansPerSecond =
                ComposeVector(first.angularVelocityRadiansPerSecond, second.angularVelocityRadiansPerSecond, second.orientation);
            composed.confidence = CombinedConfidence(first.confidence, second.confidence);
            composed.loss = CombinedLoss(first.loss, second.loss);
            return composed;
        }

        [[nodiscard]] Result<XRPoseSample> ComposePair(const XRPoseSample &first, const XRPoseSample &second,
                                                       const XRSessionId &activeSession, const XRWorldOriginRevision activeOriginRevision) {
            if (first.Target() != second.Source())
                return Reject<XRPoseSample>(XRErrors::CoordinateSpaceIncompatible);
            if (first.Purpose() != second.Purpose() || first.Time() != second.Time())
                return Reject<XRPoseSample>(XRErrors::TimeDomainIncompatible);
            return XRPoseSample::Create({.session = first.Session(),
                                         .source = first.Source(),
                                         .target = second.Target(),
                                         .purpose = first.Purpose(),
                                         .time = first.Time(),
                                         .components = ComposeComponents(first.Components(), second.Components())},
                                        activeSession, activeOriginRevision);
        }
    }  // namespace

    /** @copydoc XRPoseSample::Create */
    Result<XRPoseSample> XRPoseSample::Create(const XRPoseDescriptor &descriptor, const XRSessionId &activeSession,
                                              const XRWorldOriginRevision activeOriginRevision) {
        auto session = ValidateXRSession(descriptor.session, activeSession);
        if (session.HasError())
            return Result<XRPoseSample>::Failure(session.ErrorValue());
        Error spaceFailure;
        if (!ValidSpace(descriptor.source, activeSession, activeOriginRevision, spaceFailure) ||
            !ValidSpace(descriptor.target, activeSession, activeOriginRevision, spaceFailure)) {
            return Result<XRPoseSample>::Failure(std::move(spaceFailure));
        }
        if (descriptor.source.id == descriptor.target.id && descriptor.source != descriptor.target)
            return Reject<XRPoseSample>(XRErrors::CoordinateSpaceIncompatible);
        if (!ValidTime(descriptor))
            return Reject<XRPoseSample>(XRErrors::TimeDomainIncompatible);
        if (!ValidComponents(descriptor.components))
            return Reject<XRPoseSample>(XRErrors::PoseInvalid);
        return Result<XRPoseSample>::Success(XRPoseSample{descriptor});
    }

    XRPoseSample::XRPoseSample(const XRPoseDescriptor &descriptor) noexcept
        : session_(descriptor.session), source_(descriptor.source), target_(descriptor.target), purpose_(descriptor.purpose),
          time_(descriptor.time), components_(descriptor.components) {}

    /** @copydoc XRPoseSample::Session */
    const XRSessionId &XRPoseSample::Session() const noexcept {
        return session_;
    }

    /** @copydoc XRPoseSample::Source */
    const XRCoordinateSpace &XRPoseSample::Source() const noexcept {
        return source_;
    }

    /** @copydoc XRPoseSample::Target */
    const XRCoordinateSpace &XRPoseSample::Target() const noexcept {
        return target_;
    }

    /** @copydoc XRPoseSample::Purpose */
    XRPosePurpose XRPoseSample::Purpose() const noexcept {
        return purpose_;
    }

    /** @copydoc XRPoseSample::Time */
    const XRTimeEvidence &XRPoseSample::Time() const noexcept {
        return time_;
    }

    /** @copydoc XRPoseSample::Components */
    const XRPoseComponents &XRPoseSample::Components() const noexcept {
        return components_;
    }

    /** @copydoc ComposeXRSpaceTransforms */
    Result<XRPoseSample> ComposeXRSpaceTransforms(const std::span<const XRPoseSample> transforms, const XRSessionId &activeSession,
                                                  const XRWorldOriginRevision activeOriginRevision) {
        if (transforms.empty())
            return Reject<XRPoseSample>(XRErrors::OperationInvalid);
        if (transforms.size() > XRCoordinateHardLimits::MaximumTransformHops)
            return Reject<XRPoseSample>(XRErrors::CapacityExceeded);

        XRPoseSample composed = transforms.front();
        for (const auto &transform : transforms) {
            auto source = ValidateXRSessionObject(transform.Source().id, activeSession);
            auto target = ValidateXRSessionObject(transform.Target().id, activeSession);
            if (source.HasError())
                return Result<XRPoseSample>::Failure(source.ErrorValue());
            if (target.HasError())
                return Result<XRPoseSample>::Failure(target.ErrorValue());
            if (transform.Source().worldOriginRevision != activeOriginRevision ||
                transform.Target().worldOriginRevision != activeOriginRevision) {
                return Reject<XRPoseSample>(XRErrors::OriginRevisionStale);
            }
        }
        for (std::size_t index = 1; index < transforms.size(); ++index) {
            auto next = ComposePair(composed, transforms[index], activeSession, activeOriginRevision);
            if (next.HasError())
                return next;
            composed = std::move(next).Value();
        }
        return Result<XRPoseSample>::Success(std::move(composed));
    }
}  // namespace Horo::XR
