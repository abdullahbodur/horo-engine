#pragma once

/** @file AudioPlanarBlock.h
 * @brief Borrowed canonical processing buffers with explicit semantic shape and capacity.
 */

#include "Horo/Audio/AudioFormat.h"

namespace Horo::Audio {
    /**
     * @brief Borrowed planar binary32 storage for one processing call, never a native device buffer.
     *
     * Every plane contains capacityFrames contiguous samples and begins at a 64-byte boundary.
     * Planes do not overlap. The caller retains the immutable layout and plane-pointer array,
     * and grants exclusive sample writes for the call. Views must not escape to jobs or later
     * callbacks. Valid frames and padding are positive-zero silence unless completely overwritten
     * under the prepared graph's contract. This view owns no storage or epoch lease.
     */
    struct AudioPlanarBlockView final {
        AudioChannelLayoutView layout;
        std::uint32_t sampleRate{};
        std::span<AudioSample *const> planes;
        std::uint32_t validFrames{};
        std::uint32_t capacityFrames{};
    };

    /**
     * @brief Validates prepared buffer shape, semantic format, alignment and non-overlapping address ranges.
     * @param block Borrowed buffers to validate, without reading or writing their sample contents.
     * @param expected Admitted canonical format, retained by the owner.
     * @return True for a matching bounded shape; does not prove address accessibility, allocation extent,
     * lifetime, padding initialization or exclusive ownership. Callers must prove those before processing.
     * @pre Control/preparation use. Validation is bounded quadratic work over at most MaximumAudioChannels
     * planes and is not an implicit per-frame callback operation. Supported targets use flat object addresses.
     */
    [[nodiscard]] bool ValidateAudioPlanarBlock(const AudioPlanarBlockView &block, const AudioProcessingFormat &expected) noexcept;
}  // namespace Horo::Audio
