#include "Horo/Application/ProjectCompatibility.h"

#include "GeneratedProjectCompatibility.h"
#include "Horo/Foundation/String.h"
#include "ProjectErrors.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <fstream>
#include <unordered_set>
#include <utility>

namespace Horo::Application
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::uintmax_t kMaximumMetadataBytes = 64U * 1024U;
        constexpr std::size_t kMaximumDepth = 16;
        constexpr std::size_t kMaximumKeys = 256;
        constexpr std::size_t kMaximumKeyBytes = 128;
        constexpr std::size_t kMaximumStringBytes = 8U * 1024U;
        constexpr std::size_t kMaximumProjectIdBytes = 256;
        constexpr std::size_t kMaximumProjectNameBytes = 1024;
        constexpr std::size_t kMaximumProjectVersionBytes = 64;
        constexpr std::size_t kMaximumTimestampBytes = 64;
        constexpr std::size_t kMaximumBackendIdBytes = 64;
        constexpr std::size_t kMaximumSignatureBytes = 4U * 1024U;

        Error MetadataError(const ErrorCodeDescriptor& descriptor, std::string message = {})
        {
            return MakeError(descriptor, std::move(message));
        }

        bool IsCanonicalBackendId(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > kMaximumBackendIdBytes || value.back() == '-')
                return false;
            const auto alpha = [](const char c) { return c >= 'a' && c <= 'z'; };
            const auto digit = [](const char c) { return c >= '0' && c <= '9'; };
            if (!alpha(value.front()))
                return false;
            return std::all_of(value.begin(), value.end(), [&](const char c)
            {
                return alpha(c) || digit(c) || c == '-';
            });
        }

        Result<std::string> ReadMetadata(const std::filesystem::path& path)
        {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error)
            {
                std::error_code existsError;
                const bool exists = std::filesystem::exists(path, existsError);
                const ErrorCodeDescriptor& descriptor =
                    !existsError && !exists ? ProjectErrors::MetadataNotFound : ProjectErrors::MetadataReadFailed;
                return Result<std::string>::Failure(
                    MetadataError(descriptor, "Unable to inspect " + path.string() + '.'));
            }
            if (size == 0 || size > kMaximumMetadataBytes)
            {
                return Result<std::string>::Failure(MetadataError(ProjectErrors::MetadataSizeInvalid));
            }
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open())
            {
                return Result<std::string>::Failure(MetadataError(ProjectErrors::MetadataReadFailed));
            }
            std::string contents(size, '\0');
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (input.gcount() != static_cast<std::streamsize>(contents.size()) || input.bad())
            {
                return Result<std::string>::Failure(MetadataError(ProjectErrors::MetadataReadFailed));
            }
            return Result<std::string>::Success(std::move(contents));
        }

        Result<Json> ParseJson(const std::string& contents)
        {
            bool duplicate = false;
            bool limitExceeded = false;
            std::size_t keyCount = 0;
            std::vector<std::unordered_set<std::string>> keys;
            const Json::parser_callback_t callback = [&](const int depth, const Json::parse_event_t event, Json& parsed)
            {
                if (depth < 0 || static_cast<std::size_t>(depth) > kMaximumDepth)
                {
                    limitExceeded = true;
                    return false;
                }
                if (event == Json::parse_event_t::object_start)
                {
                    const std::size_t objectDepth = static_cast<std::size_t>(depth);
                    if (keys.size() <= objectDepth)
                        keys.resize(objectDepth + 1);
                    keys[objectDepth].clear();
                }
                else if (event == Json::parse_event_t::key)
                {
                    const std::string value = parsed.get<std::string>();
                    const std::size_t objectDepth = depth > 0 ? static_cast<std::size_t>(depth - 1) : 0;
                    if (keys.size() <= objectDepth)
                        keys.resize(objectDepth + 1);
                    duplicate = !keys[objectDepth].insert(value).second || duplicate;
                    limitExceeded = ++keyCount > kMaximumKeys || value.size() > kMaximumKeyBytes || limitExceeded;
                }
                else if (event == Json::parse_event_t::value && parsed.is_string())
                {
                    limitExceeded = parsed.get_ref<const std::string&>().size() > kMaximumStringBytes || limitExceeded;
                }
                return !duplicate && !limitExceeded;
            };

            try
            {
                Json document = Json::parse(contents, callback, true, false);
                if (duplicate)
                    return Result<Json>::Failure(MetadataError(ProjectErrors::MetadataDuplicateKey));
                if (limitExceeded)
                    return Result<Json>::Failure(MetadataError(ProjectErrors::MetadataLimitExceeded));
                return Result<Json>::Success(std::move(document));
            }
            catch (const Json::exception& exception)
            {
                if (duplicate)
                    return Result<Json>::Failure(MetadataError(ProjectErrors::MetadataDuplicateKey));
                if (limitExceeded)
                    return Result<Json>::Failure(MetadataError(ProjectErrors::MetadataLimitExceeded));
                return Result<Json>::Failure(MetadataError(ProjectErrors::MetadataInvalidJson,
                                                           "Invalid project metadata JSON: " + std::string{
                                                               exception.what()
                                                           }));
            }
        }

        bool HasString(const Json& object, const char* key)
        {
            return object.contains(key) && object[key].is_string();
        }

        Result<CompatibilityProof> ParseProof(const Json& value)
        {
            if (!value.is_object() || !HasString(value, "release") || !HasString(value, "contractBaseline") ||
                !HasString(value, "decisionHash") || !HasString(value, "signature"))
            {
                return Result<CompatibilityProof>::Failure(MetadataError(ProjectErrors::MetadataInvalidSchema));
            }
            const std::string& signature = value["signature"].get_ref<const std::string&>();
            if (signature.empty() || signature.size() > kMaximumSignatureBytes)
            {
                return Result<CompatibilityProof>::Failure(MetadataError(ProjectErrors::MetadataInvalidValue));
            }
            auto release = ParseHoroVersion(value["release"].get_ref<const std::string&>());
            auto baseline = ParseHoroVersion(value["contractBaseline"].get_ref<const std::string&>());
            auto decisionHash = ParseCompatibilityDecisionHash(value["decisionHash"].get_ref<const std::string&>());
            if (release.HasError() || baseline.HasError() || decisionHash.HasError())
            {
                return Result<CompatibilityProof>::Failure(MetadataError(ProjectErrors::MetadataInvalidValue));
            }
            return Result<CompatibilityProof>::Success(
                {{release.Value()}, {baseline.Value()}, decisionHash.Value(), signature});
        }

        bool SameReleaseLine(const HoroVersion& lhs, const HoroVersion& rhs) noexcept
        {
            return lhs.IsStable() && rhs.IsStable() && lhs.major == rhs.major && lhs.minor == rhs.minor;
        }

        bool IsCanonicalVersionValue(const HoroVersion& version)
        {
            const auto parsed = ParseHoroVersion(FormatHoroVersion(version));
            return parsed.HasValue() && parsed.Value() == version;
        }

        ProjectCompatibilitySnapshot FailedSnapshot(const EngineReleaseVersion target,
                                                    const ProjectCompatibilityStatus status,
                                                    Error error)
        {
            return {.status = status, .targetVersion = target, .diagnostic = std::move(error)};
        }
    } // namespace

    ReleaseCompatibilityRegistry::ReleaseCompatibilityRegistry(std::vector<ReleaseCompatibilityDecision> decisions)
        : decisions_(std::move(decisions))
    {
    }

    /** @copydoc ReleaseCompatibilityRegistry::Create */
    Result<ReleaseCompatibilityRegistry> ReleaseCompatibilityRegistry::Create(
        const std::span<const ReleaseCompatibilityDecision> decisions)
    {
        if (decisions.empty())
        {
            return Result<ReleaseCompatibilityRegistry>::Failure(MakeError(ProjectErrors::RegistryInvalid));
        }
        std::vector<ReleaseCompatibilityDecision> copy{decisions.begin(), decisions.end()};
        std::sort(copy.begin(), copy.end(), [](const auto& lhs, const auto& rhs)
        {
            return CompareHoroVersions(lhs.release.value, rhs.release.value) == std::strong_ordering::less;
        });
        for (std::size_t i = 0; i < copy.size(); ++i)
        {
            const auto& decision = copy[i];
            if (!IsCanonicalVersionValue(decision.release.value) ||
                !IsCanonicalVersionValue(decision.contractBaseline.value))
            {
                return Result<ReleaseCompatibilityRegistry>::Failure(MakeError(ProjectErrors::RegistryInvalid));
            }
            if (i > 0 && copy[i - 1].release == decision.release)
            {
                return Result<
                    ReleaseCompatibilityRegistry>::Failure(MakeError(ProjectErrors::RegistryDuplicateRelease));
            }
            if (CompareHoroVersions(decision.contractBaseline.value, decision.release.value) ==
                std::strong_ordering::greater)
            {
                return Result<
                    ReleaseCompatibilityRegistry>::Failure(MakeError(ProjectErrors::RegistryBaselineMismatch));
            }
            if (decision.kind == CompatibilityDecisionKind::EstablishBaseline &&
                !(decision.release.value == decision.contractBaseline.value))
            {
                return Result<
                    ReleaseCompatibilityRegistry>::Failure(MakeError(ProjectErrors::RegistryBaselineMismatch));
            }
            const auto baseline = std::find_if(copy.begin(), copy.end(), [&](const auto& candidate)
            {
                return candidate.release.value == decision.contractBaseline.value;
            });
            if (baseline == copy.end() || baseline->kind != CompatibilityDecisionKind::EstablishBaseline ||
                baseline->persistentContract != decision.persistentContract)
            {
                return Result<
                    ReleaseCompatibilityRegistry>::Failure(MakeError(ProjectErrors::RegistryBaselineMismatch));
            }
            for (std::size_t prior = 0; prior < i; ++prior)
            {
                if (SameReleaseLine(copy[prior].release.value, decision.release.value) &&
                    (copy[prior].contractBaseline != decision.contractBaseline ||
                        copy[prior].persistentContract != decision.persistentContract))
                {
                    return Result<ReleaseCompatibilityRegistry>::Failure(
                        MakeError(ProjectErrors::RegistryPatchContractDrift));
                }
            }
        }
        return Result<ReleaseCompatibilityRegistry>::Success(ReleaseCompatibilityRegistry{std::move(copy)});
    }

    /** @copydoc ReleaseCompatibilityRegistry::Find */
    const ReleaseCompatibilityDecision* ReleaseCompatibilityRegistry::Find(
        const EngineReleaseVersion release) const noexcept
    {
        const auto found = std::lower_bound(
            decisions_.begin(), decisions_.end(), release,
            [](const ReleaseCompatibilityDecision& decision, const EngineReleaseVersion candidate)
            {
                return CompareHoroVersions(decision.release.value, candidate.value) == std::strong_ordering::less;
            });
        return found != decisions_.end() && found->release == release ? &*found : nullptr;
    }

    /** @copydoc ReleaseCompatibilityRegistry::Decisions */
    std::span<const ReleaseCompatibilityDecision> ReleaseCompatibilityRegistry::Decisions() const noexcept
    {
        return decisions_;
    }

    /** @copydoc RejectingCompatibilityProofVerifier::Verify */
    Result<void> RejectingCompatibilityProofVerifier::Verify(const CompatibilityProof& proof,
                                                             const PersistentContractHash& expectedContract) const
    {
        static_cast<void>(proof);
        static_cast<void>(expectedContract);
        return Result<void>::Failure(MakeError(ProjectErrors::ProofRejected));
    }

    /** @copydoc LoadProjectMetadata */
    Result<ProjectMetadata> LoadProjectMetadata(const std::filesystem::path& projectRoot)
    {
        auto contents = ReadMetadata(projectRoot / ".horo/project.json");
        if (contents.HasError())
            return Result<ProjectMetadata>::Failure(contents.ErrorValue());
        auto parsed = ParseJson(contents.Value());
        if (parsed.HasError())
            return Result<ProjectMetadata>::Failure(parsed.ErrorValue());
        const Json& root = parsed.Value();
        if (!root.is_object() || !HasString(root, "horoVersion") || !HasString(root, "persistentContract") ||
            !HasString(root, "projectId") || !HasString(root, "name") || !HasString(root, "projectVersion") ||
            !HasString(root, "createdAt") || !root.contains("settings") || !root["settings"].is_object() ||
            !HasString(root["settings"], "renderBackend"))
        {
            return Result<ProjectMetadata>::Failure(MetadataError(ProjectErrors::MetadataInvalidSchema));
        }

        auto horoVersion = ParseHoroVersion(root["horoVersion"].get_ref<const std::string&>());
        auto contract = ParsePersistentContractHash(root["persistentContract"].get_ref<const std::string&>());
        if (horoVersion.HasError() || contract.HasError())
        {
            return Result<ProjectMetadata>::Failure(MetadataError(ProjectErrors::MetadataInvalidValue));
        }

        const std::string projectId = root["projectId"].get<std::string>();
        const std::string name = root["name"].get<std::string>();
        const std::string projectVersion = root["projectVersion"].get<std::string>();
        const std::string createdAt = root["createdAt"].get<std::string>();
        const std::string renderBackend = root["settings"]["renderBackend"].get<std::string>();
        if (Text::IsBlank(projectId) || projectId.size() > kMaximumProjectIdBytes || Text::IsBlank(name) ||
            name.size() > kMaximumProjectNameBytes || Text::IsBlank(projectVersion) ||
            projectVersion.size() > kMaximumProjectVersionBytes || Text::IsBlank(createdAt) ||
            createdAt.size() > kMaximumTimestampBytes || !IsCanonicalBackendId(renderBackend))
        {
            return Result<ProjectMetadata>::Failure(MetadataError(ProjectErrors::MetadataInvalidValue));
        }

        std::optional<CompatibilityProof> proof;
        if (root.contains("compatibilityProof"))
        {
            auto parsedProof = ParseProof(root["compatibilityProof"]);
            if (parsedProof.HasError())
                return Result<ProjectMetadata>::Failure(parsedProof.ErrorValue());
            proof = std::move(parsedProof).Value();
        }
        std::optional<MigrationHistoryHead> historyHead;
        if (root.contains("migrationHistoryHead"))
        {
            if (!root["migrationHistoryHead"].is_string())
                return Result<ProjectMetadata>::Failure(MetadataError(ProjectErrors::MetadataInvalidValue));
            auto parsedHead = ParsePersistentContractHash(root["migrationHistoryHead"].get_ref<const std::string&>());
            if (parsedHead.HasError())
                return Result<ProjectMetadata>::Failure(MetadataError(ProjectErrors::MetadataInvalidValue));
            historyHead = MigrationHistoryHead{MigrationContentHash{parsedHead.Value().bytes}};
        }
        return Result<ProjectMetadata>::Success({
            {horoVersion.Value()},
            contract.Value(),
            std::move(proof),
            projectId,
            name,
            projectVersion,
            createdAt,
            renderBackend,
            historyHead
        });
    }

    ProjectCompatibilityInspector::ProjectCompatibilityInspector(const ReleaseCompatibilityRegistry& registry,
                                                                 const EngineReleaseVersion currentRelease,
                                                                 const ICompatibilityProofVerifier& proofVerifier)
        noexcept
        : registry_(registry), currentRelease_(currentRelease), proofVerifier_(proofVerifier)
    {
    }

    /** @copydoc ProjectCompatibilityInspector::Inspect */
    ProjectCompatibilitySnapshot ProjectCompatibilityInspector::Inspect(const std::filesystem::path& projectRoot) const
    {
        auto loaded = LoadProjectMetadata(projectRoot);
        if (loaded.HasError())
        {
            const bool inaccessible = loaded.ErrorValue().code.Value() == "project.metadata.not_found" ||
                loaded.ErrorValue().code.Value() == "project.metadata.read_failed";
            return FailedSnapshot(currentRelease_,
                                  inaccessible
                                      ? ProjectCompatibilityStatus::Inaccessible
                                      : ProjectCompatibilityStatus::Corrupt,
                                  loaded.ErrorValue());
        }
        ProjectMetadata metadata = std::move(loaded).Value();
        const ReleaseCompatibilityDecision* current = registry_.Find(currentRelease_);
        if (current == nullptr)
        {
            return FailedSnapshot(currentRelease_, ProjectCompatibilityStatus::Corrupt,
                                  MakeError(ProjectErrors::RegistryInvalid));
        }

        ProjectCompatibilitySnapshot snapshot{.metadata = metadata, .targetVersion = currentRelease_};
        const ReleaseCompatibilityDecision* source = registry_.Find(metadata.horoVersion);
        if (source != nullptr)
        {
            snapshot.sourceBaseline = source->contractBaseline;
            if (source->persistentContract != metadata.persistentContract)
            {
                snapshot.status = ProjectCompatibilityStatus::FutureVersion;
                snapshot.diagnostic =
                    MakeError(ProjectErrors::HashInvalid, "Project contract does not match its release decision.");
                return snapshot;
            }
            const auto ordering = CompareHoroVersions(metadata.horoVersion.value, currentRelease_.value);
            if (ordering == std::strong_ordering::equal)
            {
                snapshot.status = ProjectCompatibilityStatus::Current;
                return snapshot;
            }
            if (SameReleaseLine(metadata.horoVersion.value, currentRelease_.value) &&
                source->contractBaseline == current->contractBaseline &&
                source->persistentContract == current->persistentContract)
            {
                snapshot.status = ProjectCompatibilityStatus::CompatibleReleaseLine;
                snapshot.markerUpdateRequired = ordering == std::strong_ordering::less;
                return snapshot;
            }
            snapshot.status = ordering == std::strong_ordering::less
                                  ? ProjectCompatibilityStatus::MigrationPathMissing
                                  : ProjectCompatibilityStatus::FutureVersion;
            return snapshot;
        }

        const auto ordering = CompareHoroVersions(metadata.horoVersion.value, currentRelease_.value);
        if (ordering == std::strong_ordering::greater &&
            SameReleaseLine(metadata.horoVersion.value, currentRelease_.value) &&
            metadata.persistentContract == current->persistentContract && metadata.compatibilityProof.has_value() &&
            metadata.compatibilityProof->release == metadata.horoVersion &&
            metadata.compatibilityProof->contractBaseline == current->contractBaseline)
        {
            const Result<void> verification =
                proofVerifier_.Verify(*metadata.compatibilityProof, current->persistentContract);
            if (verification.HasValue())
            {
                snapshot.status = ProjectCompatibilityStatus::CompatibleReleaseLine;
                snapshot.sourceBaseline = current->contractBaseline;
                return snapshot;
            }
            snapshot.diagnostic = verification.ErrorValue();
        }
        snapshot.status = ordering == std::strong_ordering::less
                              ? ProjectCompatibilityStatus::MigrationPathMissing
                              : ProjectCompatibilityStatus::FutureVersion;
        if (!snapshot.diagnostic.has_value() && ordering == std::strong_ordering::greater)
        {
            snapshot.diagnostic = MakeError(ProjectErrors::ProofRejected);
        }
        return snapshot;
    }

    /** @copydoc CurrentEngineReleaseVersion */
    EngineReleaseVersion CurrentEngineReleaseVersion()
    {
        const auto parsed = ParseHoroVersion(Generated::kHoroReleaseVersion);
        if (parsed.HasError())
            std::terminate();
        return {parsed.Value()};
    }

    /** @copydoc BuiltInReleaseCompatibilityRegistry */
    const ReleaseCompatibilityRegistry& BuiltInReleaseCompatibilityRegistry()
    {
        static const ReleaseCompatibilityRegistry registry = []
        {
            std::vector<ReleaseCompatibilityDecision> decisions;
            decisions.reserve(Generated::kHoroReleaseDecisionCount);
            for (const auto& generated : Generated::kHoroReleaseDecisions)
            {
                const auto release = ParseHoroVersion(generated.release);
                const auto baseline = ParseHoroVersion(generated.contractBaseline);
                const auto contract = ParsePersistentContractHash(generated.persistentContract);
                const auto decisionHash = ParseCompatibilityDecisionHash(generated.decisionHash);
                if (release.HasError() || baseline.HasError() || contract.HasError() || decisionHash.HasError())
                    std::terminate();
                decisions.push_back({
                    {release.Value()},
                    {baseline.Value()},
                    contract.Value(),
                    decisionHash.Value(),
                    generated.establishesBaseline
                        ? CompatibilityDecisionKind::EstablishBaseline
                        : CompatibilityDecisionKind::CompatibleReleaseLine
                });
            }
            const auto created = ReleaseCompatibilityRegistry::Create(decisions);
            if (created.HasError())
                std::terminate();
            return std::move(created).Value();
        }();
        return registry;
    }

    /** @copydoc InspectProjectCompatibility */
    ProjectCompatibilitySnapshot InspectProjectCompatibility(const std::filesystem::path& projectRoot)
    {
        static const RejectingCompatibilityProofVerifier verifier;
        const ProjectCompatibilityInspector inspector{
            BuiltInReleaseCompatibilityRegistry(), CurrentEngineReleaseVersion(),
            verifier
        };
        return inspector.Inspect(projectRoot);
    }
} // namespace Horo::Application
