#include "../ProjectMigration.h"

#include "src/application/project/ProjectErrors.h"

#include <nlohmann/json.hpp>

#include <array>
#include <ranges>

namespace Horo::ProjectMigrations::R0_1_0
{
namespace
{
using Json = nlohmann::json;

constexpr std::array<std::string_view, 3> AssetCompression{"lz4", "none", "zstd"};
constexpr std::array<std::string_view, 4> TextureCompression{"bc7", "bc5", "astc", "none"};
constexpr std::string_view TargetContract =
    "sha256:997e790fc23515b362847c755006156aa35353ce7f2624518acf7ed1214ddb03";

[[nodiscard]] Result<Json> ProjectRoot(const Application::ProjectMigrationContext &context)
{
    const auto documents = context.ListDocuments(
        Application::MigrationDocumentQuery::Kind(Application::MigrationDocumentKind::ProjectMetadata));
    if (documents.size() != 1 || documents.front().path != ".horo/project.json")
        return Result<Json>::Failure(MakeError(Application::ProjectErrors::MigrationStageFailed,
                                               "Candidate must contain exactly one project metadata document."));
    auto document = context.ReadDocument(documents.front().handle);
    if (document.HasError())
        return Result<Json>::Failure(document.ErrorValue());
    try
    {
        return Result<Json>::Success(Json::parse(reinterpret_cast<const char *>(document.Value().bytes.data()),
                                                 reinterpret_cast<const char *>(document.Value().bytes.data() +
                                                                                document.Value().bytes.size())));
    }
    catch (...)
    {
        return Result<Json>::Failure(MakeError(Application::ProjectErrors::MigrationStageFailed,
                                               "Candidate project metadata is not valid JSON."));
    }
}

template <std::size_t Size>
[[nodiscard]] bool IsAllowed(const Json &settings, const char *key,
                             const std::array<std::string_view, Size> &allowed)
{
    return settings.contains(key) && settings[key].is_string() && !settings[key].get_ref<const std::string &>().empty() &&
           std::ranges::find(allowed, settings[key].get_ref<const std::string &>()) != allowed.end();
}

[[nodiscard]] Result<void> ValidateCompression(const Json &root)
{
    if (!root.is_object() || !root.contains("settings") || !root["settings"].is_object())
        return Result<void>::Failure(MakeError(Application::ProjectErrors::MigrationStageFailed,
                                               "Project settings must be an object."));
    const Json &settings = root["settings"];
    if (!IsAllowed(settings, "assetCompression", AssetCompression))
        return Result<void>::Failure(MakeError(Application::ProjectErrors::MigrationStageFailed,
                                               "assetCompression is missing or unsupported."));
    if (!IsAllowed(settings, "textureCompression", TextureCompression))
        return Result<void>::Failure(MakeError(Application::ProjectErrors::MigrationStageFailed,
                                               "textureCompression is missing or unsupported."));
    return Result<void>::Success();
}

class CompressionPostconditionValidator final : public Application::IProjectMigrationValidator
{
  public:
    [[nodiscard]] Application::MigrationStageDescriptor Describe() const override
    {
        return {.id = {"validate_compression_postconditions"},
                .readFamilies = {"project.settings.compression"},
                .estimatedWeight = 1};
    }
    [[nodiscard]] Result<void> Validate(const Application::ProjectMigrationContext &context,
                                        const CancellationToken &) const override
    {
        auto root = ProjectRoot(context);
        return root.HasError() ? Result<void>::Failure(root.ErrorValue()) : ValidateCompression(root.Value());
    }
};

class TargetContractValidator final : public Application::IProjectMigrationValidator
{
  public:
    [[nodiscard]] Application::MigrationStageDescriptor Describe() const override
    {
        return {.id = {"validate_0_1_0_target_contract"},
                .readFamilies = {"project.metadata", "project.migration_history"},
                .estimatedWeight = 1};
    }
    [[nodiscard]] Result<void> Validate(const Application::ProjectMigrationContext &context,
                                        const CancellationToken &) const override
    {
        auto rootResult = ProjectRoot(context);
        if (rootResult.HasError())
            return Result<void>::Failure(rootResult.ErrorValue());
        const Json &root = rootResult.Value();
        constexpr std::array required{"horoVersion", "persistentContract", "projectId", "name", "projectVersion",
                                      "createdAt", "settings", "migrationHistoryHead"};
        if (!root.is_object() || std::ranges::any_of(required, [&root](const char *key) { return !root.contains(key); }))
            return Result<void>::Failure(MakeError(Application::ProjectErrors::MigrationStageFailed,
                                                   "Target project metadata lacks required fields."));
        if (!root["horoVersion"].is_string() || root["horoVersion"] != "0.1.0" ||
            !root["persistentContract"].is_string() || root["persistentContract"] != TargetContract ||
            !root["migrationHistoryHead"].is_string())
            return Result<void>::Failure(MakeError(Application::ProjectErrors::MigrationStageFailed,
                                                   "Target version, contract, or history binding is invalid."));
        return ValidateCompression(root);
    }
};
} // namespace

/** @copydoc BuildCompressionPostconditionValidator */
std::shared_ptr<const Application::IProjectMigrationValidator> BuildCompressionPostconditionValidator()
{
    return std::make_shared<CompressionPostconditionValidator>();
}

/** @copydoc BuildTargetValidator */
std::shared_ptr<const Application::IProjectMigrationValidator> BuildTargetValidator()
{
    return std::make_shared<TargetContractValidator>();
}
} // namespace Horo::ProjectMigrations::R0_1_0
