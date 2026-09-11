#include "Horo/WorldStreaming/WorldSpatialObjectDescriptor.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] constexpr bool IsSupported(const WorldSpatialObjectPlacementClass placement) noexcept {
            return placement >= WorldSpatialObjectPlacementClass::Spatial && placement <= WorldSpatialObjectPlacementClass::AlwaysPresent;
        }

        [[nodiscard]] constexpr bool IsKnown(const WorldSpatialObjectOwnerState state) noexcept {
            return state >= WorldSpatialObjectOwnerState::Active && state <= WorldSpatialObjectOwnerState::Closed;
        }

        [[nodiscard]] bool BoundsAreOrdered(const WorldPartitionBounds &bounds) noexcept {
            const auto minimum = bounds.minimum.Millimeters();
            const auto maximum = bounds.maximum.Millimeters();
            for (std::size_t axis = 0; axis < minimum.size(); ++axis) {
                if (minimum[axis] > maximum[axis])
                    return false;
            }
            return true;
        }

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Internal::Failure<T>(descriptor);
        }

        [[nodiscard]] Result<void> ValidateContext(const WorldSpatialObjectAdmissionContext &context) {
            if (!IsKnown(context.ownerState) || context.objectCapacity == 0 || context.objectCount > context.objectCapacity)
                return Failure<void>(WorldStreamingErrors::SpatialObjectDescriptorInvalid);
            if (context.currentDescriptor.has_value()) {
                if (const auto valid = ValidateWorldSpatialObjectDescriptor(*context.currentDescriptor); valid.HasError())
                    return valid;
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<WorldSpatialObjectAdmissionKind> ValidateInsert(const WorldSpatialObjectRequest &request,
                                                                             const WorldSpatialObjectAdmissionContext &context) {
            if (request.expectedRevision.has_value())
                return Failure<WorldSpatialObjectAdmissionKind>(WorldStreamingErrors::SpatialObjectRevisionStale);
            if (context.objectCount == context.objectCapacity)
                return Failure<WorldSpatialObjectAdmissionKind>(WorldStreamingErrors::SpatialObjectCapacityExceeded);
            return Result<WorldSpatialObjectAdmissionKind>::Success(WorldSpatialObjectAdmissionKind::Insert);
        }

        [[nodiscard]] Result<WorldSpatialObjectAdmissionKind> ValidateReplacement(const WorldSpatialObjectRequest &request,
                                                                                  const WorldSpatialObjectDescriptor &current) {
            if (request.candidate.address != current.address)
                return Failure<WorldSpatialObjectAdmissionKind>(WorldStreamingErrors::SpatialObjectIdentityConflict);
            if (!request.expectedRevision.has_value() || *request.expectedRevision != current.revision)
                return Failure<WorldSpatialObjectAdmissionKind>(WorldStreamingErrors::SpatialObjectRevisionStale);
            const auto next = NextWorldAuthoringRevision(current.revision);
            if (next.HasError())
                return Result<WorldSpatialObjectAdmissionKind>::Failure(next.ErrorValue());
            if (request.candidate.revision != next.Value())
                return Failure<WorldSpatialObjectAdmissionKind>(WorldStreamingErrors::SpatialObjectRevisionStale);
            return Result<WorldSpatialObjectAdmissionKind>::Success(WorldSpatialObjectAdmissionKind::Replace);
        }
    }  // namespace

    /** @copydoc WorldSpatialObjectDescriptor::IsValid */
    bool WorldSpatialObjectDescriptor::IsValid() const noexcept {
        return version.major != 0 && address.IsValid() && revision.IsValid() && sourceAsset.IsValid() && BoundsAreOrdered(bounds);
    }

    /** @copydoc ValidateWorldSpatialObjectDescriptor */
    Result<void> ValidateWorldSpatialObjectDescriptor(const WorldSpatialObjectDescriptor &descriptor) {
        if (!descriptor.IsValid())
            return Failure<void>(WorldStreamingErrors::SpatialObjectDescriptorInvalid);
        if (descriptor.version.major != WorldSpatialObjectSchemaVersion::CurrentMajor ||
            descriptor.version.minor > WorldSpatialObjectSchemaVersion::CurrentMinor)
            return Failure<void>(WorldStreamingErrors::SpatialObjectVersionUnsupported);
        if (!IsSupported(descriptor.placement))
            return Failure<void>(WorldStreamingErrors::SpatialObjectPlacementUnsupported);
        return Result<void>::Success();
    }

    /** @copydoc ValidateWorldSpatialObjectAdmission */
    Result<WorldSpatialObjectAdmissionKind> ValidateWorldSpatialObjectAdmission(const WorldSpatialObjectRequest &request,
                                                                                const WorldSpatialObjectAdmissionContext &context) {
        if (const auto valid = ValidateWorldSpatialObjectDescriptor(request.candidate); valid.HasError())
            return Result<WorldSpatialObjectAdmissionKind>::Failure(valid.ErrorValue());
        if (request.expectedRevision.has_value() && !request.expectedRevision->IsValid())
            return Failure<WorldSpatialObjectAdmissionKind>(WorldStreamingErrors::SpatialObjectDescriptorInvalid);
        if (const auto valid = ValidateContext(context); valid.HasError())
            return Result<WorldSpatialObjectAdmissionKind>::Failure(valid.ErrorValue());
        if (context.ownerState != WorldSpatialObjectOwnerState::Active)
            return Failure<WorldSpatialObjectAdmissionKind>(WorldStreamingErrors::SpatialObjectLifecycleUnavailable);
        if (!context.currentDescriptor.has_value())
            return ValidateInsert(request, context);
        return ValidateReplacement(request, *context.currentDescriptor);
    }
}  // namespace Horo::WorldStreaming
