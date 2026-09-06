#include "Horo/WorldStreaming/StreamingSourceDescriptor.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] constexpr bool IsSupportedIntent(const StreamingSourceIntent intent) noexcept {
            return intent >= StreamingSourceIntent::Camera && intent <= StreamingSourceIntent::Preload;
        }

        [[nodiscard]] Result<void> ValidateAdmissionContext(const StreamingSourceAdmissionContext &context) {
            if (!context.expectedOwner.IsValid() || context.ownerState > StreamingSourceOwnerState::Closed ||
                (context.currentRevision.has_value() && !context.currentRevision->IsValid()) ||
                context.activeSourceCount > context.sourceCapacity)
                return Failure<void>(WorldStreamingErrors::SourceDescriptorInvalid);
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc StreamingSourcePriority::Create */
    Result<StreamingSourcePriority> StreamingSourcePriority::Create(const float value) {
        const StreamingSourcePriority priority{value};
        if (!priority.IsValid())
            return Failure<StreamingSourcePriority>(WorldStreamingErrors::SourceDescriptorInvalid);
        return Result<StreamingSourcePriority>::Success(priority);
    }

    /** @copydoc StreamingSourceOwnerToken::IsValid */
    bool StreamingSourceOwnerToken::IsValid() const noexcept {
        return partition.IsValid() && epoch.IsValid() && slot != InvalidSlot && generation != 0;
    }

    /** @copydoc StreamingSourceDescriptor::IsValid */
    bool StreamingSourceDescriptor::IsValid() const noexcept {
        return id.IsValid() && owner.IsValid() && priority.IsValid() && revision.IsValid();
    }

    /** @copydoc ValidateStreamingSourceDescriptor */
    Result<void> ValidateStreamingSourceDescriptor(const StreamingSourceDescriptor &descriptor) {
        if (!descriptor.IsValid())
            return Failure<void>(WorldStreamingErrors::SourceDescriptorInvalid);
        if (!IsSupportedIntent(descriptor.intent))
            return Failure<void>(WorldStreamingErrors::SourceIntentUnsupported);
        return Result<void>::Success();
    }

    /** @copydoc ValidateStreamingSourceAdmission */
    Result<StreamingSourceAdmissionKind> ValidateStreamingSourceAdmission(const StreamingSourceDescriptor &descriptor,
                                                                          const StreamingSourceAdmissionContext &context) {
        if (const auto valid = ValidateStreamingSourceDescriptor(descriptor); valid.HasError())
            return Result<StreamingSourceAdmissionKind>::Failure(valid.ErrorValue());
        if (const auto valid = ValidateAdmissionContext(context); valid.HasError())
            return Result<StreamingSourceAdmissionKind>::Failure(valid.ErrorValue());
        if (descriptor.owner != context.expectedOwner)
            return Failure<StreamingSourceAdmissionKind>(WorldStreamingErrors::SourceOwnerStale);
        if (context.ownerState != StreamingSourceOwnerState::Active)
            return Failure<StreamingSourceAdmissionKind>(WorldStreamingErrors::SourceLifecycleUnavailable);
        if (context.currentRevision.has_value()) {
            if (descriptor.revision <= *context.currentRevision)
                return Failure<StreamingSourceAdmissionKind>(WorldStreamingErrors::SourceRevisionStale);
            return Result<StreamingSourceAdmissionKind>::Success(StreamingSourceAdmissionKind::Replace);
        }
        if (context.activeSourceCount == context.sourceCapacity)
            return Failure<StreamingSourceAdmissionKind>(WorldStreamingErrors::SourceCapacityExceeded);
        return Result<StreamingSourceAdmissionKind>::Success(StreamingSourceAdmissionKind::Insert);
    }
}  // namespace Horo::WorldStreaming
