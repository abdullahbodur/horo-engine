#include "Horo/WorldStreaming/StreamingDesiredState.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] constexpr bool IsKnownResidency(const StreamingDesiredResidency residency) noexcept {
            return residency >= StreamingDesiredResidency::Unloaded && residency <= StreamingDesiredResidency::Activated;
        }

        [[nodiscard]] constexpr bool IsKnownRetention(const StreamingRetention retention) noexcept {
            return retention >= StreamingRetention::Releasable && retention <= StreamingRetention::Pinned;
        }
    }  // namespace

    /** @copydoc StreamingSourceDesiredState::Create */
    Result<StreamingSourceDesiredState> StreamingSourceDesiredState::Create(StreamingSourceDescriptor source,
                                                                            const StreamingDesiredResidency residency,
                                                                            const StreamingRetention retention) {
        if (const auto valid = ValidateStreamingSourceDescriptor(source); valid.HasError())
            return Result<StreamingSourceDesiredState>::Failure(valid.ErrorValue());
        if (!IsKnownResidency(residency) || !IsKnownRetention(retention))
            return Failure<StreamingSourceDesiredState>(WorldStreamingErrors::SourceDesiredStateUnsupported);
        if (residency == StreamingDesiredResidency::Unloaded && retention == StreamingRetention::Pinned)
            return Failure<StreamingSourceDesiredState>(WorldStreamingErrors::SourceDesiredStateInvalid);
        return Result<StreamingSourceDesiredState>::Success(StreamingSourceDesiredState{source, residency, retention});
    }

    /** @copydoc ValidateStreamingSourceDesiredStateAdmission */
    Result<StreamingSourceAdmissionKind> ValidateStreamingSourceDesiredStateAdmission(const StreamingSourceDesiredState &desiredState,
                                                                                      const StreamingSourceAdmissionContext &context) {
        return ValidateStreamingSourceAdmission(desiredState.Source(), context);
    }
}  // namespace Horo::WorldStreaming
