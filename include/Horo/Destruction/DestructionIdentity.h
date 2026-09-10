#pragma once

/**
 * @file DestructionIdentity.h
 * @brief Stable authored fracture identities and generation-fenced runtime identity compositions.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/Destruction/DestructionErrors.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/Sha256.h"
#include "Horo/Foundation/StrongId.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace Horo::Destruction {
    /** @brief Strong non-zero destruction identity in one tag-defined domain. */
    template <typename Tag> using DestructionStableIdentity = Foundation::Detail::NonZeroId64<Tag, DestructionErrors::IdentityInvalid>;

    struct DestructibleIdentityTag;
    struct DestructionChunkIdentityTag;
    struct FractureContentRevisionTag;
    struct DestructionWorldIdentityTag;
    struct DestructionGenerationTag;
    struct DestructionStateRevisionTag;
    struct DestructionCommandValueTag;

    /** @brief Stable authored destructible occurrence, independent of entity slot, name, or runtime handle. */
    using DestructibleId = DestructionStableIdentity<DestructibleIdentityTag>;
    /** @brief Stable artifact-local chunk identity, independent of table position, name, and native subshape. */
    using DestructionChunkId = DestructionStableIdentity<DestructionChunkIdentityTag>;
    /** @brief Non-zero durable version of one fracture asset's canonical semantic content. */
    using FractureContentRevision = DestructionStableIdentity<FractureContentRevisionTag>;
    /** @brief Process-local identity of one scene-scoped destruction-world incarnation. */
    using DestructionWorldId = DestructionStableIdentity<DestructionWorldIdentityTag>;
    /** @brief Non-wrapping runtime incarnation of one stable destructible occurrence. */
    using DestructionGeneration = DestructionStableIdentity<DestructionGenerationTag>;
    /** @brief Monotonic semantic state revision within one destructible generation. */
    using DestructionStateRevision = DestructionStableIdentity<DestructionStateRevisionTag>;
    /** @brief Owner-issued idempotency value for one command in an exact destructible generation. */
    using DestructionCommandValue = DestructionStableIdentity<DestructionCommandValueTag>;

    /** @brief Canonical network-byte-order encoding of one stable 64-bit identity. */
    using SerializedDestructionStableIdentity = std::array<std::uint8_t, sizeof(std::uint64_t)>;
    /** @brief Canonical fracture asset identity bytes. */
    using SerializedFractureAssetId = std::array<std::uint8_t, 16>;
    /** @brief Canonical asset, revision, and semantic digest content identity. */
    using SerializedFractureArtifactContentIdentity = std::array<std::uint8_t, 56>;
    /** @brief Canonical exact-content chunk identity. */
    using SerializedFractureChunkIdentity = std::array<std::uint8_t, 64>;
    /** @brief Canonical world, destructible, and runtime-generation handle identity. */
    using SerializedDestructionHandle = std::array<std::uint8_t, 24>;
    /** @brief Canonical command identity. */
    using SerializedDestructionCommandId = std::array<std::uint8_t, 32>;
    /** @brief Canonical committed-event occurrence identity. */
    using SerializedDestructionEventOccurrenceId = std::array<std::uint8_t, 37>;

    /**
     * @brief Encodes one stable destruction identity without native layout or process state.
     * @param identity Stable identity to encode.
     * @return Exact eight-byte network-order representation; invalid encodes as zero.
     */
    template <typename Tag>
    [[nodiscard]] constexpr SerializedDestructionStableIdentity SerializeDestructionStableIdentity(
        const DestructionStableIdentity<Tag> identity) noexcept {
        SerializedDestructionStableIdentity bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            const std::size_t shift = (bytes.size() - index - 1U) * 8U;
            bytes[index] = static_cast<std::uint8_t>(identity.Value() >> shift);
        }
        return bytes;
    }

    /**
     * @brief Decodes one stable destruction identity from canonical bytes.
     * @param bytes Exact network-order representation.
     * @return Typed identity or DestructionErrors::SerializedIdentityInvalid.
     */
    template <typename Tag>
    [[nodiscard]] Result<DestructionStableIdentity<Tag>> DeserializeDestructionStableIdentity(
        const SerializedDestructionStableIdentity &bytes) {
        std::uint64_t value{};
        std::size_t shift{};
        for (auto iterator = bytes.rbegin(); iterator != bytes.rend(); ++iterator, shift += 8U)
            value |= static_cast<std::uint64_t>(*iterator) << shift;
        auto identity = DestructionStableIdentity<Tag>::Create(value);
        if (identity.HasError())
            return Result<DestructionStableIdentity<Tag>>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
        return identity;
    }

    /** @brief Path-independent reference to one canonical fracture asset. */
    class FractureAssetId final {
    public:
        /** @brief Constructs the reserved invalid fracture asset identity. */
        FractureAssetId() = default;

        /**
         * @brief Validates an Assets-owned stable identity for fracture use.
         * @param asset Stable sidecar identity; paths and names are not accepted.
         * @return Typed identity or DestructionErrors::IdentityInvalid.
         */
        [[nodiscard]] static Result<FractureAssetId> Create(const Assets::AssetId &asset);

        /** @brief Returns the canonical Assets identity. @return Borrowed immutable asset identity. */
        [[nodiscard]] const Assets::AssetId &Asset() const noexcept;
        /** @brief Checks representation, not whether content is loaded. @return True for a non-zero identity. */
        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] auto operator<=>(const FractureAssetId &) const noexcept = default;

    private:
        explicit FractureAssetId(Assets::AssetId asset) noexcept : asset_(asset) {}

        Assets::AssetId asset_{};
    };

    /** @brief Exact immutable semantic content version of one fracture asset. */
    class FractureArtifactContentIdentity final {
    public:
        /** @brief Constructs the reserved invalid content identity. */
        FractureArtifactContentIdentity() = default;

        /**
         * @brief Validates exact fracture content identity dimensions.
         * @param asset Stable fracture asset identity.
         * @param revision Non-zero durable content revision.
         * @param semanticDigest Digest covering the canonical chunk table and all semantic cook inputs.
         * @return Exact content identity or DestructionErrors::IdentityInvalid.
         */
        [[nodiscard]] static Result<FractureArtifactContentIdentity> Create(FractureAssetId asset, FractureContentRevision revision,
                                                                            const Sha256Digest &semanticDigest);

        /** @brief Returns the stable fracture asset. @return Asset identity owned by this value. */
        [[nodiscard]] constexpr FractureAssetId Asset() const noexcept {
            return asset_;
        }

        /** @brief Returns the exact durable content revision. @return Non-zero content revision. */
        [[nodiscard]] constexpr FractureContentRevision Revision() const noexcept {
            return revision_;
        }

        /** @brief Returns the semantic fingerprint. @return Borrowed canonical SHA-256 digest. */
        [[nodiscard]] const Sha256Digest &SemanticDigest() const noexcept;
        /** @brief Checks all dimensions, not cache residency or compatibility. @return True for usable content identity. */
        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] auto operator<=>(const FractureArtifactContentIdentity &) const noexcept = default;

    private:
        FractureArtifactContentIdentity(FractureAssetId asset, FractureContentRevision revision,
                                        const Sha256Digest &semanticDigest) noexcept
            : asset_(asset), revision_(revision), semanticDigest_(semanticDigest) {}

        FractureAssetId asset_{};
        FractureContentRevision revision_{};
        Sha256Digest semanticDigest_{};
    };

    /** @brief Stable chunk identity fenced to one exact immutable fracture content version. */
    struct FractureChunkIdentity final {
        FractureArtifactContentIdentity content{}; /**< Exact chunk-table/content generation. */
        DestructionChunkId chunk{};                /**< Stable semantic chunk ID, never a table index. */

        /** @brief Checks representation, not artifact residency. @return True when content and chunk are usable. */
        [[nodiscard]] bool IsValid() const noexcept {
            return content.IsValid() && chunk.IsValid();
        }

        [[nodiscard]] auto operator<=>(const FractureChunkIdentity &) const noexcept = default;
    };

    /** @brief Non-owning runtime handle to one stable destructible in an exact world generation. */
    struct DestructionHandle final {
        DestructionWorldId world{};         /**< Exact scene-scoped destruction-world incarnation. */
        DestructibleId destructible{};      /**< Stable authored destructible occurrence. */
        DestructionGeneration generation{}; /**< Exact non-wrapping runtime incarnation. */

        /** @brief Checks representation, not current world residency. @return True when all dimensions are usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return world.IsValid() && destructible.IsValid() && generation.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const DestructionHandle &) const noexcept = default;
    };

    /** @brief Stable idempotency identity for a command targeting one exact destructible generation. */
    struct DestructionCommandId final {
        DestructionHandle target{};      /**< Exact command target incarnation. */
        DestructionCommandValue value{}; /**< Stable issuer/owner-assigned command value. */

        /** @brief Checks representation, not admission or terminal-result retention. @return True when complete. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return target.IsValid() && value.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const DestructionCommandId &) const noexcept = default;
    };

    /** @brief Closed baseline fact kinds used as an input to committed occurrence identity. */
    enum class DestructionFactKind : std::uint8_t {
        Damaged,
        ChunksActivated,
        SupportLost,
        ChunksDormant,
        ChunksReactivated,
        AvailabilityChanged,
    };

    /** @brief Deterministic identity of one fact in one committed semantic revision. */
    struct DestructionEventOccurrenceId final {
        DestructionHandle source{};               /**< Exact world/destructible runtime generation. */
        DestructionStateRevision stateRevision{}; /**< Revision made visible by aggregate commit. */
        DestructionFactKind kind{};               /**< Stable closed fact kind. */
        std::uint32_t revisionOrdinal{};          /**< Zero-based canonical order within the revision. */

        /** @brief Checks typed dimensions and fact-kind range. @return True for a deterministic usable occurrence. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return source.IsValid() && stateRevision.IsValid() && kind <= DestructionFactKind::AvailabilityChanged;
        }

        [[nodiscard]] constexpr auto operator<=>(const DestructionEventOccurrenceId &) const noexcept = default;
    };

    /** @brief Encodes a path-independent fracture asset identity. @param asset Identity to encode.
     * @return Canonical UUID bytes; invalid encodes as zero.
     */
    [[nodiscard]] SerializedFractureAssetId SerializeFractureAssetId(const FractureAssetId &asset) noexcept;
    /** @brief Decodes canonical fracture asset identity bytes. @param bytes Canonical UUID bytes.
     * @return Typed asset identity or DestructionErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<FractureAssetId> DeserializeFractureAssetId(const SerializedFractureAssetId &bytes);
    /** @brief Encodes exact fracture content identity. @param content Identity to encode. @return Canonical bytes. */
    [[nodiscard]] SerializedFractureArtifactContentIdentity SerializeFractureArtifactContentIdentity(
        const FractureArtifactContentIdentity &content) noexcept;
    /** @brief Decodes exact fracture content identity. @param bytes Canonical bytes.
     * @return Typed identity or DestructionErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<FractureArtifactContentIdentity> DeserializeFractureArtifactContentIdentity(
        const SerializedFractureArtifactContentIdentity &bytes);
    /** @brief Encodes an exact-content chunk identity. @param chunk Identity to encode. @return Canonical bytes. */
    [[nodiscard]] SerializedFractureChunkIdentity SerializeFractureChunkIdentity(const FractureChunkIdentity &chunk) noexcept;
    /** @brief Decodes an exact-content chunk identity. @param bytes Canonical bytes.
     * @return Typed identity or DestructionErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<FractureChunkIdentity> DeserializeFractureChunkIdentity(const SerializedFractureChunkIdentity &bytes);
    /** @brief Encodes a generation-fenced runtime handle. @param handle Identity to encode. @return Canonical bytes. */
    [[nodiscard]] SerializedDestructionHandle SerializeDestructionHandle(DestructionHandle handle) noexcept;
    /** @brief Decodes a generation-fenced runtime handle. @param bytes Canonical bytes.
     * @return Typed handle or DestructionErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<DestructionHandle> DeserializeDestructionHandle(const SerializedDestructionHandle &bytes);
    /** @brief Encodes a command identity. @param command Identity to encode. @return Canonical bytes. */
    [[nodiscard]] SerializedDestructionCommandId SerializeDestructionCommandId(const DestructionCommandId &command) noexcept;
    /** @brief Decodes a command identity. @param bytes Canonical bytes.
     * @return Typed identity or DestructionErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<DestructionCommandId> DeserializeDestructionCommandId(const SerializedDestructionCommandId &bytes);
    /** @brief Encodes a deterministic committed-event occurrence. @param occurrence Identity to encode.
     * @return Canonical bytes containing no padding or native representation.
     */
    [[nodiscard]] SerializedDestructionEventOccurrenceId SerializeDestructionEventOccurrenceId(
        const DestructionEventOccurrenceId &occurrence) noexcept;
    /** @brief Decodes a deterministic committed-event occurrence. @param bytes Canonical bytes.
     * @return Typed identity or DestructionErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<DestructionEventOccurrenceId> DeserializeDestructionEventOccurrenceId(
        const SerializedDestructionEventOccurrenceId &bytes);

    /** @brief Advances one runtime generation without wraparound. @param generation Current generation.
     * @return Next generation, IdentityInvalid, or GenerationExhausted.
     */
    [[nodiscard]] Result<DestructionGeneration> AdvanceDestructionGeneration(DestructionGeneration generation);
    /** @brief Advances one semantic state revision without wraparound. @param revision Current revision.
     * @return Next revision, IdentityInvalid, or RevisionExhausted.
     */
    [[nodiscard]] Result<DestructionStateRevision> AdvanceDestructionStateRevision(DestructionStateRevision revision);
    /** @brief Advances one fracture content revision without wraparound. @param revision Current revision.
     * @return Next revision, IdentityInvalid, or RevisionExhausted.
     */
    [[nodiscard]] Result<FractureContentRevision> AdvanceFractureContentRevision(FractureContentRevision revision);

    /**
     * @brief Validates a submitted handle against the exact current owner identity.
     * @param submitted Consumer-supplied handle.
     * @param current Owner's current handle.
     * @return Success, IdentityInvalid, IdentityUnknown, or StaleGeneration.
     * @post Success does not prove backing Scene/Physics/Render residency.
     */
    [[nodiscard]] Result<void> ValidateDestructionHandleAccess(DestructionHandle submitted, DestructionHandle current);
    /** @brief Validates exact fracture content against the current published content. @param submitted Candidate content.
     * @param current Current content. @return Success, IdentityInvalid, IdentityUnknown, or StaleContent.
     */
    [[nodiscard]] Result<void> ValidateFractureContentAccess(const FractureArtifactContentIdentity &submitted,
                                                             const FractureArtifactContentIdentity &current);
    /** @brief Validates a chunk's exact content fence. @param submitted Candidate chunk identity.
     * @param currentContent Current artifact content. @return Success or a typed identity/content failure.
     */
    [[nodiscard]] Result<void> ValidateFractureChunkAccess(const FractureChunkIdentity &submitted,
                                                           const FractureArtifactContentIdentity &currentContent);
    /** @brief Validates a command target generation. @param submitted Candidate command identity.
     * @param currentTarget Current target handle. @return Success or a typed identity/generation failure.
     */
    [[nodiscard]] Result<void> ValidateDestructionCommandAccess(const DestructionCommandId &submitted, DestructionHandle currentTarget);
    /** @brief Validates an event occurrence against current source and semantic revision.
     * @param submitted Candidate occurrence.
     * @param currentSource Current source handle.
     * @param currentRevision Current semantic revision.
     * @return Success, IdentityInvalid, IdentityUnknown, StaleGeneration, or StaleRevision.
     */
    [[nodiscard]] Result<void> ValidateDestructionEventAccess(const DestructionEventOccurrenceId &submitted,
                                                              DestructionHandle currentSource, DestructionStateRevision currentRevision);
}  // namespace Horo::Destruction
