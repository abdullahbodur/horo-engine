#include "Horo/Runtime/Save/SaveReference.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Horo::Runtime {
    namespace {
        template <typename Identity> [[nodiscard]] bool Valid(const Identity &identity) noexcept {
            return identity.IsValid();
        }

        [[nodiscard]] Error WireError(const ErrorCodeDescriptor &descriptor, const std::size_t byteOffset) {
            Error error = MakeError(descriptor);
            error.diagnostics.push_back(
                {DiagnosticCode{"save.reference.location"},
                 DiagnosticSeverity::Error,
                 std::string{descriptor.summary},
                 {"canonical/reference", 0,
                  static_cast<std::uint32_t>(std::min(byteOffset, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())))}});
            return error;
        }

        template <typename Identity> [[nodiscard]] Result<void> WriteIdentity(CanonicalValueWriter &writer, const Identity &identity) {
            for (const std::uint8_t byte : identity.Bytes()) {
                auto written = writer.WriteUInt8(byte);
                if (written.HasError())
                    return written;
            }
            return Result<void>::Success();
        }

        template <typename First, typename Second>
        [[nodiscard]] Result<void> WriteIdentityPair(CanonicalValueWriter &writer, const First &first, const Second &second) {
            auto written = WriteIdentity(writer, first);
            return written.HasError() ? written : WriteIdentity(writer, second);
        }

        [[nodiscard]] Result<void> WriteParticipant(CanonicalValueWriter &writer, const SaveParticipantId &participant) {
            return writer.WriteUtf8(participant.Value());
        }

        [[nodiscard]] constexpr SaveReferenceWireTag TagFor(const std::monostate &) noexcept {
            return SaveReferenceWireTag::Null;
        }

        [[nodiscard]] constexpr SaveReferenceWireTag TagFor(const SaveAssetReference &) noexcept {
            return SaveReferenceWireTag::Asset;
        }

        [[nodiscard]] constexpr SaveReferenceWireTag TagFor(const SaveSceneReference &) noexcept {
            return SaveReferenceWireTag::Scene;
        }

        [[nodiscard]] constexpr SaveReferenceWireTag TagFor(const SaveEntityReference &) noexcept {
            return SaveReferenceWireTag::Entity;
        }

        [[nodiscard]] constexpr SaveReferenceWireTag TagFor(const SavePrefabProvenance &) noexcept {
            return SaveReferenceWireTag::Prefab;
        }

        [[nodiscard]] constexpr SaveReferenceWireTag TagFor(const SaveParticipantReference &) noexcept {
            return SaveReferenceWireTag::Participant;
        }

        [[nodiscard]] constexpr SaveReferenceWireTag TagFor(const SaveRecordReference &) noexcept {
            return SaveReferenceWireTag::Record;
        }

        [[nodiscard]] Result<void> WritePayload(CanonicalValueWriter &, const std::monostate &) {
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> WritePayload(CanonicalValueWriter &writer, const SaveAssetReference &value) {
            return WriteIdentity(writer, value.asset);
        }

        [[nodiscard]] Result<void> WritePayload(CanonicalValueWriter &writer, const SaveSceneReference &value) {
            return WriteIdentityPair(writer, value.world, value.scene);
        }

        [[nodiscard]] Result<void> WritePayload(CanonicalValueWriter &writer, const SaveEntityReference &value) {
            return WriteIdentityPair(writer, value.world, value.entity);
        }

        [[nodiscard]] Result<void> WritePayload(CanonicalValueWriter &writer, const SavePrefabProvenance &value) {
            return WriteIdentityPair(writer, value.prefab, value.instance);
        }

        [[nodiscard]] Result<void> WritePayload(CanonicalValueWriter &writer, const SaveParticipantReference &value) {
            return WriteParticipant(writer, value.participant);
        }

        [[nodiscard]] Result<void> WritePayload(CanonicalValueWriter &writer, const SaveRecordReference &value) {
            auto written = WriteParticipant(writer, value.participant);
            return written.HasError() ? written : WriteIdentity(writer, value.record);
        }

        template <typename Identity> [[nodiscard]] Result<Identity> ReadIdentity(CanonicalValueReader &reader) {
            const std::size_t start = reader.ByteOffset();
            auto encoded = reader.ReadExactBytes(SaveIdentityDetail::Bytes{}.size());
            if (encoded.HasError())
                return Result<Identity>::Failure(std::move(encoded).ErrorValue());
            SaveIdentityDetail::Bytes bytes{};
            std::ranges::transform(encoded.Value(), bytes.begin(), [](const std::byte value) {
                return std::to_integer<std::uint8_t>(value);
            });
            auto identity = Identity::FromBytes(bytes);
            return identity.HasError() ? Result<Identity>::Failure(WireError(SaveErrors::ReferenceCorrupt, start)) : std::move(identity);
        }

        [[nodiscard]] Result<SaveParticipantId> ReadParticipant(CanonicalValueReader &reader, const std::size_t maximumBytes) {
            const std::size_t start = reader.ByteOffset();
            auto encoded = reader.ReadBytes(maximumBytes);
            if (encoded.HasError())
                return Result<SaveParticipantId>::Failure(std::move(encoded).ErrorValue());
            const std::string_view text{reinterpret_cast<const char *>(encoded.Value().data()), encoded.Value().size()};
            auto participant = SaveParticipantId::Parse(text);
            return participant.HasError() ? Result<SaveParticipantId>::Failure(WireError(SaveErrors::ReferenceCorrupt, start))
                                          : std::move(participant);
        }

        template <typename Target> [[nodiscard]] Result<SaveReferenceTarget> TargetResult(Target target) {
            return Result<SaveReferenceTarget>::Success(SaveReferenceTarget{std::move(target)});
        }

        [[nodiscard]] Result<SaveReferenceTarget> DecodeAsset(CanonicalValueReader &reader) {
            auto asset = ReadIdentity<SaveAssetId>(reader);
            return asset.HasError() ? Result<SaveReferenceTarget>::Failure(std::move(asset).ErrorValue())
                                    : TargetResult(SaveAssetReference{std::move(asset).Value()});
        }

        [[nodiscard]] Result<SaveReferenceTarget> DecodeScene(CanonicalValueReader &reader) {
            auto world = ReadIdentity<SaveWorldId>(reader);
            if (world.HasError())
                return Result<SaveReferenceTarget>::Failure(std::move(world).ErrorValue());
            auto scene = ReadIdentity<SaveBaseSceneId>(reader);
            return scene.HasError() ? Result<SaveReferenceTarget>::Failure(std::move(scene).ErrorValue())
                                    : TargetResult(SaveSceneReference{std::move(world).Value(), std::move(scene).Value()});
        }

        [[nodiscard]] Result<SaveReferenceTarget> DecodeEntity(CanonicalValueReader &reader) {
            auto world = ReadIdentity<SaveWorldId>(reader);
            if (world.HasError())
                return Result<SaveReferenceTarget>::Failure(std::move(world).ErrorValue());
            auto entity = ReadIdentity<PersistentEntityId>(reader);
            return entity.HasError() ? Result<SaveReferenceTarget>::Failure(std::move(entity).ErrorValue())
                                     : TargetResult(SaveEntityReference{std::move(world).Value(), std::move(entity).Value()});
        }

        [[nodiscard]] Result<SaveReferenceTarget> DecodePrefab(CanonicalValueReader &reader) {
            auto prefab = ReadIdentity<SaveAssetId>(reader);
            if (prefab.HasError())
                return Result<SaveReferenceTarget>::Failure(std::move(prefab).ErrorValue());
            auto instance = ReadIdentity<SavePrefabInstanceId>(reader);
            return instance.HasError() ? Result<SaveReferenceTarget>::Failure(std::move(instance).ErrorValue())
                                       : TargetResult(SavePrefabProvenance{std::move(prefab).Value(), std::move(instance).Value()});
        }

        [[nodiscard]] Result<SaveReferenceTarget> DecodeParticipant(CanonicalValueReader &reader, const std::size_t maximumBytes) {
            auto participant = ReadParticipant(reader, maximumBytes);
            return participant.HasError() ? Result<SaveReferenceTarget>::Failure(std::move(participant).ErrorValue())
                                          : TargetResult(SaveParticipantReference{std::move(participant).Value()});
        }

        [[nodiscard]] Result<SaveReferenceTarget> DecodeRecord(CanonicalValueReader &reader, const std::size_t maximumBytes) {
            auto participant = ReadParticipant(reader, maximumBytes);
            if (participant.HasError())
                return Result<SaveReferenceTarget>::Failure(std::move(participant).ErrorValue());
            auto record = ReadIdentity<SaveRecordId>(reader);
            return record.HasError() ? Result<SaveReferenceTarget>::Failure(std::move(record).ErrorValue())
                                     : TargetResult(SaveRecordReference{std::move(participant).Value(), std::move(record).Value()});
        }

        [[nodiscard]] Result<SaveReferenceTarget> DecodePayload(const SaveReferenceWireTag tag, CanonicalValueReader &reader,
                                                                const std::size_t maximumParticipantBytes) {
            switch (tag) {
                case SaveReferenceWireTag::Null:
                    return TargetResult(std::monostate{});
                case SaveReferenceWireTag::Asset:
                    return DecodeAsset(reader);
                case SaveReferenceWireTag::Scene:
                    return DecodeScene(reader);
                case SaveReferenceWireTag::Entity:
                    return DecodeEntity(reader);
                case SaveReferenceWireTag::Prefab:
                    return DecodePrefab(reader);
                case SaveReferenceWireTag::Participant:
                    return DecodeParticipant(reader, maximumParticipantBytes);
                case SaveReferenceWireTag::Record:
                    return DecodeRecord(reader, maximumParticipantBytes);
            }
            return Result<SaveReferenceTarget>::Failure(
                WireError(SaveErrors::ReferenceCorrupt, reader.ByteOffset() - sizeof(std::uint8_t)));
        }

        [[nodiscard]] Result<void> InvalidResolution() {
            return Result<void>::Failure(MakeError(SaveErrors::ReferenceResolutionInvalid));
        }
    }  // namespace

    /** @copydoc ValidateSaveReference */
    Result<void> ValidateSaveReference(const SaveReferenceTarget &target) {
        const bool valid = std::visit([](const auto &value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, std::monostate>)
                return true;
            else if constexpr (std::is_same_v<Value, SaveAssetReference>)
                return Valid(value.asset);
            else if constexpr (std::is_same_v<Value, SaveSceneReference>)
                return Valid(value.world) && Valid(value.scene);
            else if constexpr (std::is_same_v<Value, SaveEntityReference>)
                return Valid(value.world) && Valid(value.entity);
            else if constexpr (std::is_same_v<Value, SavePrefabProvenance>)
                return Valid(value.prefab) && Valid(value.instance);
            else if constexpr (std::is_same_v<Value, SaveParticipantReference>)
                return Valid(value.participant);
            else
                return Valid(value.participant) && Valid(value.record);
        }, target);
        return valid ? Result<void>::Success() : Result<void>::Failure(MakeError(SaveErrors::ReferenceInvalid));
    }

    /** @copydoc EncodeSaveReference */
    Result<CanonicalEncodedValue> EncodeSaveReference(const SaveReferenceTarget &target, const CanonicalCodecLimits limits) {
        auto validation = ValidateSaveReference(target);
        if (validation.HasError())
            return Result<CanonicalEncodedValue>::Failure(std::move(validation).ErrorValue());
        CanonicalValueWriter writer{limits};
        auto written = std::visit([&writer](const auto &value) {
            auto tag = writer.WriteUInt8(static_cast<std::uint8_t>(TagFor(value)));
            return tag.HasError() ? tag : WritePayload(writer, value);
        }, target);
        return written.HasError() ? Result<CanonicalEncodedValue>::Failure(std::move(written).ErrorValue()) : std::move(writer).Finalize();
    }

    /** @copydoc DecodeSaveReference */
    Result<SaveReferenceTarget> DecodeSaveReference(const std::span<const std::byte> bytes, const CanonicalCodecLimits limits) {
        auto created = CanonicalValueReader::Create(bytes, limits);
        if (created.HasError())
            return Result<SaveReferenceTarget>::Failure(std::move(created).ErrorValue());
        auto reader = std::move(created).Value();
        auto encodedTag = reader.ReadUInt8();
        if (encodedTag.HasError())
            return Result<SaveReferenceTarget>::Failure(std::move(encodedTag).ErrorValue());
        const auto tag = static_cast<SaveReferenceWireTag>(encodedTag.Value());
        const auto maximumParticipantBytes = std::min(limits.maximumStringBytes, MaximumSaveParticipantIdBytes);
        auto target = DecodePayload(tag, reader, maximumParticipantBytes);
        if (target.HasError())
            return target;
        auto finished = reader.RequireFinished();
        return finished.HasError() ? Result<SaveReferenceTarget>::Failure(std::move(finished).ErrorValue()) : std::move(target);
    }

    /** @copydoc ValidateSaveReferenceResolution */
    Result<void> ValidateSaveReferenceResolution(const SaveReferenceResolution &resolution) {
        auto original = ValidateSaveReference(resolution.original);
        if (original.HasError())
            return original;
        const bool hasContext = !std::holds_alternative<std::monostate>(resolution.original);
        switch (resolution.disposition) {
            case SaveReferenceDisposition::Resolved:
                return resolution.replacement ? InvalidResolution() : Result<void>::Success();
            case SaveReferenceDisposition::Missing:
            case SaveReferenceDisposition::Deferred:
                return hasContext && !resolution.replacement ? Result<void>::Success() : InvalidResolution();
            case SaveReferenceDisposition::Remapped:
                break;
            default:
                return InvalidResolution();
        }
        if (!hasContext || !resolution.replacement || std::holds_alternative<std::monostate>(*resolution.replacement))
            return InvalidResolution();
        auto replacement = ValidateSaveReference(*resolution.replacement);
        if (replacement.HasError() || resolution.original.index() != resolution.replacement->index() ||
            resolution.original == *resolution.replacement)
            return InvalidResolution();
        return Result<void>::Success();
    }
}  // namespace Horo::Runtime
