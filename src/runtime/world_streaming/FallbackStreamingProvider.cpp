#include "Horo/WorldStreaming/FallbackStreamingProvider.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

namespace Horo::WorldStreaming {
    namespace {
        /** @brief Validate one complete candidate before changing any live fallback state. */
        Result<void> Validate(const FallbackStreamingProviderDescriptor &descriptor) {
            using enum FallbackStreamingProviderMode;
            if (!descriptor.owner.IsValid() || !descriptor.revision.IsValid())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderInvalid));
            if (descriptor.mode != SingleCell && descriptor.mode != Null)
                return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderUnsupported));
            if (descriptor.mode == Null)
                return descriptor.singleCell.has_value() ? Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderInvalid))
                                                         : Result<void>::Success();
            if (!descriptor.singleCell.has_value() || !descriptor.singleCell->IsValid())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderInvalid));
            if (descriptor.maximumPublishedCells < 1)
                return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderCapacityExceeded));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc FallbackStreamingProvider::FallbackStreamingProvider */
    FallbackStreamingProvider::FallbackStreamingProvider(FallbackStreamingProvider &&other) noexcept
        : descriptor_(other.descriptor_), cell_(other.cell_), state_(other.state_) {
        other.descriptor_ = {};
        other.cell_ = {};
        other.state_ = FallbackStreamingProviderState::Closed;
    }

    /** @copydoc FallbackStreamingProvider::Create */
    Result<FallbackStreamingProvider> FallbackStreamingProvider::Create(const FallbackStreamingProviderDescriptor &descriptor) {
        if (const auto validation = Validate(descriptor); validation.HasError())
            return Result<FallbackStreamingProvider>::Failure(validation.ErrorValue());
        return Result<FallbackStreamingProvider>::Success(FallbackStreamingProvider{descriptor});
    }

    /** @copydoc FallbackStreamingProvider::DesiredCells */
    std::span<const StreamingCellId> FallbackStreamingProvider::DesiredCells() const noexcept {
        const bool publishesCell =
            state_ == FallbackStreamingProviderState::Active && descriptor_.mode == FallbackStreamingProviderMode::SingleCell;
        return std::span(cell_).first(static_cast<std::size_t>(publishesCell));
    }

    /** @copydoc FallbackStreamingProvider::Replace */
    Result<void> FallbackStreamingProvider::Replace(const FallbackStreamingProviderDescriptor &descriptor) {
        if (state_ != FallbackStreamingProviderState::Active)
            return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderLifecycleUnavailable));
        if (descriptor.owner != descriptor_.owner || descriptor.revision <= descriptor_.revision)
            return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderStale));
        if (const auto validation = Validate(descriptor); validation.HasError())
            return validation;
        descriptor_ = descriptor;
        cell_.front() = descriptor.singleCell.value_or(StreamingCellId{});
        return Result<void>::Success();
    }

    /** @copydoc FallbackStreamingProvider::RequestCancellation */
    Result<void> FallbackStreamingProvider::RequestCancellation(const StreamingSourceOwnerToken &owner,
                                                                const StreamingSourceRevision revision) noexcept {
        if (owner != descriptor_.owner || revision != descriptor_.revision)
            return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderStale));
        if (state_ == FallbackStreamingProviderState::Closed)
            return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderLifecycleUnavailable));
        state_ = FallbackStreamingProviderState::Cancelling;
        return Result<void>::Success();
    }

    /** @copydoc FallbackStreamingProvider::Shutdown */
    Result<void> FallbackStreamingProvider::Shutdown(const StreamingSourceOwnerToken &owner) noexcept {
        if (owner != descriptor_.owner)
            return Result<void>::Failure(MakeError(WorldStreamingErrors::FallbackProviderStale));
        state_ = FallbackStreamingProviderState::Closed;
        return Result<void>::Success();
    }
}  // namespace Horo::WorldStreaming
