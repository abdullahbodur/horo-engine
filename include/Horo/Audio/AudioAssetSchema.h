#pragma once

/**
 * @file AudioAssetSchema.h
 * @brief Versioned, bounded authoring schemas for audio media and deterministic variations.
 */

#include "Horo/Audio/AudioFormat.h"
#include "Horo/Audio/AudioIdentity.h"
#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Horo::Audio {
    /** @brief Compiled ceiling for loop records in one Audio asset. */
    inline constexpr std::size_t MaximumAudioAssetLoopRegions = 64;
    /** @brief Compiled ceiling for marker records in one Audio asset. */
    inline constexpr std::size_t MaximumAudioAssetMarkers = 4'096;
    /** @brief Compiled ceiling for seek records in one streamed Audio asset. */
    inline constexpr std::size_t MaximumAudioAssetSeekPoints = 65'536;
    /** @brief Compiled ceiling for clip references in one variation asset. */
    inline constexpr std::size_t MaximumAudioVariationEntries = 1'024;
    /** @brief Compiled byte ceiling for one persisted marker label. */
    inline constexpr std::size_t MaximumAudioMarkerLabelBytes = 255;

    /** @brief Current persisted Audio asset schema version. */
    struct AudioAssetSchemaVersion final {
        std::uint16_t major{1};
        std::uint16_t minor{};
        constexpr auto operator<=>(const AudioAssetSchemaVersion &) const noexcept = default;
    };

    inline constexpr AudioAssetSchemaVersion CurrentAudioAssetSchemaVersion{1, 0};

    /** @brief Project-lowerable validation limits bounded by the compiled Audio profile. */
    struct AudioAssetSchemaLimits final {
        std::size_t maximumLoopRegions{MaximumAudioAssetLoopRegions};
        std::size_t maximumMarkers{MaximumAudioAssetMarkers};
        std::size_t maximumSeekPoints{MaximumAudioAssetSeekPoints};
        std::size_t maximumVariationEntries{MaximumAudioVariationEntries};
        std::size_t maximumMarkerLabelBytes{MaximumAudioMarkerLabelBytes};
        bool operator==(const AudioAssetSchemaLimits &) const = default;
    };

    /** @brief Stable non-zero identity local to one persisted Audio asset. */
    struct AudioAssetElementId final {
        std::uint32_t value{};

        /** @brief Reports whether the local persisted identity is non-zero. @return True for a usable identity. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        constexpr auto operator<=>(const AudioAssetElementId &) const noexcept = default;
    };

    /** @brief Half-open sample-frame loop region `[startFrame, endFrame)`. */
    struct AudioLoopRegion final {
        AudioAssetElementId id;
        std::uint64_t startFrame{};
        std::uint64_t endFrame{};
        constexpr auto operator<=>(const AudioLoopRegion &) const noexcept = default;
    };

    /** @brief Named sample-frame marker; the persisted label limit is measured in bytes. */
    struct AudioMarker final {
        AudioAssetElementId id;
        std::uint64_t frame{};
        std::string label;
        bool operator==(const AudioMarker &) const = default;
    };

    /** @brief Optional cook-produced loudness values using explicit logarithmic units. */
    struct AudioLoudnessMetadata final {
        std::optional<float> integratedLufs;
        std::optional<float> shortTermLufs;
        std::optional<float> truePeakDbtp;
        std::optional<float> rmsDbfs;
        std::optional<float> normalizationGainDb;
        bool operator==(const AudioLoudnessMetadata &) const = default;
    };

    /** @brief Shared semantic media facts; duration is derived from frame count and sample rate. */
    struct AudioMediaDescriptor final {
        AudioProcessingFormat format;
        std::uint64_t frameCount{};
        bool operator==(const AudioMediaDescriptor &) const = default;
    };

    /** @brief Persisted metadata shared by resident clips and streamed media. */
    struct AudioMediaAnnotations final {
        std::vector<AudioLoopRegion> loops;
        std::vector<AudioMarker> markers;
        std::optional<AudioLoudnessMetadata> loudness;
        bool operator==(const AudioMediaAnnotations &) const = default;
    };

    /** @brief Resident clip payload facts; encoded container and codec identity are owned by AUD-002.4. */
    struct AudioClipPayloadDescriptor final {
        AudioMediaDescriptor media;
        bool operator==(const AudioClipPayloadDescriptor &) const = default;
    };

    /** @brief Resident clip authoring schema; contains no runtime voice, decoder or native handle. */
    struct AudioClipAssetSchema final {
        AudioAssetSchemaVersion version{CurrentAudioAssetSchemaVersion};
        AudioClipPayloadDescriptor payload;
        AudioMediaAnnotations annotations;
        bool operator==(const AudioClipAssetSchema &) const = default;
    };

    /** @brief One decoded-frame to encoded-byte seek anchor in strictly increasing order. */
    struct AudioSeekPoint final {
        std::uint64_t frame{};
        std::uint64_t payloadByteOffset{};
        constexpr auto operator<=>(const AudioSeekPoint &) const noexcept = default;
    };

    /** @brief Stream payload facts required to validate bounded seek metadata. */
    struct AudioStreamPayloadDescriptor final {
        AudioMediaDescriptor media;
        std::uint64_t payloadByteCount{};
        std::uint32_t decodeBlockFrameCount{};
        std::vector<AudioSeekPoint> seekPoints;
        bool operator==(const AudioStreamPayloadDescriptor &) const = default;
    };

    /** @brief Stream authoring schema; seek data is metadata and performs no decoder work. */
    struct AudioStreamAssetSchema final {
        AudioAssetSchemaVersion version{CurrentAudioAssetSchemaVersion};
        AudioStreamPayloadDescriptor payload;
        AudioMediaAnnotations annotations;
        bool operator==(const AudioStreamAssetSchema &) const = default;
    };

    /** @brief Deterministic policy used to select one ordered variation entry. */
    enum class AudioVariationSelection : std::uint8_t {
        Random,
        RoundRobin,
        Shuffle,
        WeightedRandom
    };

    /** @brief One stable clip dependency and its weighted-random contribution. */
    struct AudioVariationEntry final {
        AudioClipId clip;
        float weight{1.0F};
        bool operator==(const AudioVariationEntry &) const = default;
    };

    /** @brief Inclusive finite authoring range; validation never clamps or normalizes it. */
    struct AudioVariationRange final {
        float minimum{};
        float maximum{};
        constexpr auto operator<=>(const AudioVariationRange &) const noexcept = default;
    };

    /** @brief Ordered deterministic variation schema resolved before callback submission. */
    struct AudioVariationAssetSchema final {
        AudioAssetSchemaVersion version{CurrentAudioAssetSchemaVersion};
        AudioVariationSelection selection{AudioVariationSelection::Random};
        std::vector<AudioVariationEntry> entries;
        AudioVariationRange pitchDeltaSemitones;
        AudioVariationRange gainDeltaDb;
        std::uint64_t deterministicSeed{};
        bool operator==(const AudioVariationAssetSchema &) const = default;
    };

    /**
     * @brief Validates project limits before they are used for schema admission.
     * @param limits Candidate non-zero limits that may lower but never exceed compiled maxima.
     * @return Success or a stable limit error.
     * @throws std::bad_alloc When constructing an error diagnostic fails.
     */
    [[nodiscard]] Result<void> ValidateAudioAssetSchemaLimits(const AudioAssetSchemaLimits &limits);

    /**
     * @brief Validates a resident AudioClip schema without mutation or normalization.
     * @param schema Candidate persisted schema.
     * @param limits Already configured active limits.
     * @return Success or a typed version, limit or structural error.
     * @throws std::bad_alloc When constructing an error diagnostic fails.
     */
    [[nodiscard]] Result<void> ValidateAudioClipAssetSchema(const AudioClipAssetSchema &schema, const AudioAssetSchemaLimits &limits = {});

    /**
     * @brief Validates an AudioStream schema and its monotonic bounded seek table.
     * @param schema Candidate persisted schema.
     * @param limits Already configured active limits.
     * @return Success or a typed version, limit or structural error.
     * @throws std::bad_alloc When constructing an error diagnostic fails.
     */
    [[nodiscard]] Result<void> ValidateAudioStreamAssetSchema(const AudioStreamAssetSchema &schema,
                                                              const AudioAssetSchemaLimits &limits = {});

    /**
     * @brief Validates ordered clip dependencies and deterministic selection metadata.
     * @param schema Candidate persisted variation schema.
     * @param limits Already configured active limits.
     * @return Success or a typed version, limit or structural error.
     * @throws std::bad_alloc When constructing an error diagnostic fails.
     */
    [[nodiscard]] Result<void> ValidateAudioVariationAssetSchema(const AudioVariationAssetSchema &schema,
                                                                 const AudioAssetSchemaLimits &limits = {});
}  // namespace Horo::Audio
