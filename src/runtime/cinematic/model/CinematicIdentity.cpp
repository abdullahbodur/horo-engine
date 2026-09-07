#include "Horo/Cinematic/CinematicIdentity.h"

namespace Horo::Cinematic {
    namespace {
        enum class CompositionOffset : std::uint64_t {
            Sequence,
            Track,
            Keyframe,
            Binding,
            Count
        };

        template <typename Tag>
        CinematicIdentity<Tag> ComposedIdentity(const std::uint64_t firstStableValue, const CompositionOffset offset,
                                                const std::uint32_t generation) noexcept {
            return {firstStableValue + static_cast<std::uint64_t>(offset), generation};
        }
    }  // namespace

    /** @copydoc MakeDeterministicCinematicIdentityComposition */
    Result<CinematicIdentityComposition> MakeDeterministicCinematicIdentityComposition(const std::uint64_t firstStableValue,
                                                                                       const std::uint32_t generation) {
        using enum CompositionOffset;
        if (constexpr auto IdentityCount = static_cast<std::uint64_t>(Count);
            firstStableValue == 0 || generation == 0 || firstStableValue > std::numeric_limits<std::uint64_t>::max() - (IdentityCount - 1U))
            return Result<CinematicIdentityComposition>::Failure(MakeError(CinematicErrors::IdentityInvalid));

        return Result<CinematicIdentityComposition>::Success({
            .sequence = ComposedIdentity<SequenceIdentityTag>(firstStableValue, Sequence, generation),
            .track = ComposedIdentity<TrackIdentityTag>(firstStableValue, Track, generation),
            .keyframe = ComposedIdentity<KeyframeIdentityTag>(firstStableValue, Keyframe, generation),
            .binding = ComposedIdentity<PropertyBindingIdentityTag>(firstStableValue, Binding, generation),
        });
    }
}  // namespace Horo::Cinematic
