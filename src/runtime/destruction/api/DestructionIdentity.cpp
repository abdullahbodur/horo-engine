#include "Horo/Destruction/DestructionIdentity.h"

#include <algorithm>
#include <limits>
#include <numeric>

namespace Horo::Destruction {
    namespace {
        template <std::size_t Size>
        void WriteNetworkValue(std::array<std::uint8_t, Size> &bytes, const std::size_t offset, std::uint64_t value,
                               const std::size_t width) noexcept {
            for (std::size_t remaining = width; remaining > 0; --remaining) {
                bytes[offset + remaining - 1U] = static_cast<std::uint8_t>(value);
                value >>= 8U;
            }
        }

        template <std::size_t Size>
        [[nodiscard]] std::uint64_t ReadNetworkValue(const std::array<std::uint8_t, Size> &bytes, const std::size_t offset,
                                                     const std::size_t width) noexcept {
            const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
            const auto last = first + static_cast<std::ptrdiff_t>(width);
            return std::accumulate(first, last, std::uint64_t{}, [](const std::uint64_t accumulated, const std::uint8_t byte) {
                return (accumulated << 8U) | byte;
            });
        }

        template <typename Identity, std::size_t Size>
        [[nodiscard]] Result<Identity> DecodeIdentity(const std::array<std::uint8_t, Size> &bytes, const std::size_t offset) {
            const auto decoded = Identity::Create(ReadNetworkValue(bytes, offset, sizeof(std::uint64_t)));
            if (!decoded.HasValue())
                return Result<Identity>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
            return decoded;
        }

        [[nodiscard]] bool HasDigestValue(const Sha256Digest &digest) noexcept {
            return std::ranges::any_of(digest.bytes, [](const std::uint8_t byte) {
                return byte != 0;
            });
        }

        template <typename Identity>
        [[nodiscard]] Result<Identity> AdvanceIdentity(const Identity current, const ErrorCodeDescriptor &exhausted) {
            if (!current.IsValid())
                return Result<Identity>::Failure(MakeError(DestructionErrors::IdentityInvalid));
            if (current.Value() == std::numeric_limits<std::uint64_t>::max())
                return Result<Identity>::Failure(MakeError(exhausted));
            return Identity::Create(current.Value() + 1U);
        }

        template <std::size_t DestinationSize, std::size_t SourceSize>
        void CopyBytes(std::array<std::uint8_t, DestinationSize> &destination, const std::size_t offset,
                       const std::array<std::uint8_t, SourceSize> &source) noexcept {
            std::ranges::copy(source, destination.begin() + static_cast<std::ptrdiff_t>(offset));
        }

