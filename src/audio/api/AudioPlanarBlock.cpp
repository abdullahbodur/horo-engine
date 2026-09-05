#include "Horo/Audio/AudioPlanarBlock.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>

namespace Horo::Audio {
    namespace {  // NOSONAR(cpp:S1000) - File-local validation helpers intentionally have internal linkage.

        /** @brief Requires every independently evaluated invariant to hold. */
        template <std::size_t Count> bool AllTrue(const std::array<bool, Count> &invariants) noexcept {
            return std::ranges::all_of(invariants, std::identity{});
        }

        /** @brief Retained block metadata must match the complete admitted channel semantics. */
        bool LayoutMatches(const AudioChannelLayoutView &view, const AudioChannelLayout &expected) noexcept {
            return AllTrue(std::array{view.kind == expected.kind, view.ambisonic == expected.ambisonic,
                                      std::ranges::equal(view.orderedChannels, expected.orderedChannels)});
        }

        /** @brief Checks address arithmetic before comparing ranges; never dereferences sample storage. */
        bool ValidPlanes(const AudioPlanarBlockView &block) noexcept {
            const auto bytes = static_cast<std::uintptr_t>(block.capacityFrames) * sizeof(AudioSample);
            for (std::size_t index = 0; index < block.planes.size(); ++index) {
                const auto start = reinterpret_cast<std::uintptr_t>(block.planes[index]);
                if (!AllTrue(std::array{start != 0, start % 64 == 0, start <= std::numeric_limits<std::uintptr_t>::max() - bytes}))
                    return false;
                for (std::size_t previous = 0; previous < index; ++previous) {
                    const auto other = reinterpret_cast<std::uintptr_t>(block.planes[previous]);
                    if (AllTrue(std::array{start < other + bytes, other < start + bytes}))
                        return false;
                }
            }
            return true;
        }
    }  // namespace

    /** @copydoc ValidateAudioPlanarBlock */
    bool ValidateAudioPlanarBlock(const AudioPlanarBlockView &block, const AudioProcessingFormat &expected) noexcept {
        if (!AllTrue(std::array{ValidateAudioProcessingFormat(expected), block.sampleRate == expected.sampleRate,
                                block.validFrames <= block.capacityFrames, block.capacityFrames != 0,
                                block.capacityFrames <= MaximumAudioCallbackFrames,
                                block.planes.size() == expected.layout.orderedChannels.size()}))
            return false;
        return AllTrue(std::array{LayoutMatches(block.layout, expected.layout), ValidPlanes(block)});
    }
}  // namespace Horo::Audio
