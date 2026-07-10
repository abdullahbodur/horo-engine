#include "Horo/Editor/ProjectCreationScreen.h"

#include <cctype>
#include <system_error>
#include <utility>

/**
 * @file ProjectCreationScreen.cpp
 * @brief Implementation of the project-creation screen controller and inspection logic.
 */

namespace Horo::Editor {
namespace {

/**
 * @brief Checks if a string is empty or contains only whitespace.
 * @param value String to inspect.
 * @return True if blank.
 */
[[nodiscard]] bool IsBlank(const std::string& value) {
    for (const unsigned char character : value) {
        if (!std::isspace(character)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Checks if a string contains file path separators.
 * @param value String to inspect.
 * @return True if separator found.
 */
[[nodiscard]] bool HasPathSeparator(const std::string& value) {
    return value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
}

/**
 * @brief Checks if a file status contains any write permissions.
 * @param permissions Filesystem permissions to check.
 * @return True if owner, group, or other write is permitted.
 */
[[nodiscard]] bool HasAnyWritePermission(const std::filesystem::perms permissions) {
    constexpr auto writeBits = std::filesystem::perms::owner_write |
                               std::filesystem::perms::group_write |
                               std::filesystem::perms::others_write;
    return (permissions & writeBits) != std::filesystem::perms::none;
}

/**
 * @brief Traverses up from a path to find the nearest existing directory.
 * @param path Start path.
 * @return The first existing ancestor, or empty if root reached without success.
 */
[[nodiscard]] std::filesystem::path FindNearestExistingParent(std::filesystem::path path) {
    std::error_code error;
    while (!path.empty()) {
        if (std::filesystem::exists(path, error)) {
            return error ? std::filesystem::path{} : path;
        }
        if (error && error != std::errc::no_such_file_or_directory) {
            return {};
        }
        error.clear();
        const std::filesystem::path parent = path.parent_path();
        if (parent == path) {
            return {};
        }
        path = parent;
    }
    return {};
}

/** 
 * @brief Converts a valid draft into its service-facing request without performing I/O. 
 * @param draft Current project settings.
 * @return The constructed request.
 */
[[nodiscard]] ProjectCreationRequest MakeRequest(const ProjectCreationDraft& draft) {
    return {draft.templateId,
            draft.projectName,
            std::filesystem::path{draft.projectPath},
            draft.projectVersion,
            draft.defaultScene,
            draft.renderBackend,
            draft.physicsEnabled,
            draft.targetFrameRate,
            draft.buildProfile,
            draft.assetCompression,
            draft.textureCompression,
            draft.targetPlatform,
            draft.compilerFamily,
            draft.minimumCxxStandard,
            draft.initializeGit,
            draft.restorePackages,
            draft.includeStarterContent,
            draft.generateCMakeProject};
}

} // namespace

/** @copydoc InspectProjectCreationLocation */
ProjectCreationLocation InspectProjectCreationLocation(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        return {ProjectCreationLocationKind::Inaccessible, {}, false};
    }

    if (std::filesystem::exists(status)) {
        if (!std::filesystem::is_directory(status)) {
            return {ProjectCreationLocationKind::ExistingNonDirectory, path.parent_path(), false};
        }

        const bool parentAppearsWritable = HasAnyWritePermission(status.permissions());
        const bool isEmpty = std::filesystem::is_empty(path, error);
        if (error) {
            return {ProjectCreationLocationKind::Inaccessible, path, parentAppearsWritable};
        }
        return {isEmpty ? ProjectCreationLocationKind::EmptyDirectory : ProjectCreationLocationKind::OccupiedDirectory,
                path,
                parentAppearsWritable};
    }

    const std::filesystem::path parent = FindNearestExistingParent(path.parent_path());
    if (parent.empty()) {
        return {ProjectCreationLocationKind::Inaccessible, {}, false};
    }

    const std::filesystem::file_status parentStatus = std::filesystem::status(parent, error);
    if (error || !std::filesystem::is_directory(parentStatus)) {
        return {ProjectCreationLocationKind::Inaccessible, parent, false};
    }
    return {ProjectCreationLocationKind::Missing, parent, HasAnyWritePermission(parentStatus.permissions())};
}

ProjectCreationController::ProjectCreationController()
    : initialDraft_{}, draft_{initialDraft_} {}

/** @copydoc ProjectCreationController::Draft */
const ProjectCreationDraft& ProjectCreationController::Draft() const noexcept {
    return draft_;
}

/** @copydoc ProjectCreationController::SetProjectName */
void ProjectCreationController::SetProjectName(std::string projectName) {
    draft_.projectName = std::move(projectName);
}

/** @copydoc ProjectCreationController::SetProjectPath */
void ProjectCreationController::SetProjectPath(std::string projectPath) {
    draft_.projectPath = std::move(projectPath);
}

/** @copydoc ProjectCreationController::SetTemplateId */
void ProjectCreationController::SetTemplateId(std::string templateId) {
    draft_.templateId = std::move(templateId);
}

/** @copydoc ProjectCreationController::MutableDraft */
ProjectCreationDraft& ProjectCreationController::MutableDraft() noexcept {
    return draft_;
}

/** @copydoc ProjectCreationController::SetProjectVersion */
void ProjectCreationController::SetProjectVersion(std::string projectVersion) {
    draft_.projectVersion = std::move(projectVersion);
}

/** @copydoc ProjectCreationController::SetDefaultScene */
void ProjectCreationController::SetDefaultScene(std::string defaultScene) {
    draft_.defaultScene = std::move(defaultScene);
}

/** @copydoc ProjectCreationController::SetRenderBackend */
void ProjectCreationController::SetRenderBackend(std::string renderBackend) {
    draft_.renderBackend = std::move(renderBackend);
}

/** @copydoc ProjectCreationController::SetPhysicsEnabled */
void ProjectCreationController::SetPhysicsEnabled(const bool physicsEnabled) {
    draft_.physicsEnabled = physicsEnabled;
}

/** @copydoc ProjectCreationController::SetTargetFrameRate */
void ProjectCreationController::SetTargetFrameRate(const int targetFrameRate) {
    draft_.targetFrameRate = targetFrameRate;
}

/** @copydoc ProjectCreationController::SetBuildProfile */
void ProjectCreationController::SetBuildProfile(std::string buildProfile) {
    draft_.buildProfile = std::move(buildProfile);
}

/** @copydoc ProjectCreationController::SetAssetCompression */
void ProjectCreationController::SetAssetCompression(std::string assetCompression) {
    draft_.assetCompression = std::move(assetCompression);
}

/** @copydoc ProjectCreationController::SetTextureCompression */
void ProjectCreationController::SetTextureCompression(std::string textureCompression) {
    draft_.textureCompression = std::move(textureCompression);
}

/** @copydoc ProjectCreationController::SetTargetPlatform */
void ProjectCreationController::SetTargetPlatform(std::string targetPlatform) {
    draft_.targetPlatform = std::move(targetPlatform);
}

/** @copydoc ProjectCreationController::SetCompilerFamily */
void ProjectCreationController::SetCompilerFamily(std::string compilerFamily) {
    draft_.compilerFamily = std::move(compilerFamily);
}

/** @copydoc ProjectCreationController::SetMinimumCxxStandard */
void ProjectCreationController::SetMinimumCxxStandard(const int minimumCxxStandard) {
    draft_.minimumCxxStandard = minimumCxxStandard;
}

/** @copydoc ProjectCreationController::SetInitializeGit */
void ProjectCreationController::SetInitializeGit(const bool initializeGit) {
    draft_.initializeGit = initializeGit;
}

/** @copydoc ProjectCreationController::SetRestorePackages */
void ProjectCreationController::SetRestorePackages(const bool restorePackages) {
    draft_.restorePackages = restorePackages;
}

/** @copydoc ProjectCreationController::SetIncludeStarterContent */
void ProjectCreationController::SetIncludeStarterContent(const bool includeStarterContent) {
    draft_.includeStarterContent = includeStarterContent;
}

/** @copydoc ProjectCreationController::SetGenerateCMakeProject */
void ProjectCreationController::SetGenerateCMakeProject(const bool generateCMakeProject) {
    draft_.generateCMakeProject = generateCMakeProject;
}

/** @copydoc ProjectCreationController::Validate */
ProjectCreationValidation ProjectCreationController::Validate() const {
    ProjectCreationValidation validation;
    if (IsBlank(draft_.projectName)) {
        validation.diagnostics.push_back({ProjectCreationDiagnosticCode::ProjectNameRequired,
                                          "Project name is required."});
    } else if (HasPathSeparator(draft_.projectName)) {
        validation.diagnostics.push_back({ProjectCreationDiagnosticCode::ProjectNameContainsPathSeparator,
                                          "Project name must not contain path separators."});
    }

    if (IsBlank(draft_.projectPath)) {
        validation.diagnostics.push_back({ProjectCreationDiagnosticCode::ProjectPathRequired,
                                          "Project location is required."});
        return validation;
    }

    const ProjectCreationLocation location = InspectProjectCreationLocation(draft_.projectPath);
    switch (location.kind) {
    case ProjectCreationLocationKind::OccupiedDirectory:
        validation.diagnostics.push_back({ProjectCreationDiagnosticCode::ProjectPathOccupied,
                                          "Project directory already contains files; choose an empty folder or import it."});
        break;
    case ProjectCreationLocationKind::ExistingNonDirectory:
        validation.diagnostics.push_back({ProjectCreationDiagnosticCode::ProjectPathNotDirectory,
                                          "Project location exists but is not a directory."});
        break;
    case ProjectCreationLocationKind::Inaccessible:
        validation.diagnostics.push_back({ProjectCreationDiagnosticCode::ProjectPathInaccessible,
                                          "Project location cannot be inspected."});
        break;
    case ProjectCreationLocationKind::EmptyDirectory:
    case ProjectCreationLocationKind::Missing:
        if (!location.parentAppearsWritable) {
            validation.diagnostics.push_back({ProjectCreationDiagnosticCode::ProjectParentNotWritable,
                                              "Project location's parent does not appear writable."});
        }
        break;
    }
    return validation;
}

/** @copydoc ProjectCreationController::BuildCreationRequest */
std::optional<ProjectCreationRequest> ProjectCreationController::BuildCreationRequest() const {
    if (!Validate().IsValid()) {
        return std::nullopt;
    }
    return MakeRequest(draft_);
}

/** @copydoc ProjectCreationController::IsDirty */
bool ProjectCreationController::IsDirty() const noexcept {
    return draft_ != initialDraft_;
}

/** @copydoc ProjectCreationController::LeaveIntent */
ProjectCreationLeaveIntent ProjectCreationController::LeaveIntent() const noexcept {
    return IsDirty() ? ProjectCreationLeaveIntent::RequireDiscardConfirmation : ProjectCreationLeaveIntent::Allow;
}

/** @copydoc ProjectCreationController::DiscardDraft */
void ProjectCreationController::DiscardDraft() {
    draft_ = initialDraft_;
}

} // namespace Horo::Editor
