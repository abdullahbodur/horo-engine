#include "Horo/Runtime/Save/SaveReference.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <array>
#include <type_traits>

namespace Horo::Runtime {
    namespace {
        enum class ReferenceKind : std::uint8_t {
            Null,
            Asset,
            Scene,
            Entity,
            Prefab,
            Participant,
            Record
        };

        template <typename Identity> bool Valid(const Identity &identity) noexcept {
            return identity.IsValid();
        }

        template <typename Identity> CanonicalCodecResult<void> WriteIdentity(CanonicalValueWriter &writer, const Identity &identity) {
            return writer.WriteBytes(std::as_bytes(std::span{identity.Bytes()}));
        }

        CanonicalCodecResult<void> WriteParticipant(CanonicalValueWriter &writer, const SaveParticipantId &participant) {
            return writer.WriteUtf8(participant.Value());
        }

        template <typename Identity> CanonicalCodecResult<Identity> ReadIdentity(CanonicalValueReader &reader) {
            auto encoded = reader.ReadBytes();
            if (encoded.HasError())
                return CanonicalCodecResult<Identity>::Failure(encoded.ErrorValue());
            if (encoded.Value().size() != SaveIdentityDetail::Bytes{}.size())
                return CanonicalCodecResult<Identity>::Failure(
                    {.error = MakeError(SaveErrors::ReferenceInvalid), .context = {.byteOffset = 0}});
            SaveIdentityDetail::Bytes bytes{};
            for (std::size_t index = 0; index < bytes.size(); ++index)
                bytes[index] = std::to_integer<std::uint8_t>(encoded.Value()[index]);
            auto identity = Identity::FromBytes(bytes);
            if (identity.HasError())
                return CanonicalCodecResult<Identity>::Failure(
                    {.error = MakeError(SaveErrors::ReferenceInvalid), .context = {.byteOffset = 0}});
            return CanonicalCodecResult<Identity>::Success(std::move(identity).Value());
        }

        CanonicalCodecResult<SaveParticipantId> ReadParticipant(CanonicalValueReader &reader) {
            auto text = reader.ReadUtf8();
            if (text.HasError())
                return CanonicalCodecResult<SaveParticipantId>::Failure(text.ErrorValue());
            auto participant = SaveParticipantId::Parse(text.Value());
            if (participant.HasError())
                return CanonicalCodecResult<SaveParticipantId>::Failure(
                    {.error = MakeError(SaveErrors::ReferenceInvalid), .context = {.byteOffset = 0}});
            return CanonicalCodecResult<SaveParticipantId>::Success(std::move(participant).Value());
        }

