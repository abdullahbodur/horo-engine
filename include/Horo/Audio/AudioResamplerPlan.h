#pragma once

/** @file AudioResamplerPlan.h
 * @brief Control-thread admission of backend-neutral sample-rate conversion work.
 */

#include "Horo/Foundation/Result.h"

#include <cstdint>

namespace Horo::Audio {
    /** @brief The boundary that owns conversion; device conversion cannot apply voice pitch. */
    enum class AudioResamplerStage : std::uint8_t {
        ClipToMix,
        MixToDevice
    };

    /** @brief Explicit kernel selection; Linear is an unfiltered low-cost mode, not an anti-aliasing guarantee. */
    enum class AudioResamplerQuality : std::uint8_t {
        Linear,
        Sinc32,
        Sinc64
    };

    /** @brief Owned preparation request. Sample rates and pitch are independent of playback-speed control. */
    struct AudioResamplerDescriptor {
        AudioResamplerStage stage{AudioResamplerStage::ClipToMix};
        AudioResamplerQuality quality{AudioResamplerQuality::Sinc32};
        std::uint32_t inputRate{}; /**< Explicit frames per second; no default device or mixer rate. */
        std::uint32_t outputRate{};
        std::uint32_t channels{}; /**< Plane count only; callers retain the same semantic layout. */
        std::uint32_t maximumOutputFrames{};
        double pitch{1.0};         /**< Source-frame speed multiplier; changes pitch and duration together. */
        double playbackSpeed{1.0}; /**< Pitch-preserving time stretch is unavailable; only unity is admitted. */
    };

    /** @brief Explicit per-instance admission limits, not measured wall-clock performance claims. */
    struct AudioResamplerBudget {
        std::uint64_t maximumSampleProducts{};  /**< Channels * output frames * taps per call. */
        std::uint64_t maximumHistoryBytes{};    /**< Planar binary32 history required by the kernel. */
        std::uint32_t maximumLookAheadFrames{}; /**< Latency allowance in input-frame units. */
    };

    /** @brief Immutable admitted metadata. It neither allocates processing state nor activates a callback. */
    class AudioResamplerPlan final {
    public:
        /**
         * @brief Validate one conversion request outside the real-time callback.
         * @param descriptor Explicit conversion, kernel, channel and block-size request.
         * @param budget Work, history and look-ahead limits reserved by the owning composition.
         * @return An admitted plan or a stable audio validation/unsupported/budget error.
         */
        [[nodiscard]] static Result<AudioResamplerPlan> Prepare(const AudioResamplerDescriptor &descriptor,
                                                                const AudioResamplerBudget &budget);
        /** @brief Get the owned, validated request. @return Immutable descriptor reference. */
        [[nodiscard]] const AudioResamplerDescriptor &Descriptor() const noexcept;
        /** @brief Get input frames advanced per output frame. @return Rate ratio multiplied by pitch. */
        [[nodiscard]] double InputStep() const noexcept;
        /** @brief Get the exact kernel width. @return Number of input taps per output sample. */
        [[nodiscard]] std::uint32_t Taps() const noexcept;
        /** @brief Get reserved processing requirements. @return Work, history and input look-ahead costs. */
        [[nodiscard]] const AudioResamplerBudget &Requirements() const noexcept;

    private:
        /** @brief Construct only after admission has validated all fields and costs. */
        AudioResamplerPlan(const AudioResamplerDescriptor &descriptor, double step, std::uint32_t taps,
                           const AudioResamplerBudget &requirements);
        AudioResamplerDescriptor descriptor_;
        double step_;
        std::uint32_t taps_;
        AudioResamplerBudget requirements_;
    };
}  // namespace Horo::Audio
