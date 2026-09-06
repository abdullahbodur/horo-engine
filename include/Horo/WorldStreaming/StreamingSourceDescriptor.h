#pragma once

/**
 * @file StreamingSourceDescriptor.h
 * @brief Inert source metadata and pure bounded admission rules for world streaming.
 */

#include "Horo/WorldStreaming/WorldStreamingIdentity.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace Horo::WorldStreaming {
    /** @brief Why a host-authored source contributes world-streaming relevance. */
    enum class StreamingSourceIntent : std::uint8_t {
        Camera,
        Gameplay,
        NetworkRelevance,
        Preload,
    };

    /** @brief Finite non-negative base priority; ordering policy is owned by WST-002.5. */
    class StreamingSourcePriority final {
    public:
        /** @brief Constructs the reserved invalid priority. */
        StreamingSourcePriority() = default;

        /**
         * @brief Validates a source priority without registering or evaluating a source.
         * @param value Finite non-negative priority weight.
         * @return Typed priority or SourceDescriptorInvalid.
         */
        [[nodiscard]] static Result<StreamingSourcePriority> Create(float value);

        /** @brief Returns the validated priority weight. @return Stored weight, or -1 for the invalid default. */
        [[nodiscard]] constexpr float Value() const noexcept {
            return value_;
        }

        /** @brief Checks only the value representation. @return Whether the weight is finite and non-negative. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ >= 0.0F && value_ <= std::numeric_limits<float>::max();
        }

        [[nodiscard]] constexpr auto operator<=>(const StreamingSourcePriority &) const noexcept = default;

    private:
        explicit constexpr StreamingSourcePriority(const float value) noexcept : value_(value) {}

        float value_{-1.0F};
    };

    /**
     * @brief Runtime-only generation-checked identity for one source-owner lifetime.
     * @details The owning authority issues tokens. Closing an owner invalidates its token; a reused slot must receive a nonzero, strictly
     * newer generation. Tokens are copied into async requests but are never serialized.
     */
    struct StreamingSourceOwnerToken final {
        static constexpr std::uint32_t InvalidSlot = std::numeric_limits<std::uint32_t>::max();

        WorldPartitionId partition;      /**< Stable partition identity. */
        PartitionEpoch epoch;            /**< Mounted partition incarnation. */
        std::uint32_t slot{InvalidSlot}; /**< Owner-registry slot, meaningful only to the issuing authority. */
        std::uint32_t generation{};      /**< Nonzero slot generation; never wraps within the authority lifetime. */

        /** @brief Checks token representation, not whether the owner is currently active. @return True when all token fields are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const StreamingSourceOwnerToken &) const noexcept = default;
    };

    /** @brief Inert backend-neutral metadata for one stable streaming relevance source. */
    struct StreamingSourceDescriptor final {
        StreamingSourceId id;             /**< Stable host-authored source identity. */
        StreamingSourceOwnerToken owner;  /**< Exact runtime owner lifetime; never persisted. */
        StreamingSourceIntent intent{};   /**< Source category; shape and range are defined by WST-002.2. */
        StreamingSourcePriority priority; /**< Base weight; reduction and starvation policy are separate. */
        StreamingSourceRevision revision; /**< Strictly monotonic revision for replacement. */

        /** @brief Checks structural representation only and performs no registration. @return Whether every value field is valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const StreamingSourceDescriptor &) const noexcept = default;
    };

    /** @brief Admission availability for one owner lifetime. */
    enum class StreamingSourceOwnerState : std::uint8_t {
        Active,
        Cancelling,
        Closed,
    };

    /** @brief Immutable authority snapshot consumed by pure source admission validation. */
    struct StreamingSourceAdmissionContext final {
        StreamingSourceOwnerToken expectedOwner;                /**< Currently active owner token. */
        std::optional<StreamingSourceRevision> currentRevision; /**< Revision when this stable source is already admitted. */
        std::size_t activeSourceCount{};                        /**< Count charged to the bounded owner registry. */
        std::size_t sourceCapacity{};                           /**< Maximum distinct active source identities. */
        StreamingSourceOwnerState ownerState{StreamingSourceOwnerState::Closed}; /**< Current lifecycle gate. */
    };

    /** @brief Mutation kind authorized by successful pure validation. */
    enum class StreamingSourceAdmissionKind : std::uint8_t {
        Insert,
        Replace,
    };

    /**
     * @brief Validates inert source metadata.
     * @param descriptor Descriptor to validate without registration or ambient state access.
     * @return Success, SourceDescriptorInvalid, or SourceIntentUnsupported.
     */
    [[nodiscard]] Result<void> ValidateStreamingSourceDescriptor(const StreamingSourceDescriptor &descriptor);

    /**
     * @brief Validates a source admission against an immutable owner snapshot without mutating it.
     * @param descriptor Structurally valid candidate source descriptor.
     * @param context Current owner lifetime, revision, capacity, and lifecycle snapshot.
     * @return Insert or Replace, or a typed invalid, unsupported, stale, capacity, or lifecycle error. Replacement of an existing identity
     * does not consume another capacity slot.
     */
    [[nodiscard]] Result<StreamingSourceAdmissionKind> ValidateStreamingSourceAdmission(const StreamingSourceDescriptor &descriptor,
                                                                                        const StreamingSourceAdmissionContext &context);
}  // namespace Horo::WorldStreaming
