#pragma once

/**
 * @file CinematicIdentity.h
 * @brief Stable generation-safe cinematic identities and canonical persistence encoding.
 */

#include "Horo/Cinematic/CinematicErrors.h"
#include "Horo/Foundation/Result.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Horo::Cinematic {
    /** @brief Canonical network-byte-order encoding of a stable value and generation. */
    using SerializedCinematicIdentity = std::array<std::uint8_t, 12>;

    /** @brief Strong generation-safe identity in one tag-defined cinematic domain. */
    template <typename Tag> struct CinematicIdentity final {
        using IdentityTag = Tag;

        std::uint64_t stableValue{}; /**< Durable owner-issued value retained across editor, save, and cook boundaries. */
        std::uint32_t generation{};  /**< Non-zero, non-wrapping generation for reuse after explicit retirement. */

        /** @brief Checks representation, not current document residency. @return True when both dimensions are non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return stableValue != 0 && generation != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const CinematicIdentity &) const noexcept = default;
    };

    struct SequenceIdentityTag;
    struct TrackIdentityTag;
    struct KeyframeIdentityTag;
    struct PropertyBindingIdentityTag;

    /** @brief Stable generation-safe identity of one authored cinematic sequence. */
    using SequenceId = CinematicIdentity<SequenceIdentityTag>;
    /** @brief Stable generation-safe identity of one track in authored sequence data. */
    using TrackId = CinematicIdentity<TrackIdentityTag>;
    /** @brief Stable generation-safe identity of one keyframe in authored sequence data. */
    using KeyframeId = CinematicIdentity<KeyframeIdentityTag>;
    /** @brief Stable generation-safe identity of one declared property binding. */
    using PropertyBindingId = CinematicIdentity<PropertyBindingIdentityTag>;

    /**
     * @brief Creates a validated cinematic identity from owner-issued dimensions.
     * @param stableValue Durable non-zero authored value.
     * @param generation Non-zero current generation.
     * @return Typed identity or CinematicErrors::IdentityInvalid.
     */
    template <typename Tag>
    [[nodiscard]] Result<CinematicIdentity<Tag>> MakeCinematicIdentity(const std::uint64_t stableValue, const std::uint32_t generation) {
        const CinematicIdentity<Tag> identity{stableValue, generation};
        if (!identity.IsValid())
            return Result<CinematicIdentity<Tag>>::Failure(MakeError(CinematicErrors::IdentityInvalid));
        return Result<CinematicIdentity<Tag>>::Success(identity);
    }

    /**
     * @brief Encodes an identity in a fixed-width canonical network-byte-order representation.
     * @param identity Identity to persist through editor, save/load, and cook boundaries.
     * @return Exact bytes; an invalid identity encodes reserved values and fails decoding.
     */
    template <typename Tag>
    [[nodiscard]] constexpr SerializedCinematicIdentity SerializeCinematicIdentity(const CinematicIdentity<Tag> identity) noexcept {
        SerializedCinematicIdentity bytes{};
        for (std::size_t byte = 0; byte < sizeof(identity.stableValue); ++byte) {
            const std::size_t shift = (sizeof(identity.stableValue) - byte - 1U) * 8U;
            bytes[byte] = static_cast<std::uint8_t>(identity.stableValue >> shift);
        }
        for (std::size_t byte = 0; byte < sizeof(identity.generation); ++byte) {
            const std::size_t shift = (sizeof(identity.generation) - byte - 1U) * 8U;
            bytes[sizeof(identity.stableValue) + byte] = static_cast<std::uint8_t>(identity.generation >> shift);
        }
        return bytes;
    }

    /**
     * @brief Decodes and validates one canonical cinematic identity.
     * @param bytes Fixed-width network-byte-order representation.
     * @return Exact typed identity or CinematicErrors::SerializedIdentityInvalid.
     */
    template <typename Tag>
    [[nodiscard]] Result<CinematicIdentity<Tag>> DeserializeCinematicIdentity(const SerializedCinematicIdentity &bytes) {
        std::uint64_t stableValue{};
        for (std::size_t byte = 0; byte < sizeof(stableValue); ++byte)
            stableValue = (stableValue << 8U) | bytes[byte];

        std::uint32_t generation{};
        for (std::size_t byte = sizeof(stableValue); byte < bytes.size(); ++byte)
            generation = (generation << 8U) | bytes[byte];

        auto identity = MakeCinematicIdentity<Tag>(stableValue, generation);
        if (identity.HasError())
            return Result<CinematicIdentity<Tag>>::Failure(MakeError(CinematicErrors::SerializedIdentityInvalid));
        return identity;
    }

    /**
     * @brief Validates a submitted identity against the owning document's exact current identity.
     * @param submitted Candidate supplied by an editor or runtime consumer.
     * @param current Current identity stored by the owner.
     * @return Success, IdentityInvalid, IdentityUnknown, or IdentityStale.
     */
    template <typename Tag>
    [[nodiscard]] Result<void> ValidateCinematicIdentityAccess(const CinematicIdentity<Tag> submitted,
                                                               const CinematicIdentity<Tag> current) {
        if (!submitted.IsValid() || !current.IsValid())
            return Result<void>::Failure(MakeError(CinematicErrors::IdentityInvalid));
        if (submitted.stableValue != current.stableValue)
            return Result<void>::Failure(MakeError(CinematicErrors::IdentityUnknown));
        if (submitted.generation != current.generation)
            return Result<void>::Failure(MakeError(CinematicErrors::IdentityStale));
        return Result<void>::Success();
    }

    /**
     * @brief Advances an identity generation without changing its durable value or wrapping.
     * @param current Current valid identity retired by its owner.
     * @return Replacement identity, IdentityInvalid, or GenerationExhausted.
     */
    template <typename Tag>
    [[nodiscard]] Result<CinematicIdentity<Tag>> AdvanceCinematicIdentityGeneration(const CinematicIdentity<Tag> current) {
        if (!current.IsValid())
            return Result<CinematicIdentity<Tag>>::Failure(MakeError(CinematicErrors::IdentityInvalid));
        if (current.generation == std::numeric_limits<std::uint32_t>::max())
            return Result<CinematicIdentity<Tag>>::Failure(MakeError(CinematicErrors::GenerationExhausted));
        return MakeCinematicIdentity<Tag>(current.stableValue, current.generation + 1U);
    }

    /** @brief Inert identity group used by playback and authoring test harnesses. */
    struct CinematicIdentityComposition final {
        SequenceId sequence{};       /**< Sequence identity. */
        TrackId track{};             /**< Track identity. */
        KeyframeId keyframe{};       /**< Keyframe identity. */
        PropertyBindingId binding{}; /**< Property-binding identity. */

        /** @brief Reports whether this is the explicit null composition. @return True when every identity is invalid. */
        [[nodiscard]] constexpr bool IsNull() const noexcept {
            return !sequence.IsValid() && !track.IsValid() && !keyframe.IsValid() && !binding.IsValid();
        }

        /** @brief Reports whether every identity is usable. @return True for a complete composition. */
        [[nodiscard]] constexpr bool IsComplete() const noexcept {
            return sequence.IsValid() && track.IsValid() && keyframe.IsValid() && binding.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const CinematicIdentityComposition &) const noexcept = default;
    };

    /** @brief Returns an inert explicit null composition. @return A composition containing no usable identity. */
    [[nodiscard]] constexpr CinematicIdentityComposition NullCinematicIdentityComposition() noexcept {
        return {};
    }

    /**
     * @brief Creates a repeatable complete composition for playback and authoring harnesses.
     * @param firstStableValue First of four consecutive durable values.
     * @param generation Shared non-zero generation.
     * @return Complete composition or IdentityInvalid when the range cannot represent four identities.
     * @note Construction is inert metadata; it performs no registration or ambient mutation.
     */
    [[nodiscard]] Result<CinematicIdentityComposition> MakeDeterministicCinematicIdentityComposition(std::uint64_t firstStableValue,
                                                                                                     std::uint32_t generation);
}  // namespace Horo::Cinematic