        CanonicalCodecFailure RootFailure(Error error) {
            return {.error = std::move(error), .context = {}};
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
    CanonicalCodecResult<std::vector<std::byte>> EncodeSaveReference(const SaveReferenceTarget &target, const CanonicalCodecLimits limits) {
        auto validation = ValidateSaveReference(target);
        if (validation.HasError())
            return CanonicalCodecResult<std::vector<std::byte>>::Failure(RootFailure(validation.ErrorValue()));
        CanonicalValueWriter writer{limits};
        auto encoded = std::visit([&writer](const auto &value) -> CanonicalCodecResult<void> {
            using Value = std::decay_t<decltype(value)>;
            ReferenceKind kind{};
            if constexpr (std::is_same_v<Value, std::monostate>)
                kind = ReferenceKind::Null;
            else if constexpr (std::is_same_v<Value, SaveAssetReference>)
                kind = ReferenceKind::Asset;
            else if constexpr (std::is_same_v<Value, SaveSceneReference>)
                kind = ReferenceKind::Scene;
            else if constexpr (std::is_same_v<Value, SaveEntityReference>)
                kind = ReferenceKind::Entity;
            else if constexpr (std::is_same_v<Value, SavePrefabProvenance>)
                kind = ReferenceKind::Prefab;
            else if constexpr (std::is_same_v<Value, SaveParticipantReference>)
                kind = ReferenceKind::Participant;
            else
                kind = ReferenceKind::Record;
            auto result = writer.WriteUInt8(static_cast<std::uint8_t>(kind));
            if (result.HasError() || std::is_same_v<Value, std::monostate>)
                return result;
            if constexpr (std::is_same_v<Value, SaveAssetReference>)
                return WriteIdentity(writer, value.asset);
            else if constexpr (std::is_same_v<Value, SaveSceneReference>) {
                result = WriteIdentity(writer, value.world);
                return result.HasError() ? result : WriteIdentity(writer, value.scene);
            } else if constexpr (std::is_same_v<Value, SaveEntityReference>) {
                result = WriteIdentity(writer, value.world);
                return result.HasError() ? result : WriteIdentity(writer, value.entity);
            } else if constexpr (std::is_same_v<Value, SavePrefabProvenance>) {
                result = WriteIdentity(writer, value.prefab);
                return result.HasError() ? result : WriteIdentity(writer, value.instance);
            } else if constexpr (std::is_same_v<Value, SaveParticipantReference>)
                return WriteParticipant(writer, value.participant);
            else if constexpr (std::is_same_v<Value, SaveRecordReference>) {
                result = WriteParticipant(writer, value.participant);
                return result.HasError() ? result : WriteIdentity(writer, value.record);
            }
            return result;
        }, target);
        if (encoded.HasError())
            return CanonicalCodecResult<std::vector<std::byte>>::Failure(encoded.ErrorValue());
        return CanonicalCodecResult<std::vector<std::byte>>::Success(std::move(writer).TakeBytes());
    }

    /** @copydoc DecodeSaveReference */
    CanonicalCodecResult<SaveReferenceTarget> DecodeSaveReference(const std::span<const std::byte> bytes,
                                                                  const CanonicalCodecLimits limits) {
        CanonicalValueReader reader{bytes, limits};
        auto encodedKind = reader.ReadUInt8();
        if (encodedKind.HasError())
            return CanonicalCodecResult<SaveReferenceTarget>::Failure(encodedKind.ErrorValue());
        const auto kind = static_cast<ReferenceKind>(encodedKind.Value());
        SaveReferenceTarget target;
        if (kind == ReferenceKind::Null)
            target = std::monostate{};
        else if (kind == ReferenceKind::Asset) {
            auto asset = ReadIdentity<SaveAssetId>(reader);
            if (asset.HasError())
                return CanonicalCodecResult<SaveReferenceTarget>::Failure(asset.ErrorValue());
            target = SaveAssetReference{std::move(asset).Value()};
        } else if (kind == ReferenceKind::Scene) {
            auto world = ReadIdentity<SaveWorldId>(reader);
            auto scene = ReadIdentity<SaveBaseSceneId>(reader);
            if (world.HasError())
                return CanonicalCodecResult<SaveReferenceTarget>::Failure(world.ErrorValue());
            if (scene.HasError())
                return CanonicalCodecResult<SaveReferenceTarget>::Failure(scene.ErrorValue());
            target = SaveSceneReference{std::move(world).Value(), std::move(scene).Value()};
        } else if (kind == ReferenceKind::Entity) {
            auto world = ReadIdentity<SaveWorldId>(reader);
            auto entity = ReadIdentity<PersistentEntityId>(reader);
            if (world.HasError())
                return CanonicalCodecResult<SaveReferenceTarget>::Failure(world.ErrorValue());
            if (entity.HasError())
                return CanonicalCodecResult<SaveReferenceTarget>::Failure(entity.ErrorValue());
            target = SaveEntityReference{std::move(world).Value(), std::move(entity).Value()};
        } else if (kind == ReferenceKind::Prefab) {
            auto prefab = ReadIdentity<SaveAssetId>(reader);
            auto instance = ReadIdentity<SavePrefabInstanceId>(reader);
            if (prefab.HasError())
                return CanonicalCodecResult<SaveReferenceTarget>::Failure(prefab.ErrorValue());
            if (instance.HasError())
                return CanonicalCodecResult<SaveReferenceTarget>::Failure(instance.ErrorValue());
            target = SavePrefabProvenance{std::move(prefab).Value(), std::move(instance).Value()};
        } else if (kind == ReferenceKind::Participant || kind == ReferenceKind::Record) {
            auto participant = ReadParticipant(reader);
            if (participant.HasError())
                return CanonicalCodecResult<SaveReferenceTarget>::Failure(participant.ErrorValue());
            if (kind == ReferenceKind::Participant)
                target = SaveParticipantReference{std::move(participant).Value()};
            else {
                auto record = ReadIdentity<SaveRecordId>(reader);
                if (record.HasError())
                    return CanonicalCodecResult<SaveReferenceTarget>::Failure(record.ErrorValue());
                target = SaveRecordReference{std::move(participant).Value(), std::move(record).Value()};
            }
        } else
            return CanonicalCodecResult<SaveReferenceTarget>::Failure(RootFailure(MakeError(SaveErrors::ReferenceInvalid)));
        auto finished = reader.RequireFinished();
        if (finished.HasError())
            return CanonicalCodecResult<SaveReferenceTarget>::Failure(finished.ErrorValue());
        return CanonicalCodecResult<SaveReferenceTarget>::Success(std::move(target));
    }

    /** @copydoc ValidateSaveReferenceResolution */
    Result<void> ValidateSaveReferenceResolution(const SaveReferenceResolution &resolution) {
        auto original = ValidateSaveReference(resolution.original);
        if (original.HasError())
            return original;
        const bool hasReplacement = resolution.replacement.has_value();
        const bool requiresReplacement = resolution.disposition == SaveReferenceDisposition::Remapped;
        const bool knownDisposition =
            resolution.disposition >= SaveReferenceDisposition::Resolved && resolution.disposition <= SaveReferenceDisposition::Deferred;
        if (!knownDisposition || hasReplacement != requiresReplacement)
            return Result<void>::Failure(MakeError(SaveErrors::ReferenceResolutionInvalid));
        if (hasReplacement) {
            auto replacement = ValidateSaveReference(*resolution.replacement);
            if (replacement.HasError() || std::holds_alternative<std::monostate>(*resolution.replacement))
                return Result<void>::Failure(MakeError(SaveErrors::ReferenceResolutionInvalid));
        }
        return Result<void>::Success();
    }
}  // namespace Horo::Runtime
