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
#include <unordered_set>
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
        template <typename Version> [[nodiscard]] Result<Version> ParseVersion(const Json &value, const ErrorCodeDescriptor &invalidError) {
            if (!value.is_number_unsigned() || value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                return Result<Version>::Failure(MakeError(invalidError));
            auto parsed = Version::Create(static_cast<std::uint32_t>(value.get<std::uint64_t>()));
            if (parsed.HasError())
                return Result<Version>::Failure(MakeError(invalidError));
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
            auto schema = ParseVersion<ParticipantSchemaVersion>(value.at("schemaVersion"), SaveErrors::ArchiveManifestInvalid);
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

        /** @brief Reports whether every persistent identity in a header is valid. */
        [[nodiscard]] bool HasValidHeaderIdentities(const SaveArchiveHeader &header) noexcept {
            return header.slot.IsValid() && header.slotGeneration.IsValid() && header.product.IsValid() && header.environment.IsValid() &&
                   header.user.IsValid() && header.profile.IsValid() && header.project.IsValid() && header.world.IsValid() &&
                   header.baseScene.IsValid();
        }

        /** @brief Reports whether header provenance strings fit the configured bounds. */
        [[nodiscard]] bool HasValidHeaderText(const SaveArchiveHeader &header, const SaveArchiveMetadataLimits &limits) noexcept {
            return !header.engineVersion.empty() && !header.projectBuildId.empty() &&
                   header.engineVersion.size() <= limits.maximumTextBytes && header.projectBuildId.size() <= limits.maximumTextBytes;
        }

        /** @brief Reports whether parent generation, versions, timestamps, and feature bits are coherent. */
        [[nodiscard]] bool HasValidHeaderPublication(const SaveArchiveHeader &header, const SaveArchiveMetadataLimits &limits) noexcept {
            const bool parentValid =
                !header.parentGeneration || (header.parentGeneration->IsValid() && *header.parentGeneration != header.slotGeneration);
            return parentValid && header.productCompatibility.IsValid() && header.capturedAtUnixMilliseconds != 0 &&
                   (header.featureFlags & ~limits.supportedFeatureFlagsMask) == 0;
        }

        struct HeaderIdentities final {
            SaveGameSlotId slot;
            SlotGenerationId generation;
            ProductStorageId product;
            EnvironmentStorageId environment;
            LocalUserStorageId user;
            GameProfileId profile;
            SaveProjectId project;
            SaveWorldId world;
            SaveBaseSceneId baseScene;
        };

        /** @brief Decodes the identity group from an exact-shape header object. */
        [[nodiscard]] Result<HeaderIdentities> DecodeHeaderIdentities(const Json &root) {
            auto slot = ParseIdentity<SaveGameSlotId>(root.at("slot"));
            auto generation = ParseIdentity<SlotGenerationId>(root.at("slotGeneration"));
            auto product = ParseIdentity<ProductStorageId>(root.at("product"));
            auto environment = ParseIdentity<EnvironmentStorageId>(root.at("environment"));
            auto user = ParseIdentity<LocalUserStorageId>(root.at("user"));
            auto profile = ParseIdentity<GameProfileId>(root.at("profile"));
            auto project = ParseIdentity<SaveProjectId>(root.at("project"));
            auto world = ParseIdentity<SaveWorldId>(root.at("world"));
            auto baseScene = ParseIdentity<SaveBaseSceneId>(root.at("baseScene"));
            if (slot.HasError() || generation.HasError() || product.HasError() || environment.HasError() || user.HasError() ||
                profile.HasError() || project.HasError() || world.HasError() || baseScene.HasError())
                return Result<HeaderIdentities>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));
            return Result<HeaderIdentities>::Success({.slot = std::move(slot).Value(),
                                                      .generation = std::move(generation).Value(),
                                                      .product = std::move(product).Value(),
                                                      .environment = std::move(environment).Value(),
                                                      .user = std::move(user).Value(),
                                                      .profile = std::move(profile).Value(),
                                                      .project = std::move(project).Value(),
                                                      .world = std::move(world).Value(),
                                                      .baseScene = std::move(baseScene).Value()});
        }

        struct HeaderProvenance final {
            ProductSaveCompatibilityVersion compatibility;
            std::optional<SlotGenerationId> parent;
            std::string engineVersion;
            std::string projectBuildId;
        };

        /** @brief Decodes bounded version, parent, and diagnostic fields from a header object. */
        [[nodiscard]] Result<HeaderProvenance> DecodeHeaderProvenance(const Json &root, const SaveArchiveMetadataLimits &limits) {
            auto compatibility =
                ParseVersion<ProductSaveCompatibilityVersion>(root.at("productCompatibilityVersion"), SaveErrors::ArchiveHeaderInvalid);
            auto engineVersion = ParseText(root.at("engineVersion"), limits);
            auto projectBuildId = ParseText(root.at("projectBuildId"), limits);
            if (compatibility.HasError())
                return Result<HeaderProvenance>::Failure(compatibility.ErrorValue());
            if (engineVersion.HasError())
                return Result<HeaderProvenance>::Failure(engineVersion.ErrorValue());
            if (projectBuildId.HasError())
                return Result<HeaderProvenance>::Failure(projectBuildId.ErrorValue());
            std::optional<SlotGenerationId> parent;
            if (!root.at("parentGeneration").is_null()) {
                auto parsedParent = ParseIdentity<SlotGenerationId>(root.at("parentGeneration"));
                if (parsedParent.HasError())
                    return Result<HeaderProvenance>::Failure(parsedParent.ErrorValue());
                parent = std::move(parsedParent).Value();
            }
            return Result<HeaderProvenance>::Success({.compatibility = std::move(compatibility).Value(),
                                                      .parent = std::move(parent),
                                                      .engineVersion = std::move(engineVersion).Value(),
                                                      .projectBuildId = std::move(projectBuildId).Value()});
        }

        /** @brief Applies one root compatibility axis and records whether migration is required. */
        [[nodiscard]] std::optional<SaveCompatibilityDecision> EvaluateRootVersion(const VersionAdmission admission,
                                                                                   const SaveCompatibilityReason reason,
                                                                                   bool &migrationRequired) {
            if (admission == VersionAdmission::Rejected)
                return Reject(reason);
            migrationRequired |= admission == VersionAdmission::Migration;
            return std::nullopt;
        }

        /** @brief Validates stable participant policy order and independent version ranges. */
        [[nodiscard]] bool HasValidParticipantPolicy(const SaveCompatibilityPolicy &policy) noexcept {
            if (!std::ranges::is_sorted(policy.participants, {}, &SaveParticipantCompatibility::participant))
                return false;
            for (std::size_t index = 0; index < policy.participants.size(); ++index) {
                const auto &support = policy.participants[index];
                if (!support.participant.IsValid() || !IsValidSupport(support.versions) ||
                    (index != 0 && policy.participants[index - 1].participant == support.participant))
                    return false;
            }
            return true;
        }

        /** @brief Evaluates declared participant support and required current composition. */
        [[nodiscard]] std::optional<SaveCompatibilityDecision> EvaluateDeclaredParticipants(const SaveGameManifest &manifest,
                                                                                            const SaveCompatibilityPolicy &policy,
                                                                                            bool &migrationRequired) {
            for (const auto &support : policy.participants) {
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
            return std::nullopt;
        }

        /** @brief Rejects required manifest participants absent from the sealed policy. */
        [[nodiscard]] std::optional<SaveCompatibilityDecision> FindUnknownRequiredParticipant(const SaveGameManifest &manifest,
                                                                                              const SaveCompatibilityPolicy &policy) {
            for (const SaveManifestParticipant &entry : manifest.participants) {
                const auto found =
                    std::ranges::lower_bound(policy.participants, entry.participant, {}, &SaveParticipantCompatibility::participant);
                if (entry.required && (found == policy.participants.end() || found->participant != entry.participant))
                    return Reject(SaveCompatibilityReason::UnknownRequiredParticipant, entry.participant);
            }
            return std::nullopt;
        }
    }  // namespace

    /** @copydoc ValidateSaveArchiveHeader */
    Result<void> ValidateSaveArchiveHeader(const SaveArchiveHeader &header, const SaveArchiveMetadataLimits &limits) {
        if (!HasValidLimits(limits))
            return Result<void>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
        if (!HasValidHeaderIdentities(header) || !HasValidHeaderPublication(header, limits))
            return Result<void>::Failure(MakeError(SaveErrors::ArchiveHeaderInvalid));
        if (!HasValidHeaderText(header, limits))
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
        std::unordered_set<SaveRecordId, PersistentSaveIdentityHash<SaveRecordIdentityTag>> uniqueChunks;
        uniqueChunks.reserve(std::min(limits.maximumTotalChunks, manifest.participants.size() * 2U));
        const SaveParticipantId *previousParticipant = nullptr;
        for (const SaveManifestParticipant &entry : manifest.participants) {
            if (!entry.participant.IsValid() || !entry.schemaVersion.IsValid() || entry.chunks.empty())
                return Result<void>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            if (entry.chunks.size() > limits.maximumChunksPerParticipant || entry.chunks.size() > limits.maximumTotalChunks - totalChunks)
                return Result<void>::Failure(MakeError(SaveErrors::ArchiveMetadataLimitExceeded));
            if (previousParticipant != nullptr && *previousParticipant >= entry.participant)
                return Result<void>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            if (const auto unique = ValidateUniqueSaveIdentities<SaveRecordIdentityTag>(entry.chunks);
                unique.HasError() || !std::ranges::is_sorted(entry.chunks))
                return Result<void>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            for (const SaveRecordId &chunk : entry.chunks) {
                if (!uniqueChunks.insert(chunk).second)
                    return Result<void>::Failure(MakeError(SaveErrors::ArchiveManifestInvalid));
            }
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

        auto identities = DecodeHeaderIdentities(root);
        auto provenance = DecodeHeaderProvenance(root, limits);
        if (identities.HasError() || provenance.HasError())
            return Result<SaveArchiveHeader>::Failure(identities.HasError() ? identities.ErrorValue() : provenance.ErrorValue());
        HeaderIdentities identityValues = std::move(identities).Value();
        HeaderProvenance provenanceValues = std::move(provenance).Value();
        SaveArchiveHeader header{.slot = std::move(identityValues.slot),
                                 .slotGeneration = std::move(identityValues.generation),
                                 .parentGeneration = std::move(provenanceValues.parent),
                                 .productCompatibility = std::move(provenanceValues.compatibility),
                                 .product = std::move(identityValues.product),
                                 .environment = std::move(identityValues.environment),
                                 .user = std::move(identityValues.user),
                                 .profile = std::move(identityValues.profile),
                                 .project = std::move(identityValues.project),
                                 .world = std::move(identityValues.world),
                                 .baseScene = std::move(identityValues.baseScene),
                                 .capturedAtUnixMilliseconds = root.at("capturedAtUnixMilliseconds").get<std::uint64_t>(),
                                 .playTimeNanoseconds = root.at("playTimeNanoseconds").get<std::uint64_t>(),
                                 .engineVersion = std::move(provenanceValues.engineVersion),
                                 .projectBuildId = std::move(provenanceValues.projectBuildId),
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

        auto schema = ParseVersion<SaveSchemaVersion>(root.at("saveSchemaVersion"), SaveErrors::ArchiveManifestInvalid);
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
        limits.supportedFeatureFlagsMask = std::numeric_limits<std::uint64_t>::max();
        if (ValidateSaveArchiveHeader(header, limits).HasError() || ValidateSaveGameManifest(manifest, limits).HasError() ||
            !IsValidSupport(policy.archiveVersions) || !IsValidSupport(policy.saveSchemaVersions) ||
            !IsValidSupport(policy.productVersions))
            return Reject(SaveCompatibilityReason::InvalidMetadata);

        if (!HasValidParticipantPolicy(policy))
            return Reject(SaveCompatibilityReason::InvalidMetadata);

        bool migrationRequired = false;
        if (auto rejected = EvaluateRootVersion(ClassifyVersion(archiveVersion, policy.archiveVersions),
                                                SaveCompatibilityReason::UnsupportedArchiveVersion, migrationRequired))
            return *rejected;
        if (auto rejected = EvaluateRootVersion(ClassifyVersion(manifest.saveSchemaVersion, policy.saveSchemaVersions),
                                                SaveCompatibilityReason::UnsupportedSaveSchema, migrationRequired))
            return *rejected;
        if (auto rejected = EvaluateRootVersion(ClassifyVersion(header.productCompatibility, policy.productVersions),
                                                SaveCompatibilityReason::UnsupportedProductVersion, migrationRequired))
            return *rejected;
        if ((header.featureFlags & ~policy.supportedFeatureFlagsMask) != 0)
            return Reject(SaveCompatibilityReason::UnsupportedFeature);

        if (auto rejected = EvaluateDeclaredParticipants(manifest, policy, migrationRequired))
            return *rejected;
        if (auto rejected = FindUnknownRequiredParticipant(manifest, policy))
            return *rejected;
        return {.disposition =
                    migrationRequired ? SaveCompatibilityDisposition::MigrationRequired : SaveCompatibilityDisposition::DirectRead,
                .reason = SaveCompatibilityReason::None,
                .participant = std::nullopt};
    }
}  // namespace Horo::Runtime