        template <std::size_t DestinationSize, std::size_t SourceSize>
        [[nodiscard]] std::array<std::uint8_t, DestinationSize> ReadBytes(const std::array<std::uint8_t, SourceSize> &source,
                                                                          const std::size_t offset) noexcept {
            std::array<std::uint8_t, DestinationSize> destination{};
            std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset), DestinationSize, destination.begin());
            return destination;
        }

        template <std::size_t Size>
        void WriteHandle(std::array<std::uint8_t, Size> &bytes, const std::size_t offset, const DestructionHandle handle) noexcept {
            WriteNetworkValue(bytes, offset, handle.world.Value(), 8);
            WriteNetworkValue(bytes, offset + 8, handle.destructible.Value(), 8);
            WriteNetworkValue(bytes, offset + 16, handle.generation.Value(), 8);
        }

        template <std::size_t Size>
        [[nodiscard]] Result<DestructionHandle> ReadHandle(const std::array<std::uint8_t, Size> &bytes, const std::size_t offset) {
            auto world = DecodeIdentity<DestructionWorldId>(bytes, offset);
            auto destructible = DecodeIdentity<DestructibleId>(bytes, offset + 8);
            auto generation = DecodeIdentity<DestructionGeneration>(bytes, offset + 16);
            if (world.HasError() || destructible.HasError() || generation.HasError())
                return Result<DestructionHandle>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
            return Result<DestructionHandle>::Success({world.Value(), destructible.Value(), generation.Value()});
        }
    }  // namespace

    /** @copydoc FractureAssetId::Create */
    Result<FractureAssetId> FractureAssetId::Create(const Assets::AssetId &asset) {
        if (!asset.IsValid())
            return Result<FractureAssetId>::Failure(MakeError(DestructionErrors::IdentityInvalid));
        return Result<FractureAssetId>::Success(FractureAssetId{asset});
    }

    /** @copydoc FractureAssetId::Asset */
    const Assets::AssetId &FractureAssetId::Asset() const noexcept {
        return asset_;
    }

    /** @copydoc FractureAssetId::IsValid */
    bool FractureAssetId::IsValid() const noexcept {
        return asset_.IsValid();
    }

    /** @copydoc FractureArtifactContentIdentity::Create */
    Result<FractureArtifactContentIdentity> FractureArtifactContentIdentity::Create(const FractureAssetId asset,
                                                                                    const FractureContentRevision revision,
                                                                                    const Sha256Digest &semanticDigest) {
        if (!asset.IsValid() || !revision.IsValid() || !HasDigestValue(semanticDigest))
            return Result<FractureArtifactContentIdentity>::Failure(MakeError(DestructionErrors::IdentityInvalid));
        return Result<FractureArtifactContentIdentity>::Success({asset, revision, semanticDigest});
    }

    /** @copydoc FractureArtifactContentIdentity::SemanticDigest */
    const Sha256Digest &FractureArtifactContentIdentity::SemanticDigest() const noexcept {
        return semanticDigest_;
    }

    /** @copydoc FractureArtifactContentIdentity::IsValid */
    bool FractureArtifactContentIdentity::IsValid() const noexcept {
        return asset_.IsValid() && revision_.IsValid() && HasDigestValue(semanticDigest_);
    }

    /** @copydoc SerializeFractureAssetId */
    SerializedFractureAssetId SerializeFractureAssetId(const FractureAssetId &asset) noexcept {
        return asset.Asset().Bytes();
    }

    /** @copydoc DeserializeFractureAssetId */
    Result<FractureAssetId> DeserializeFractureAssetId(const SerializedFractureAssetId &bytes) {
        auto asset = FractureAssetId::Create(Assets::AssetId::FromBytes(bytes));
        if (asset.HasError())
            return Result<FractureAssetId>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
        return asset;
    }

    /** @copydoc SerializeFractureArtifactContentIdentity */
    SerializedFractureArtifactContentIdentity SerializeFractureArtifactContentIdentity(
        const FractureArtifactContentIdentity &content) noexcept {
        SerializedFractureArtifactContentIdentity bytes{};
        CopyBytes(bytes, 0, SerializeFractureAssetId(content.Asset()));
        WriteNetworkValue(bytes, 16, content.Revision().Value(), 8);
        CopyBytes(bytes, 24, content.SemanticDigest().bytes);
        return bytes;
    }

    /** @copydoc DeserializeFractureArtifactContentIdentity */
    Result<FractureArtifactContentIdentity> DeserializeFractureArtifactContentIdentity(
        const SerializedFractureArtifactContentIdentity &bytes) {
        auto asset = DeserializeFractureAssetId(ReadBytes<16>(bytes, 0));
        auto revision = DecodeIdentity<FractureContentRevision>(bytes, 16);
        const Sha256Digest digest{ReadBytes<32>(bytes, 24)};
        if (asset.HasError() || revision.HasError())
            return Result<FractureArtifactContentIdentity>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
        auto content = FractureArtifactContentIdentity::Create(asset.Value(), revision.Value(), digest);
        if (content.HasError())
            return Result<FractureArtifactContentIdentity>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
        return content;
    }

    /** @copydoc SerializeFractureChunkIdentity */
    SerializedFractureChunkIdentity SerializeFractureChunkIdentity(const FractureChunkIdentity &chunk) noexcept {
        SerializedFractureChunkIdentity bytes{};
        CopyBytes(bytes, 0, SerializeFractureArtifactContentIdentity(chunk.content));
        WriteNetworkValue(bytes, 56, chunk.chunk.Value(), 8);
        return bytes;
    }

    /** @copydoc DeserializeFractureChunkIdentity */
    Result<FractureChunkIdentity> DeserializeFractureChunkIdentity(const SerializedFractureChunkIdentity &bytes) {
        auto content = DeserializeFractureArtifactContentIdentity(ReadBytes<56>(bytes, 0));
        auto chunk = DecodeIdentity<DestructionChunkId>(bytes, 56);
        if (content.HasError() || chunk.HasError())
            return Result<FractureChunkIdentity>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
        return Result<FractureChunkIdentity>::Success({content.Value(), chunk.Value()});
    }

    /** @copydoc SerializeDestructionHandle */
    SerializedDestructionHandle SerializeDestructionHandle(const DestructionHandle handle) noexcept {
        SerializedDestructionHandle bytes{};
        WriteHandle(bytes, 0, handle);
        return bytes;
    }

    /** @copydoc DeserializeDestructionHandle */
    Result<DestructionHandle> DeserializeDestructionHandle(const SerializedDestructionHandle &bytes) {
        return ReadHandle(bytes, 0);
    }

    /** @copydoc SerializeDestructionCommandId */
    SerializedDestructionCommandId SerializeDestructionCommandId(const DestructionCommandId command) noexcept {
        SerializedDestructionCommandId bytes{};
        WriteHandle(bytes, 0, command.target);
        WriteNetworkValue(bytes, 24, command.value.Value(), 8);
        return bytes;
    }

    /** @copydoc DeserializeDestructionCommandId */
    Result<DestructionCommandId> DeserializeDestructionCommandId(const SerializedDestructionCommandId &bytes) {
        auto target = ReadHandle(bytes, 0);
        auto value = DecodeIdentity<DestructionCommandValue>(bytes, 24);
        if (target.HasError() || value.HasError())
            return Result<DestructionCommandId>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
        return Result<DestructionCommandId>::Success({target.Value(), value.Value()});
    }

    /** @copydoc SerializeDestructionEventOccurrenceId */
    SerializedDestructionEventOccurrenceId SerializeDestructionEventOccurrenceId(const DestructionEventOccurrenceId &occurrence) noexcept {
        SerializedDestructionEventOccurrenceId bytes{};
        WriteHandle(bytes, 0, occurrence.source);
        WriteNetworkValue(bytes, 24, occurrence.stateRevision.Value(), 8);
        bytes[32] = static_cast<std::uint8_t>(occurrence.kind);
        WriteNetworkValue(bytes, 33, occurrence.revisionOrdinal, 4);
        return bytes;
    }

    /** @copydoc DeserializeDestructionEventOccurrenceId */
    Result<DestructionEventOccurrenceId> DeserializeDestructionEventOccurrenceId(const SerializedDestructionEventOccurrenceId &bytes) {
        auto source = ReadHandle(bytes, 0);
        auto revision = DecodeIdentity<DestructionStateRevision>(bytes, 24);
        const auto kind = static_cast<DestructionFactKind>(bytes[32]);
        const auto ordinal = static_cast<std::uint32_t>(ReadNetworkValue(bytes, 33, 4));
        if (source.HasError() || revision.HasError())
            return Result<DestructionEventOccurrenceId>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
        const DestructionEventOccurrenceId occurrence{source.Value(), revision.Value(), kind, ordinal};
        if (!occurrence.IsValid())
            return Result<DestructionEventOccurrenceId>::Failure(MakeError(DestructionErrors::SerializedIdentityInvalid));
        return Result<DestructionEventOccurrenceId>::Success(occurrence);
    }

    /** @copydoc AdvanceDestructionGeneration */
    Result<DestructionGeneration> AdvanceDestructionGeneration(const DestructionGeneration generation) {
        return AdvanceIdentity(generation, DestructionErrors::GenerationExhausted);
    }

    /** @copydoc AdvanceDestructionStateRevision */
    Result<DestructionStateRevision> AdvanceDestructionStateRevision(const DestructionStateRevision revision) {
        return AdvanceIdentity(revision, DestructionErrors::RevisionExhausted);
    }

    /** @copydoc AdvanceFractureContentRevision */
    Result<FractureContentRevision> AdvanceFractureContentRevision(const FractureContentRevision revision) {
        return AdvanceIdentity(revision, DestructionErrors::RevisionExhausted);
    }

    /** @copydoc ValidateDestructionHandleAccess */
    Result<void> ValidateDestructionHandleAccess(const DestructionHandle submitted, const DestructionHandle current) {
        if (!submitted.IsValid() || !current.IsValid())
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityInvalid));
        if (submitted.world != current.world || submitted.destructible != current.destructible)
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityUnknown));
        if (submitted.generation != current.generation)
            return Result<void>::Failure(MakeError(DestructionErrors::StaleGeneration));
        return Result<void>::Success();
    }

    /** @copydoc ValidateFractureContentAccess */
    Result<void> ValidateFractureContentAccess(const FractureArtifactContentIdentity &submitted,
                                               const FractureArtifactContentIdentity &current) {
        if (!submitted.IsValid() || !current.IsValid())
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityInvalid));
        if (submitted.Asset() != current.Asset())
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityUnknown));
        if (submitted.Revision() != current.Revision() || submitted.SemanticDigest() != current.SemanticDigest())
            return Result<void>::Failure(MakeError(DestructionErrors::StaleContent));
        return Result<void>::Success();
    }

    /** @copydoc ValidateFractureChunkAccess */
    Result<void> ValidateFractureChunkAccess(const FractureChunkIdentity &submitted,
                                             const FractureArtifactContentIdentity &currentContent) {
        if (!submitted.IsValid() || !currentContent.IsValid())
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityInvalid));
        return ValidateFractureContentAccess(submitted.content, currentContent);
    }

    /** @copydoc ValidateDestructionCommandAccess */
    Result<void> ValidateDestructionCommandAccess(const DestructionCommandId submitted, const DestructionHandle currentTarget) {
        if (!submitted.IsValid())
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityInvalid));
        return ValidateDestructionHandleAccess(submitted.target, currentTarget);
    }

    /** @copydoc ValidateDestructionEventAccess */
    Result<void> ValidateDestructionEventAccess(const DestructionEventOccurrenceId &submitted, const DestructionHandle currentSource,
                                                const DestructionStateRevision currentRevision) {
        if (!submitted.IsValid() || !currentRevision.IsValid())
            return Result<void>::Failure(MakeError(DestructionErrors::IdentityInvalid));
        auto handle = ValidateDestructionHandleAccess(submitted.source, currentSource);
        if (handle.HasError())
            return handle;
        if (submitted.stateRevision != currentRevision)
            return Result<void>::Failure(MakeError(DestructionErrors::StaleRevision));
        return Result<void>::Success();
    }
}  // namespace Horo::Destruction
