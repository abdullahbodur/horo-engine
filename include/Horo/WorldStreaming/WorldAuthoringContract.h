#pragma once

/**
 * @file WorldAuthoringContract.h
 * @brief Immutable world-authoring page and collaboration authority contract.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/WorldStreaming/WorldStreamingIdentity.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::WorldStreaming {
    /** @brief Supported world-authoring contract schema version. */
    struct WorldAuthoringContractVersion final {
        static constexpr std::uint16_t CurrentMajor = 1;
        static constexpr std::uint16_t CurrentMinor = 0;

        std::uint16_t major{CurrentMajor}; /**< Breaking contract version. */
        std::uint16_t minor{CurrentMinor}; /**< Backward-compatible contract version. */

        [[nodiscard]] constexpr auto operator<=>(const WorldAuthoringContractVersion &) const noexcept = default;
    };

    /** @brief Durable authoring unit; deliberately independent from cooked streaming cells. */
    enum class WorldAuthoringGranularity : std::uint8_t {
        SpatialPage,
    };

    /** @brief Authority used to publish a replacement of one immutable authoring page. */
    enum class WorldAuthoringCollaborationMode : std::uint8_t {
        RevisionChecked,
    };

    /** @brief Hard capacity applied to simultaneously open authoring pages. */
    struct WorldAuthoringContractLimits final {
        std::uint32_t maximumOpenPages{}; /**< Non-zero authoring-owner page limit. */
    };

    /** @brief Validated immutable decision for world-authoring storage and collaboration. */
    class WorldAuthoringContract final {
    public:
        /**
         * @brief Validates the versioned authoring decision without opening documents or contacting source control.
         * @param version Exact supported contract version.
         * @param granularity Source-controlled authoring document granularity.
         * @param collaboration Authority used to replace an immutable page revision.
         * @param limits Mandatory owner capacity.
         * @return Immutable contract, or a typed invalid, unsupported-version, or unsupported-policy error.
         */
        [[nodiscard]] static Result<WorldAuthoringContract> Create(WorldAuthoringContractVersion version,
                                                                   WorldAuthoringGranularity granularity,
                                                                   WorldAuthoringCollaborationMode collaboration,
                                                                   WorldAuthoringContractLimits limits);

        /** @brief Returns the admitted contract version. @return Immutable version value. */
        [[nodiscard]] constexpr WorldAuthoringContractVersion Version() const noexcept {
            return version_;
        }

        /** @brief Returns source document granularity. @return SpatialPage for version one. */
        [[nodiscard]] constexpr WorldAuthoringGranularity Granularity() const noexcept {
            return granularity_;
        }

        /** @brief Returns replacement authority. @return RevisionChecked for version one. */
        [[nodiscard]] constexpr WorldAuthoringCollaborationMode Collaboration() const noexcept {
            return collaboration_;
        }

        /** @brief Returns the authoring owner capacity. @return Validated non-zero limits. */
        [[nodiscard]] constexpr WorldAuthoringContractLimits Limits() const noexcept {
            return limits_;
        }

    private:
        constexpr WorldAuthoringContract(WorldAuthoringContractVersion version, WorldAuthoringGranularity granularity,
                                         WorldAuthoringCollaborationMode collaboration, WorldAuthoringContractLimits limits) noexcept
            : version_(version), granularity_(granularity), collaboration_(collaboration), limits_(limits) {}

        WorldAuthoringContractVersion version_{};
        WorldAuthoringGranularity granularity_{};
        WorldAuthoringCollaborationMode collaboration_{};
        WorldAuthoringContractLimits limits_{};
    };

    /** @brief One immutable source-controlled spatial authoring page. */
    struct WorldAuthoringPageDescriptor final {
        WorldPartitionId partition{};      /**< World whose authored state contains this page. */
        Assets::AssetId sourceAsset{};     /**< Stable page asset and source-control unit identity. */
        WorldAuthoringRevision revision{}; /**< Exact immutable content revision. */

        /** @brief Checks representation only, without registry or filesystem access. @return Whether all identities are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldAuthoringPageDescriptor &) const noexcept = default;
    };

    /** @brief Admission state for the world-authoring document owner. */
    enum class WorldAuthoringOwnerState : std::uint8_t {
        Active,
        Cancelling,
        Closed,
    };

    /** @brief Immutable request to open a page or publish its next revision. */
    struct WorldAuthoringPageRequest final {
        WorldAuthoringPageDescriptor candidate{};                 /**< Complete proposed page head. */
        std::optional<WorldAuthoringRevision> expectedRevision{}; /**< Required CAS revision for replacement; absent for insert. */
    };

    /** @brief Immutable owner snapshot consumed by pure authoring admission validation. */
    struct WorldAuthoringAdmissionContext final {
        WorldPartitionId expectedPartition{};                                  /**< Partition owned by this document authority. */
        std::optional<WorldAuthoringPageDescriptor> currentPage{};             /**< Existing head for the candidate source asset, if any. */
        std::size_t openPageCount{};                                           /**< Pages currently charged to the owner. */
        WorldAuthoringOwnerState ownerState{WorldAuthoringOwnerState::Closed}; /**< Current lifecycle gate. */
    };

    /** @brief Mutation kind authorized by successful pure validation. */
    enum class WorldAuthoringAdmissionKind : std::uint8_t {
        Open,
        Replace,
    };

    /**
     * @brief Validates a page admission without mutating documents, source control, or the supplied snapshot.
     * @param contract Validated immutable version-one authoring decision.
     * @param request Complete open or revision-replacement request.
     * @param context Current partition, page head, capacity, and owner lifecycle snapshot.
     * @return Open or Replace, or a typed invalid, stale, conflict, capacity, lifecycle, or exhaustion error.
     */
    [[nodiscard]] Result<WorldAuthoringAdmissionKind> ValidateWorldAuthoringAdmission(const WorldAuthoringContract &contract,
                                                                                      const WorldAuthoringPageRequest &request,
                                                                                      const WorldAuthoringAdmissionContext &context);
}  // namespace Horo::WorldStreaming
