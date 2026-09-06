#include "Horo/Runtime/Save/SaveArchiveMetadata.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>

namespace Horo::Runtime {
    namespace {
        using Json = nlohmann::json;
        using OrderedJson = nlohmann::ordered_json;

        constexpr std::size_t kMaximumMetadataDepth = 8;

        /** @brief Rejects duplicate object keys and excessive nesting before values enter the DOM. */
        struct MetadataDecodeGuard final {
            std::array<std::set<std::string, std::less<>>, kMaximumMetadataDepth + 1> keys;
            bool valid{true};

            bool operator()(const int depth, const Json::parse_event_t event, const Json &value) {
                if (depth < 0 || static_cast<std::size_t>(depth) >= kMaximumMetadataDepth) {
                    valid = false;
                    return false;
                }
                const auto index = static_cast<std::size_t>(depth);
                if (event == Json::parse_event_t::object_start)
                    keys[index + 1].clear();
                else if (event == Json::parse_event_t::key)
                    valid &= keys[index].insert(value.get<std::string>()).second;
                return valid;
            }
        };

        /** @brief Reports whether an object contains exactly the named fields. */
        [[nodiscard]] bool HasExactFields(const Json &value, const std::initializer_list<std::string_view> fields) {
            if (!value.is_object() || value.size() != fields.size())
                return false;
            return std::ranges::all_of(fields, [&value](const std::string_view field) {
                return value.contains(field);
            });
        }

        /** @brief Validates finite nonzero decode limits and their aggregate relationship. */
        [[nodiscard]] bool HasValidLimits(const SaveArchiveMetadataLimits &limits) noexcept {
            return limits.maximumHeaderBytes != 0 && limits.maximumManifestBytes != 0 && limits.maximumTextBytes != 0 &&
                   limits.maximumParticipants != 0 && limits.maximumChunksPerParticipant != 0 && limits.maximumTotalChunks != 0 &&
                   limits.maximumChunksPerParticipant <= limits.maximumTotalChunks;
        }

