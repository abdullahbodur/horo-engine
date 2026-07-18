#include "editor/project_model/ProjectMetadata.h"

#include "Horo/Foundation/Diagnostics.h"
#include "editor/EditorServiceErrors.h"
#include "Horo/Foundation/String.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Horo::Editor
{
namespace
{
using Json = nlohmann::json;
constexpr std::uintmax_t kMaximumProjectMetadataBytes = 64U * 1024U;
constexpr std::size_t kMaximumProjectIdentityBytes = 256;
constexpr std::size_t kMaximumProjectNameBytes = 1024;
constexpr std::size_t kMaximumBackendIdBytes = 64;

[[nodiscard]] Error MakeMetadataError(const ErrorCodeDescriptor &descriptor, std::string message)
{
    return MakeError(descriptor, std::move(message));
}

[[nodiscard]] bool IsCanonicalBackendId(const std::string_view value) noexcept
{
    const auto isLowerAlpha = [](const unsigned char character) { return character >= 'a' && character <= 'z'; };
    const auto isDigit = [](const unsigned char character) { return character >= '0' && character <= '9'; };
    if (value.empty() || value.size() > kMaximumBackendIdBytes ||
        !isLowerAlpha(static_cast<unsigned char>(value.front())) || value.back() == '-')
    {
        return false;
    }
    for (const unsigned char character : value)
    {
        if (!isLowerAlpha(character) && !isDigit(character) && character != '-')
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HasRequiredSchema(const Json &document) noexcept
{
    if (!document.is_object() || !document.contains("formatVersion") ||
        !document["formatVersion"].is_number_unsigned() || document["formatVersion"].get<std::uint64_t>() != 1 ||
        !document.contains("projectId") || !document["projectId"].is_string() || !document.contains("name") ||
        !document["name"].is_string() || !document.contains("settings") || !document["settings"].is_object())
    {
        return false;
    }

    const Json &settings = document["settings"];
    return settings.contains("renderBackend") && settings["renderBackend"].is_string();
}

[[nodiscard]] Result<std::string> ReadMetadataFile(const std::filesystem::path &metadataPath)
{
    std::error_code error;
    const std::uintmax_t byteCount = std::filesystem::file_size(metadataPath, error);
    if (error)
    {
        return Result<std::string>::Failure(
            MakeMetadataError(ProjectMetadataErrors::NotFound, "Unable to inspect " + metadataPath.string() + '.'));
    }
    if (byteCount == 0 || byteCount > kMaximumProjectMetadataBytes)
    {
        return Result<std::string>::Failure(
            MakeMetadataError(ProjectMetadataErrors::SizeInvalid, "Project metadata must be between 1 byte and 64 KiB."));
    }

    std::ifstream input(metadataPath, std::ios::binary);
    if (!input.is_open())
    {
        return Result<std::string>::Failure(
            MakeMetadataError(ProjectMetadataErrors::NotFound, "Unable to open " + metadataPath.string() + '.'));
    }

    std::string contents(static_cast<std::size_t>(byteCount), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size()) || input.bad())
    {
        return Result<std::string>::Failure(
            MakeMetadataError(ProjectMetadataErrors::ReadFailed, "Unable to read " + metadataPath.string() + '.'));
    }
    return Result<std::string>::Success(std::move(contents));
}

[[nodiscard]] Result<Json> ParseMetadataDocument(const std::string &contents)
{
    bool duplicateKey = false;
    std::vector<std::unordered_set<std::string>> objectKeys;
    const Json::parser_callback_t callback =
        [&duplicateKey, &objectKeys](const int depth, const Json::parse_event_t event, Json &parsed) {
            if (event == Json::parse_event_t::object_start)
            {
                const std::size_t objectDepth = static_cast<std::size_t>(depth);
                if (objectKeys.size() <= objectDepth)
                {
                    objectKeys.resize(objectDepth + 1);
                }
                objectKeys[objectDepth].clear();
            }
            else if (event == Json::parse_event_t::key)
            {
                const std::size_t objectDepth = depth > 0 ? static_cast<std::size_t>(depth - 1) : 0;
                if (objectKeys.size() <= objectDepth)
                {
                    objectKeys.resize(objectDepth + 1);
                }
                duplicateKey = !objectKeys[objectDepth].insert(parsed.get<std::string>()).second || duplicateKey;
            }
            return !duplicateKey;
        };

    try
    {
        Json document = Json::parse(contents, callback, true, false);
        if (duplicateKey)
        {
            return Result<Json>::Failure(
                MakeMetadataError(ProjectMetadataErrors::DuplicateKey, "Project metadata contains a duplicate JSON key."));
        }
        return Result<Json>::Success(std::move(document));
    }
    catch (const Json::exception &exception)
    {
        return Result<Json>::Failure(MakeMetadataError(
            ProjectMetadataErrors::InvalidJson, "Project metadata is not valid JSON: " + std::string{exception.what()}));
    }
}
} // namespace

/** @copydoc LoadProjectMetadata */
Result<ProjectMetadata> LoadProjectMetadata(const std::filesystem::path &projectRoot)
{
    const std::filesystem::path metadataPath = projectRoot / ".horo/project.json";
    Result<std::string> contents = ReadMetadataFile(metadataPath);
    if (contents.HasError())
    {
        return Result<ProjectMetadata>::Failure(std::move(contents).ErrorValue());
    }

    Result<Json> parsed = ParseMetadataDocument(contents.Value());
    if (parsed.HasError())
    {
        return Result<ProjectMetadata>::Failure(std::move(parsed).ErrorValue());
    }
    const Json &document = parsed.Value();
    if (!HasRequiredSchema(document))
    {
        return Result<ProjectMetadata>::Failure(MakeMetadataError(
            ProjectMetadataErrors::InvalidSchema,
            "Project metadata is missing a valid formatVersion, identity, name, settings, or renderer."));
    }

    const std::string projectId = document["projectId"].get<std::string>();
    const std::string name = document["name"].get<std::string>();
    const std::string renderBackend = document["settings"]["renderBackend"].get<std::string>();
    if (projectId.size() > kMaximumProjectIdentityBytes || Text::IsBlank(projectId) ||
        name.size() > kMaximumProjectNameBytes || Text::IsBlank(name) || !IsCanonicalBackendId(renderBackend))
    {
        return Result<ProjectMetadata>::Failure(
            MakeMetadataError(ProjectMetadataErrors::InvalidValues,
                              "Project metadata contains an invalid identity, name, or renderer value."));
    }

    return Result<ProjectMetadata>::Success(ProjectMetadata{1, projectId, name, renderBackend});
}

/** @copydoc PreflightProjectOpen */
ProjectOpenPreflight PreflightProjectOpen(const std::filesystem::path &projectRoot,
                                          const RendererAvailabilitySnapshot &availability)
{
    const Result<ProjectMetadata> loaded = LoadProjectMetadata(projectRoot);
    if (loaded.HasError())
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::ProjectMetadataUnreadable,
                                    .diagnostic = loaded.ErrorValue().message};
    }

    const ProjectMetadata &metadata = loaded.Value();
    const RendererBackendAvailability *backend = availability.Find(metadata.renderBackend);
    if (backend == nullptr || backend->state == RendererAvailabilityState::NotInstalled)
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererNotInstalled,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = "Project renderer is not installed."};
    }
    if (backend->state == RendererAvailabilityState::RepairRequired ||
        backend->state == RendererAvailabilityState::SignatureInvalid ||
        backend->state == RendererAvailabilityState::Quarantined)
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererRepairRequired,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = backend->diagnostic};
    }
    if (backend->state == RendererAvailabilityState::UpdateRequired)
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererUpdateRequired,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = backend->diagnostic};
    }
    if (backend->state == RendererAvailabilityState::AbiMismatch)
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererCapabilityMismatch,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = backend->diagnostic};
    }
    if (!backend->IsSelectable())
    {
        const std::string diagnostic = backend != nullptr && !backend->diagnostic.empty()
                                           ? backend->diagnostic
                                           : "Project renderer is not available on this editor installation.";
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererUnavailable,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = diagnostic};
    }
    if (metadata.renderBackend != availability.ActiveBackendId())
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RequiresRendererRestart,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name};
    }
    return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::Ready,
                                .requiredBackendId = metadata.renderBackend,
                                .projectName = metadata.name};
}
} // namespace Horo::Editor
