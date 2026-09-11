#include "Horo/WorldStreaming/WorldObjectOwnership.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] bool IsKnown(const WorldObjectOwnershipClass value) noexcept {
            using enum WorldObjectOwnershipClass;
            switch (value) {
                case AuthoredAlwaysPresent:
                case AuthoredSpatial:
                case RuntimeSpawned:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const WorldObjectOwnerKind value) noexcept {
            using enum WorldObjectOwnerKind;
            switch (value) {
                case World:
                case Cell:
                case Runtime:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const WorldObjectCellExitPolicy value) noexcept {
            using enum WorldObjectCellExitPolicy;
            switch (value) {
                case NotApplicable:
                case Retire:
                case RequireHandoff:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const WorldObjectOwnershipOwnerState value) noexcept {
            using enum WorldObjectOwnershipOwnerState;
            switch (value) {
                case Active:
                case Cancelling:
                case Closed:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool HasAuthoredIdentity(const WorldObjectOwnershipDescriptor &descriptor) noexcept {
            return descriptor.authored.IsValid() && !descriptor.runtimeSpawned.IsValid();
        }

        [[nodiscard]] bool HasRuntimeIdentity(const WorldObjectOwnershipDescriptor &descriptor) noexcept {
            return !descriptor.authored.IsValid() && descriptor.runtimeSpawned.IsValid();
        }

        [[nodiscard]] bool HasCoherentClassPolicy(const WorldObjectOwnershipDescriptor &descriptor) noexcept {
            using enum WorldObjectOwnershipClass;
            if (descriptor.owner.kind == WorldObjectOwnerKind::Cell) {
                if (descriptor.cellExitPolicy == WorldObjectCellExitPolicy::NotApplicable)
                    return false;
            } else if (descriptor.cellExitPolicy != WorldObjectCellExitPolicy::NotApplicable) {
                return false;
            }

            switch (descriptor.objectClass) {
                case AuthoredAlwaysPresent:
                    return descriptor.owner.kind == WorldObjectOwnerKind::World && HasAuthoredIdentity(descriptor);
                case AuthoredSpatial:
                    return descriptor.owner.kind == WorldObjectOwnerKind::Cell &&
                           descriptor.cellExitPolicy == WorldObjectCellExitPolicy::Retire && HasAuthoredIdentity(descriptor);
                case RuntimeSpawned:
                    return HasRuntimeIdentity(descriptor);
            }
            return false;
        }

        [[nodiscard]] bool SameIdentity(const WorldObjectOwnershipDescriptor &left, const WorldObjectOwnershipDescriptor &right) noexcept {
            if (left.objectClass != right.objectClass)
                return false;
            if (left.objectClass == WorldObjectOwnershipClass::RuntimeSpawned)
                return left.runtimeSpawned == right.runtimeSpawned;
            return left.authored == right.authored;
        }

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Internal::Failure<T>(descriptor);
        }

        [[nodiscard]] Result<void> ValidateContext(const WorldObjectOwnershipAdmissionContext &context) {
            if (!context.expectedWorld.IsValid() || !IsKnown(context.state) || context.objectCapacity == 0 ||
                context.objectCount > context.objectCapacity)
                return Failure<void>(WorldStreamingErrors::ObjectOwnershipInvalid);
            if (!context.current.has_value())
                return Result<void>::Success();
            if (const auto valid = ValidateWorldObjectOwnershipDescriptor(*context.current); valid.HasError())
                return valid;
            if (context.current->owner.world != context.expectedWorld)
                return Failure<void>(WorldStreamingErrors::ObjectOwnershipOwnerStale);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<WorldObjectOwnershipAdmissionKind> ValidateInsert(const WorldObjectOwnershipRequest &request,
                                                                               const WorldObjectOwnershipAdmissionContext &context) {
            if (request.expectedRevision.has_value())
                return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipRevisionStale);
            if (context.objectCount == context.objectCapacity)
                return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipCapacityExceeded);
            return Result<WorldObjectOwnershipAdmissionKind>::Success(WorldObjectOwnershipAdmissionKind::Insert);
        }

        [[nodiscard]] Result<WorldObjectOwnershipAdmissionKind> ValidateReplacement(const WorldObjectOwnershipRequest &request,
                                                                                    const WorldObjectOwnershipDescriptor &current) {
            if (!SameIdentity(current, request.candidate))
                return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipIdentityConflict);
            if (!request.expectedRevision.has_value() || *request.expectedRevision != current.revision)
                return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipRevisionStale);
            const auto next = NextWorldObjectOwnershipRevision(current.revision);
            if (next.HasError())
                return Result<WorldObjectOwnershipAdmissionKind>::Failure(next.ErrorValue());
            if (request.candidate.revision != next.Value())
                return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipRevisionStale);

            const bool ownerChanged = current.owner != request.candidate.owner;
            if (ownerChanged && current.objectClass != WorldObjectOwnershipClass::RuntimeSpawned)
                return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipUnsupported);

            const bool isHandoff = ownerChanged;
            if (isHandoff && current.owner.kind == WorldObjectOwnerKind::Cell &&
                current.cellExitPolicy != WorldObjectCellExitPolicy::RequireHandoff)
                return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipUnsupported);
            return Result<WorldObjectOwnershipAdmissionKind>::Success(isHandoff ? WorldObjectOwnershipAdmissionKind::Handoff
                                                                                : WorldObjectOwnershipAdmissionKind::Replace);
        }
    }  // namespace

    /** @copydoc WorldObjectOwnerBinding::IsValid */
    bool WorldObjectOwnerBinding::IsValid() const noexcept {
        using enum WorldObjectOwnerKind;
        if (!world.IsValid() || !IsKnown(kind))
            return false;
        switch (kind) {
            case World:
                return !cell.IsValid() && !runtimeOwner.IsValid() && !runtimeGeneration.IsValid();
            case Cell:
                return cell.IsValid() && cell.partition == world.partition && cell.epoch == world.epoch && !runtimeOwner.IsValid() &&
                       !runtimeGeneration.IsValid();
            case Runtime:
                return !cell.IsValid() && runtimeOwner.IsValid() && runtimeGeneration.IsValid();
        }
        return false;
    }

    /** @copydoc WorldObjectOwnershipDescriptor::IsValid */
    bool WorldObjectOwnershipDescriptor::IsValid() const noexcept {
        return IsKnown(objectClass) && IsKnown(cellExitPolicy) && revision.IsValid() && owner.IsValid() && HasCoherentClassPolicy(*this);
    }

    /** @copydoc ValidateWorldObjectOwnershipDescriptor */
    Result<void> ValidateWorldObjectOwnershipDescriptor(const WorldObjectOwnershipDescriptor &descriptor) {
        if (!IsKnown(descriptor.objectClass) || !IsKnown(descriptor.owner.kind) || !IsKnown(descriptor.cellExitPolicy))
            return Failure<void>(WorldStreamingErrors::ObjectOwnershipUnsupported);
        if (!descriptor.revision.IsValid() || !descriptor.owner.IsValid() ||
            (!HasAuthoredIdentity(descriptor) && !HasRuntimeIdentity(descriptor)))
            return Failure<void>(WorldStreamingErrors::ObjectOwnershipInvalid);
        if (!HasCoherentClassPolicy(descriptor))
            return Failure<void>(WorldStreamingErrors::ObjectOwnershipUnsupported);
        return Result<void>::Success();
    }

    /** @copydoc ValidateWorldObjectOwnershipAdmission */
    Result<WorldObjectOwnershipAdmissionKind> ValidateWorldObjectOwnershipAdmission(const WorldObjectOwnershipRequest &request,
                                                                                    const WorldObjectOwnershipAdmissionContext &context) {
        if (const auto valid = ValidateWorldObjectOwnershipDescriptor(request.candidate); valid.HasError())
            return Result<WorldObjectOwnershipAdmissionKind>::Failure(valid.ErrorValue());
        if (request.expectedRevision.has_value() && !request.expectedRevision->IsValid())
            return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipInvalid);
        if (const auto valid = ValidateContext(context); valid.HasError())
            return Result<WorldObjectOwnershipAdmissionKind>::Failure(valid.ErrorValue());
        if (request.candidate.owner.world != context.expectedWorld)
            return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipOwnerStale);
        if (context.state != WorldObjectOwnershipOwnerState::Active)
            return Failure<WorldObjectOwnershipAdmissionKind>(WorldStreamingErrors::ObjectOwnershipLifecycleUnavailable);

        if (!context.current.has_value())
            return ValidateInsert(request, context);
        return ValidateReplacement(request, *context.current);
    }

    /** @copydoc NextWorldObjectOwnershipRevision */
    Result<WorldObjectOwnershipRevision> NextWorldObjectOwnershipRevision(const WorldObjectOwnershipRevision current) {
        if (!current.IsValid())
            return Failure<WorldObjectOwnershipRevision>(WorldStreamingErrors::IdentityInvalid);
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Failure<WorldObjectOwnershipRevision>(WorldStreamingErrors::GenerationExhausted);
        return WorldObjectOwnershipRevision::Create(current.Value() + 1);
    }
}  // namespace Horo::WorldStreaming