        /** @brief Parses a required canonical persistent identity string. */
        template <typename Identity> [[nodiscard]] Result<Identity> ParseIdentity(const Json &value) {
            if (!value.is_string())
                return Result<Identity>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));
            auto parsed = Identity::Parse(value.get_ref<const std::string &>());
            if (parsed.HasError())
                return Result<Identity>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));
            return parsed;
        }

        /** @brief Parses one required nonzero save version from an unsigned JSON integer. */
        template <typename Version> [[nodiscard]] Result<Version> ParseVersion(const Json &value) {
            if (!value.is_number_unsigned() || value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                return Result<Version>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            auto parsed = Version::Create(static_cast<std::uint32_t>(value.get<std::uint64_t>()));
            if (parsed.HasError())
                return Result<Version>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            return parsed;
        }

        /** @brief Parses a bounded required provenance string. */
        [[nodiscard]] Result<std::string> ParseText(const Json &value, const SaveArchiveMetadataLimits &limits) {
            if (!value.is_string())
                return Result<std::string>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));
            const auto &text = value.get_ref<const std::string &>();
            if (text.empty() || text.size() > limits.maximumTextBytes)
                return Result<std::string>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
            return Result<std::string>::Success(text);
        }

        /** @brief Parses untrusted JSON with duplicate-key and nesting protection. */
        [[nodiscard]] Result<Json> ParseMetadataJson(const std::string_view text, const ErrorCodeDescriptor &invalidError) {
            try {
                MetadataDecodeGuard guard;
                auto value = Json::parse(text, std::ref(guard));
                if (!guard.valid)
                    return Result<Json>::Failure(MakeError(invalidError));
                return Result<Json>::Success(std::move(value));
            } catch (const Json::exception &) {
                return Result<Json>::Failure(MakeError(invalidError));
            }
        }

        /** @brief Parses and validates one manifest participant entry. */
        [[nodiscard]] Result<SaveManifestParticipant> DecodeParticipant(const Json &value, const SaveArchiveMetadataLimits &limits,
                                                                        std::size_t &totalChunks) {
            if (!HasExactFields(value, {"chunks", "participant", "required", "schemaVersion"}) || !value.at("participant").is_string() ||
                !value.at("required").is_boolean() || !value.at("chunks").is_array())
                return Result<SaveManifestParticipant>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            const auto &chunks = value.at("chunks");
            if (chunks.empty() || chunks.size() > limits.maximumChunksPerParticipant ||
                chunks.size() > limits.maximumTotalChunks - totalChunks)
                return Result<SaveManifestParticipant>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));

            auto participant = SaveParticipantId::Parse(value.at("participant").get_ref<const std::string &>());
            auto schema = ParseVersion<ParticipantSchemaVersion>(value.at("schemaVersion"));
            if (participant.HasError() || schema.HasError())
                return Result<SaveManifestParticipant>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));

            std::vector<SaveRecordId> decodedChunks;
            decodedChunks.reserve(chunks.size());
            for (const Json &chunk : chunks) {
                auto decoded = ParseIdentity<SaveRecordId>(chunk);
                if (decoded.HasError())
                    return Result<SaveManifestParticipant>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
                decodedChunks.push_back(std::move(decoded).Value());
            }
            totalChunks += decodedChunks.size();
            return Result<SaveManifestParticipant>::Success({.participant = std::move(participant).Value(),
                                                             .schemaVersion = std::move(schema).Value(),
                                                             .required = value.at("required").get<bool>(),
                                                             .chunks = std::move(decodedChunks)});
        }

        enum class VersionAdmission : std::uint8_t {
            Direct,
            Migration,
            Rejected,
        };

        /** @brief Classifies one independent version without conflating its schema authority. */
        template <typename Tag>
        [[nodiscard]] VersionAdmission ClassifyVersion(const SaveVersion<Tag> version, const SaveVersionSupport<Tag> &support) noexcept {
            if (support.direct.Contains(version))
                return VersionAdmission::Direct;
            if (support.migrationSource && support.migrationSource->Contains(version))
                return VersionAdmission::Migration;
            return VersionAdmission::Rejected;
        }

        /** @brief Reports whether one version support declaration has valid, non-overlapping ranges. */
        template <typename Tag> [[nodiscard]] bool IsValidSupport(const SaveVersionSupport<Tag> &support) noexcept {
            if (!support.direct.minimum.IsValid() || !support.direct.maximum.IsValid() || support.direct.minimum > support.direct.maximum)
                return false;
            return !support.migrationSource || (support.migrationSource->minimum.IsValid() && support.migrationSource->maximum.IsValid() &&
                                                support.migrationSource->minimum <= support.migrationSource->maximum);
        }

        /** @brief Creates a rejected compatibility decision. */
        [[nodiscard]] SaveCompatibilityDecision Reject(const SaveCompatibilityReason reason,
                                                       std::optional<SaveParticipantId> participant = std::nullopt) {
            return {.disposition = SaveCompatibilityDisposition::Rejected, .reason = reason, .participant = std::move(participant)};
        }
    }  // namespace

    /** @copydoc ValidateSaveArchiveHeader */
    Result<void> ValidateSaveArchiveHeader(const SaveArchiveHeader &header, const SaveArchiveMetadataLimits &limits) {
        if (!HasValidLimits(limits))
            return Result<void>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
        const bool identitiesValid = header.slot.IsValid() && header.slotGeneration.IsValid() && header.productCompatibility.IsValid() &&
                                     header.product.IsValid() && header.environment.IsValid() && header.user.IsValid() &&
                                     header.profile.IsValid() && header.project.IsValid() && header.world.IsValid() &&
                                     header.baseScene.IsValid();
        if (!identitiesValid || (header.parentGeneration && !header.parentGeneration->IsValid()) ||
            (header.parentGeneration && *header.parentGeneration == header.slotGeneration) || header.capturedAtUnixMilliseconds == 0 ||
            (header.featureFlags & ~limits.supportedFeatureFlagsMask) != 0)
            return Result<void>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));
        if (header.engineVersion.empty() || header.projectBuildId.empty() || header.engineVersion.size() > limits.maximumTextBytes ||
            header.projectBuildId.size() > limits.maximumTextBytes)
            return Result<void>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
        return Result<void>::Success();
    }

    /** @copydoc ValidateSaveGameManifest */
    Result<void> ValidateSaveGameManifest(const SaveGameManifest &manifest, const SaveArchiveMetadataLimits &limits) {
        if (!HasValidLimits(limits) || manifest.participants.size() > limits.maximumParticipants)
            return Result<void>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
        if (!manifest.saveSchemaVersion.IsValid() || manifest.participants.empty())
            return Result<void>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
        std::size_t totalChunks = 0;
        const SaveParticipantId *previousParticipant = nullptr;
        for (const SaveManifestParticipant &entry : manifest.participants) {
            if (!entry.participant.IsValid() || !entry.schemaVersion.IsValid() || entry.chunks.empty() ||
                entry.chunks.size() > limits.maximumChunksPerParticipant || entry.chunks.size() > limits.maximumTotalChunks - totalChunks)
                return Result<void>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            if (previousParticipant != nullptr && *previousParticipant >= entry.participant)
                return Result<void>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            if (const auto unique = ValidateUniqueSaveIdentities<SaveRecordIdentityTag>(entry.chunks);
                unique.HasError() || !std::ranges::is_sorted(entry.chunks))
                return Result<void>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            totalChunks += entry.chunks.size();
            previousParticipant = &entry.participant;
        }
        return Result<void>::Success();
    }

    /** @copydoc EncodeSaveArchiveHeader */
    Result<std::string> EncodeSaveArchiveHeader(const SaveArchiveHeader &header, const SaveArchiveMetadataLimits &limits) {
        if (const auto valid = ValidateSaveArchiveHeader(header, limits); valid.HasError())
            return Result<std::string>::Failure(valid.ErrorValue());
        OrderedJson root;
        root["baseScene"] = header.baseScene.ToString();
        root["capturedAtUnixMilliseconds"] = header.capturedAtUnixMilliseconds;
        root["engineVersion"] = header.engineVersion;
        root["environment"] = header.environment.ToString();
        root["featureFlags"] = header.featureFlags;
        if (header.parentGeneration)
            root["parentGeneration"] = header.parentGeneration->ToString();
        else
            root["parentGeneration"] = nullptr;
        root["playTimeNanoseconds"] = header.playTimeNanoseconds;
        root["product"] = header.product.ToString();
        root["productCompatibilityVersion"] = header.productCompatibility.Value();
        root["profile"] = header.profile.ToString();
        root["project"] = header.project.ToString();
        root["projectBuildId"] = header.projectBuildId;
        root["slot"] = header.slot.ToString();
        root["slotGeneration"] = header.slotGeneration.ToString();
        root["user"] = header.user.ToString();
        root["world"] = header.world.ToString();
        std::string encoded = root.dump();
        if (encoded.size() > limits.maximumHeaderBytes)
            return Result<std::string>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
        return Result<std::string>::Success(std::move(encoded));
    }

    /** @copydoc DecodeSaveArchiveHeader */
    Result<SaveArchiveHeader> DecodeSaveArchiveHeader(const std::string_view json, const SaveArchiveMetadataLimits &limits) {
        if (!HasValidLimits(limits) || json.size() > limits.maximumHeaderBytes)
            return Result<SaveArchiveHeader>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
        auto decoded = ParseMetadataJson(json, SaveErrors::ArchiveHeaderInvalid);
        if (decoded.HasError())
            return Result<SaveArchiveHeader>::Failure(decoded.ErrorValue());
        const Json &root = decoded.Value();
        if (!HasExactFields(root, {"baseScene", "capturedAtUnixMilliseconds", "engineVersion", "environment", "featureFlags",
                                   "parentGeneration", "playTimeNanoseconds", "product", "productCompatibilityVersion", "profile",
                                   "project", "projectBuildId", "slot", "slotGeneration", "user", "world"}) ||
            !root.at("capturedAtUnixMilliseconds").is_number_unsigned() || !root.at("playTimeNanoseconds").is_number_unsigned() ||
            !root.at("featureFlags").is_number_unsigned())
            return Result<SaveArchiveHeader>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));

        auto slot = ParseIdentity<SaveGameSlotId>(root.at("slot"));
        auto generation = ParseIdentity<SlotGenerationId>(root.at("slotGeneration"));
        auto compatibility = ParseVersion<ProductSaveCompatibilityVersion>(root.at("productCompatibilityVersion"));
        auto product = ParseIdentity<ProductStorageId>(root.at("product"));
        auto environment = ParseIdentity<EnvironmentStorageId>(root.at("environment"));
        auto user = ParseIdentity<LocalUserStorageId>(root.at("user"));
        auto profile = ParseIdentity<GameProfileId>(root.at("profile"));
        auto project = ParseIdentity<SaveProjectId>(root.at("project"));
        auto world = ParseIdentity<SaveWorldId>(root.at("world"));
        auto baseScene = ParseIdentity<SaveBaseSceneId>(root.at("baseScene"));
        auto engineVersion = ParseText(root.at("engineVersion"), limits);
        auto projectBuildId = ParseText(root.at("projectBuildId"), limits);
        if (slot.HasError() || generation.HasError() || compatibility.HasError() || product.HasError() || environment.HasError() ||
            user.HasError() || profile.HasError() || project.HasError() || world.HasError() || baseScene.HasError() ||
            engineVersion.HasError() || projectBuildId.HasError())
            return Result<SaveArchiveHeader>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));

        std::optional<SlotGenerationId> parent;
        if (!root.at("parentGeneration").is_null()) {
            auto parsedParent = ParseIdentity<SlotGenerationId>(root.at("parentGeneration"));
            if (parsedParent.HasError())
                return Result<SaveArchiveHeader>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));
            parent = std::move(parsedParent).Value();
        }
        SaveArchiveHeader header{.slot = std::move(slot).Value(),
                                 .slotGeneration = std::move(generation).Value(),
                                 .parentGeneration = std::move(parent),
                                 .productCompatibility = std::move(compatibility).Value(),
                                 .product = std::move(product).Value(),
                                 .environment = std::move(environment).Value(),
                                 .user = std::move(user).Value(),
                                 .profile = std::move(profile).Value(),
                                 .project = std::move(project).Value(),
                                 .world = std::move(world).Value(),
                                 .baseScene = std::move(baseScene).Value(),
                                 .capturedAtUnixMilliseconds = root.at("capturedAtUnixMilliseconds").get<std::uint64_t>(),
                                 .playTimeNanoseconds = root.at("playTimeNanoseconds").get<std::uint64_t>(),
                                 .engineVersion = std::move(engineVersion).Value(),
                                 .projectBuildId = std::move(projectBuildId).Value(),
                                 .featureFlags = root.at("featureFlags").get<std::uint64_t>()};
        if (const auto valid = ValidateSaveArchiveHeader(header, limits); valid.HasError())
            return Result<SaveArchiveHeader>::Failure(valid.ErrorValue());
        return Result<SaveArchiveHeader>::Success(std::move(header));
    }

    /** @copydoc EncodeSaveGameManifest */
    Result<std::string> EncodeSaveGameManifest(const SaveGameManifest &manifest, const SaveArchiveMetadataLimits &limits) {
        if (const auto valid = ValidateSaveGameManifest(manifest, limits); valid.HasError())
            return Result<std::string>::Failure(valid.ErrorValue());
        OrderedJson participants = OrderedJson::array();
        for (const SaveManifestParticipant &entry : manifest.participants) {
            OrderedJson chunks = OrderedJson::array();
            for (const SaveRecordId &chunk : entry.chunks)
                chunks.push_back(chunk.ToString());
            OrderedJson encodedEntry;
            encodedEntry["chunks"] = std::move(chunks);
            encodedEntry["participant"] = entry.participant.Value();
            encodedEntry["required"] = entry.required;
            encodedEntry["schemaVersion"] = entry.schemaVersion.Value();
            participants.push_back(std::move(encodedEntry));
        }
        OrderedJson root;
        root["canonicalStateHash"] = FormatSha256(manifest.canonicalState.value);
        root["participants"] = std::move(participants);
        root["saveSchemaVersion"] = manifest.saveSchemaVersion.Value();
        std::string encoded = root.dump();
        if (encoded.size() > limits.maximumManifestBytes)
            return Result<std::string>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
        return Result<std::string>::Success(std::move(encoded));
    }

    /** @copydoc DecodeSaveGameManifest */
    Result<SaveGameManifest> DecodeSaveGameManifest(const std::string_view json, const SaveArchiveMetadataLimits &limits) {
        if (!HasValidLimits(limits) || json.size() > limits.maximumManifestBytes)
            return Result<SaveGameManifest>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
        auto decoded = ParseMetadataJson(json, SaveErrors::ArchiveManifestInvalid);
        if (decoded.HasError())
            return Result<SaveGameManifest>::Failure(decoded.ErrorValue());
        const Json &root = decoded.Value();
        if (!HasExactFields(root, {"canonicalStateHash", "participants", "saveSchemaVersion"}) ||
            !root.at("canonicalStateHash").is_string() || !root.at("participants").is_array() || root.at("participants").empty() ||
            root.at("participants").size() > limits.maximumParticipants)
            return Result<SaveGameManifest>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));

        auto schema = ParseVersion<SaveSchemaVersion>(root.at("saveSchemaVersion"));
        auto digest = ParseSha256(root.at("canonicalStateHash").get_ref<const std::string &>());
        if (schema.HasError() || digest.HasError())
            return Result<SaveGameManifest>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
        std::vector<SaveManifestParticipant> participants;
        participants.reserve(root.at("participants").size());
        std::size_t totalChunks = 0;
        for (const Json &value : root.at("participants")) {
            auto participant = DecodeParticipant(value, limits, totalChunks);
            if (participant.HasError())
                return Result<SaveGameManifest>::Failure(participant.ErrorValue());
            participants.push_back(std::move(participant).Value());
        }
        SaveGameManifest manifest{.saveSchemaVersion = std::move(schema).Value(),
                                  .canonicalState = {std::move(digest).Value()},
                                  .participants = std::move(participants)};
        if (const auto valid = ValidateSaveGameManifest(manifest, limits); valid.HasError())
            return Result<SaveGameManifest>::Failure(valid.ErrorValue());
        return Result<SaveGameManifest>::Success(std::move(manifest));
    }

    /** @copydoc EvaluateSaveCompatibility */
    SaveCompatibilityDecision EvaluateSaveCompatibility(const ArchiveFormatVersion archiveVersion, const SaveArchiveHeader &header,
                                                        const SaveGameManifest &manifest, const SaveCompatibilityPolicy &policy) {
        SaveArchiveMetadataLimits limits;
        limits.supportedFeatureFlagsMask = policy.supportedFeatureFlagsMask;
        if (ValidateSaveArchiveHeader(header, limits).HasError() || ValidateSaveGameManifest(manifest, limits).HasError() ||
            !IsValidSupport(policy.archiveVersions) || !IsValidSupport(policy.saveSchemaVersions) ||
            !IsValidSupport(policy.productVersions))
            return Reject(SaveCompatibilityReason::InvalidMetadata);

        bool migrationRequired = false;
        const auto classifyRoot = [&migrationRequired](const VersionAdmission admission,
                                                       const SaveCompatibilityReason reason) -> std::optional<SaveCompatibilityDecision> {
            if (admission == VersionAdmission::Rejected)
                return Reject(reason);
            migrationRequired |= admission == VersionAdmission::Migration;
            return std::nullopt;
        };
        if (auto rejected =
                classifyRoot(ClassifyVersion(archiveVersion, policy.archiveVersions), SaveCompatibilityReason::UnsupportedArchiveVersion))
            return *rejected;
        if (auto rejected = classifyRoot(ClassifyVersion(manifest.saveSchemaVersion, policy.saveSchemaVersions),
                                         SaveCompatibilityReason::UnsupportedSaveSchema))
            return *rejected;
        if (auto rejected = classifyRoot(ClassifyVersion(header.productCompatibility, policy.productVersions),
                                         SaveCompatibilityReason::UnsupportedProductVersion))
            return *rejected;
        if ((header.featureFlags & ~policy.supportedFeatureFlagsMask) != 0)
            return Reject(SaveCompatibilityReason::UnsupportedFeature);

        if (!std::ranges::is_sorted(policy.participants, {}, &SaveParticipantCompatibility::participant))
            return Reject(SaveCompatibilityReason::InvalidMetadata);
        for (std::size_t index = 0; index < policy.participants.size(); ++index) {
            const auto &support = policy.participants[index];
            if (!support.participant.IsValid() || !IsValidSupport(support.versions) ||
                (index != 0 && policy.participants[index - 1].participant == support.participant))
                return Reject(SaveCompatibilityReason::InvalidMetadata);
            const auto found =
                std::ranges::lower_bound(manifest.participants, support.participant, {}, &SaveManifestParticipant::participant);
            if (found == manifest.participants.end() || found->participant != support.participant) {
                if (support.required)
                    return Reject(SaveCompatibilityReason::MissingRequiredParticipant, support.participant);
                continue;
            }
            const VersionAdmission admission = ClassifyVersion(found->schemaVersion, support.versions);
            if (admission == VersionAdmission::Rejected)
                return Reject(SaveCompatibilityReason::UnsupportedParticipantSchema, support.participant);
            migrationRequired |= admission == VersionAdmission::Migration;
        }
        for (const SaveManifestParticipant &entry : manifest.participants) {
            const auto found =
                std::ranges::lower_bound(policy.participants, entry.participant, {}, &SaveParticipantCompatibility::participant);
            if (entry.required && (found == policy.participants.end() || found->participant != entry.participant))
                return Reject(SaveCompatibilityReason::UnknownRequiredParticipant, entry.participant);
        }
        return {.disposition =
                    migrationRequired ? SaveCompatibilityDisposition::MigrationRequired : SaveCompatibilityDisposition::DirectRead,
                .reason = SaveCompatibilityReason::None,
                .participant = std::nullopt};
    }
}  // namespace Horo::Runtime
