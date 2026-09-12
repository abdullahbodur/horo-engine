#include "Horo/Runtime/Render/PresentMode.h"

#include "Horo/Runtime/Render/PresentModeErrors.h"

#include <algorithm>

namespace Horo::Render {
    namespace {
        /** @brief Reports whether a public mode value belongs to the closed current model. */
        [[nodiscard]] constexpr bool IsKnown(const PresentMode mode) noexcept {
            switch (mode) {
                case PresentMode::Fifo:
                case PresentMode::Immediate:
                    return true;
            }
            return false;
        }

        /** @brief Returns a typed failure when a sequence contains an unknown mode. */
        [[nodiscard]] Result<void> ValidateKnownModes(const std::vector<PresentMode> &modes) {
            if (std::ranges::any_of(modes, [](const PresentMode mode) {
                return !IsKnown(mode);
            }))
                return Result<void>::Failure(MakeError(PresentModeErrors::UnsupportedModeFact));
            return Result<void>::Success();
        }

        /** @brief Validates the shape and uniqueness of required or ordered Auto intent. */
        [[nodiscard]] Result<void> ValidateRequest(const PresentModeRequest &request) {
            using enum PresentModeRequestKind;
            if (request.preferences.empty() || request.preferences.size() > MaximumPresentModeEntries)
                return Result<void>::Failure(MakeError(PresentModeErrors::InvalidRequest));
            if (request.kind != Required && request.kind != Auto)
                return Result<void>::Failure(MakeError(PresentModeErrors::UnsupportedModeFact));
            if (const Result<void> known = ValidateKnownModes(request.preferences); known.HasError())
                return known;
            if (request.kind == Required && request.preferences.size() != 1)
                return Result<void>::Failure(MakeError(PresentModeErrors::InvalidRequest));

            for (std::size_t index = 0; index < request.preferences.size(); ++index) {
                if (std::find(request.preferences.begin(), request.preferences.begin() + index, request.preferences[index]) !=
                    request.preferences.begin() + index)
                    return Result<void>::Failure(MakeError(PresentModeErrors::InvalidRequest));
            }
            return Result<void>::Success();
        }

        /** @brief Validates a non-empty bounded canonical backend capability set. */
        [[nodiscard]] Result<void> ValidateCapabilities(const PresentModeCapabilities &capabilities) {
            if (capabilities.supportedModes.empty() || capabilities.supportedModes.size() > MaximumPresentModeEntries)
                return Result<void>::Failure(MakeError(PresentModeErrors::InvalidCapabilities));
            if (const Result<void> known = ValidateKnownModes(capabilities.supportedModes); known.HasError())
                return known;
            if (!std::ranges::is_sorted(capabilities.supportedModes) ||
                std::ranges::adjacent_find(capabilities.supportedModes) != capabilities.supportedModes.end())
                return Result<void>::Failure(MakeError(PresentModeErrors::InvalidCapabilities));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc NegotiatePresentMode */
    Result<ResolvedPresentMode> NegotiatePresentMode(const PresentModeRequest &request, const PresentModeCapabilities &capabilities) {
        if (const Result<void> valid = ValidateRequest(request); valid.HasError())
            return Result<ResolvedPresentMode>::Failure(valid.ErrorValue());
        if (const Result<void> valid = ValidateCapabilities(capabilities); valid.HasError())
            return Result<ResolvedPresentMode>::Failure(valid.ErrorValue());

        for (std::size_t index = 0; index < request.preferences.size(); ++index) {
            const PresentMode candidate = request.preferences[index];
            if (!std::ranges::binary_search(capabilities.supportedModes, candidate))
                continue;
            const PresentModeResolution resolution = index == 0 ? PresentModeResolution::Exact : PresentModeResolution::DegradedFallback;
            return Result<ResolvedPresentMode>::Success({request.preferences.front(), candidate, resolution, index});
        }

        const ErrorCodeDescriptor &failure = request.kind == PresentModeRequestKind::Required ? PresentModeErrors::RequiredModeUnavailable
                                                                                              : PresentModeErrors::NoPreferredModeAvailable;
        return Result<ResolvedPresentMode>::Failure(MakeError(failure));
    }
}  // namespace Horo::Render
