#include "Horo/Editor/ProjectCreationService.h"

#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/Progress.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#if defined(__APPLE__)
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace Horo::Editor {
namespace {

[[nodiscard]] Error MakeFoundationError(const char* code, const char* message) {
    return Error{.code = ErrorCode{code},
                 .domain = ErrorDomainId{"horo.editor.project_creation"},
                 .severity = ErrorSeverity::Error,
                 .message = message};
}

[[nodiscard]] bool IsBlank(const std::string& value) {
    for (const unsigned char character : value) {
        if (!std::isspace(character)) return false;
    }
    return true;
}

[[nodiscard]] bool IsSafeProjectRelativePath(const std::string& value) {
    const std::filesystem::path path{value};
    if (value.empty() || path.is_absolute()) return false;
    for (const auto& component : path) {
        if (component == "..") return false;
    }
    return true;
}

[[nodiscard]] std::string EscapeJson(const std::string_view value) {
    std::ostringstream escaped;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (character < 0x20) {
                static constexpr char hex[] = "0123456789abcdef";
                escaped << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
            } else {
                escaped << static_cast<char>(character);
            }
        }
    }
    return escaped.str();
}

[[nodiscard]] std::string MakeProjectId(const ProjectCreationOperationId operationId) {
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream value;
    value << "proj_" << std::hex << static_cast<std::uint64_t>(now) << operationId;
    return value.str();
}

[[nodiscard]] std::string UtcTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::string buffer(32, '\0');
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
    buffer.resize(std::strlen(buffer.c_str()));
    return buffer;
}

[[nodiscard]] bool WriteDurableFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) return false;
    output.close();
#if defined(__APPLE__) || defined(__linux__)
    const int descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0) return false;
    const bool synced = fsync(descriptor) == 0;
    close(descriptor);
    return synced;
#else
    return true;
#endif
}

[[nodiscard]] bool PromoteWithoutReplace(const std::filesystem::path& staging,
                                         const std::filesystem::path& destination,
                                         std::error_code& error) {
    if (std::filesystem::exists(destination, error)) {
        if (std::filesystem::is_directory(destination, error) && std::filesystem::is_empty(destination, error)) {
            std::filesystem::remove(destination, error);
            if (error) return false;
        } else {
            error = std::make_error_code(std::errc::file_exists);
            return false;
        }
    }
    if (error) return false;
#if defined(__APPLE__)
    if (renameatx_np(AT_FDCWD, staging.c_str(), AT_FDCWD, destination.c_str(), RENAME_EXCL) == 0) return true;
    error = std::error_code(errno, std::generic_category());
    return false;
#elif defined(__linux__) && defined(SYS_renameat2)
    if (syscall(SYS_renameat2, AT_FDCWD, staging.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) == 0) return true;
    error = std::error_code(errno, std::generic_category());
    return false;
#else
    if (std::filesystem::exists(destination, error) || error) return false;
    std::filesystem::rename(staging, destination, error);
    return !error;
#endif
}

[[nodiscard]] bool IsTerminal(const ProjectCreationOperationState state) noexcept {
    return state == ProjectCreationOperationState::Succeeded ||
           state == ProjectCreationOperationState::Failed ||
           state == ProjectCreationOperationState::Cancelled;
}

} // namespace

struct ProjectCreationServiceState {
    class Synchronization {
    public:
        [[nodiscard]] std::mutex& Mutex() const noexcept { return mutex; }

    private:
        mutable std::mutex mutex;
    } synchronization;

    struct Operation {
        ProjectCreationSnapshot snapshot;
        ProgressTracker progress;
        JobId jobId = 0;
    };

    explicit ProjectCreationServiceState(EngineDataBus& bus) : dataBus(bus) {}

    EngineDataBus& dataBus;
    ProjectCreationOperationId nextOperationId = 1;
    ProjectCreationRevision revision = 0;
    std::unordered_map<ProjectCreationOperationId, std::shared_ptr<Operation>> operations;
};

namespace {

void UpdateProgress(const std::shared_ptr<ProjectCreationServiceState>& state,
                    const std::shared_ptr<ProjectCreationServiceState::Operation>& operation,
                    const ProjectCreationOperationState operationState,
                    const ProjectCreationOperationPhase phase,
                    const std::string_view progressPhase,
                    const float progress) {
    ProjectCreationProgressEvent progressEvent;
    {
        std::lock_guard lock(state->synchronization.Mutex());
        if (IsTerminal(operation->snapshot.state)) return;
        if (operation->snapshot.state != ProjectCreationOperationState::Cancelling)
            operation->snapshot.state = operationState;
        operation->snapshot.phase = phase;
        static_cast<void>(operation->progress.Report(progressPhase, progress));
        operation->snapshot.progress = operation->progress.Snapshot().value;
        progressEvent.operationId = operation->snapshot.id;
        progressEvent.phase = phase;
        progressEvent.progress = operation->snapshot.progress;
        progressEvent.revision = ++state->revision;
    }
    state->dataBus.PublishAsync(progressEvent);
}

void SetFailure(const std::shared_ptr<ProjectCreationServiceState>& state,
                const std::shared_ptr<ProjectCreationServiceState::Operation>& operation,
                const ProjectCreationErrorCode code,
                std::string message) {
    ProjectCreationRevisionChangedEvent revisionEvent;
    {
        std::lock_guard lock(state->synchronization.Mutex());
        if (IsTerminal(operation->snapshot.state)) return;
        operation->snapshot.state = ProjectCreationOperationState::Failed;
        operation->snapshot.phase = ProjectCreationOperationPhase::Failed;
        operation->snapshot.error = ProjectCreationError{code, std::move(message)};
        revisionEvent.revision = ++state->revision;
    }
    LOG_ERROR("editor.project_creation", "Operation %llu failed [code=%d]: %s",
              static_cast<unsigned long long>(operation->snapshot.id),
              static_cast<int>(code),
              operation->snapshot.error->message.c_str());
    state->dataBus.PublishAsync(revisionEvent);
}

void SetCancelled(const std::shared_ptr<ProjectCreationServiceState>& state,
                  const std::shared_ptr<ProjectCreationServiceState::Operation>& operation) {
    ProjectCreationRevisionChangedEvent revisionEvent;
    {
        std::lock_guard lock(state->synchronization.Mutex());
        if (IsTerminal(operation->snapshot.state)) return;
        operation->snapshot.state = ProjectCreationOperationState::Cancelled;
        operation->snapshot.phase = ProjectCreationOperationPhase::Cancelled;
        operation->snapshot.error = ProjectCreationError{ProjectCreationErrorCode::Cancelled, "Project creation was cancelled."};
        revisionEvent.revision = ++state->revision;
    }
    LOG_INFO("editor.project_creation", "Operation %llu cancelled.",
             static_cast<unsigned long long>(operation->snapshot.id));
    state->dataBus.PublishAsync(revisionEvent);
}

[[nodiscard]] bool IsCancelled(const CancellationToken& cancellation,
                               const std::shared_ptr<ProjectCreationServiceState>& state,
                               const std::shared_ptr<ProjectCreationServiceState::Operation>& operation) {
    if (!cancellation.IsCancellationRequested()) return false;
    SetCancelled(state, operation);
    return true;
}

[[nodiscard]] std::string ProjectJson(const ProjectCreationRequest& request, const std::string& projectId) {
    std::ostringstream json;
    json << "{\n"
         << "  \"formatVersion\": 1,\n"
         << "  \"projectId\": \"" << EscapeJson(projectId) << "\",\n"
         << "  \"name\": \"" << EscapeJson(request.projectName) << "\",\n"
         << "  \"projectVersion\": \"" << EscapeJson(request.projectVersion) << "\",\n"
         << "  \"createdAt\": \"" << UtcTimestamp() << "\",\n"
         << "  \"settings\": {\n"
         << "    \"renderBackend\": \"" << EscapeJson(request.renderBackend) << "\",\n"
         << "    \"physicsEnabled\": " << (request.physicsEnabled ? "true" : "false") << ",\n"
         << "    \"targetFrameRate\": " << request.targetFrameRate << ",\n"
         << "    \"defaultScene\": \"" << EscapeJson(request.defaultScene) << "\",\n"
         << "    \"buildProfile\": \"" << EscapeJson(request.buildProfile) << "\",\n"
         << "    \"requiredToolchain\": {\n"
         << "      \"targetPlatform\": \"" << EscapeJson(request.targetPlatform) << "\",\n"
         << "      \"compilerFamily\": \"" << EscapeJson(request.compilerFamily) << "\",\n"
         << "      \"minimumCxxStandard\": " << request.minimumCxxStandard << "\n"
         << "    }\n"
         << "  }\n"
         << "}\n";
    return json.str();
}

struct StagingPreparationFailure {
    ProjectCreationErrorCode code;
    const char* message;
};

[[nodiscard]] std::optional<StagingPreparationFailure> PrepareStagingDirectory(
    const std::filesystem::path& destination,
    const std::filesystem::path& parent,
    const std::filesystem::path& staging,
    std::error_code& error) {
    if (std::filesystem::exists(destination, error)) {
        if (!std::filesystem::is_directory(destination, error) || !std::filesystem::is_empty(destination, error))
            return StagingPreparationFailure{ProjectCreationErrorCode::DestinationOccupied, "Project destination became occupied before promotion."};
    }
    if (error)
        return StagingPreparationFailure{ProjectCreationErrorCode::DestinationOccupied, "Project destination became occupied before promotion."};
    std::filesystem::create_directories(parent, error);
    if (error)
        return StagingPreparationFailure{ProjectCreationErrorCode::ParentUnavailable, "Unable to create the project destination parent directory."};
    std::filesystem::create_directory(staging, error);
    if (error)
        return StagingPreparationFailure{ProjectCreationErrorCode::StagingFailed, "Unable to create a sibling project staging directory."};
    return std::nullopt;
}

void RunCreate(const std::shared_ptr<ProjectCreationServiceState>& state,
               const std::shared_ptr<ProjectCreationServiceState::Operation>& operation,
               const ProjectCreationRequest request,
               const CancellationToken& cancellation) {
    const std::filesystem::path destination = request.projectRoot;
    const std::filesystem::path parent = destination.has_parent_path()
                                             ? destination.parent_path()
                                             : std::filesystem::current_path();
    const std::filesystem::path staging = parent / ("." + destination.filename().string() + ".horo-create-" + std::to_string(operation->snapshot.id));
    std::error_code error;
    auto cleanup = [&] {
        std::filesystem::remove_all(staging, error);
    };

    LOG_DEBUG("editor.project_creation", "Starting worker execution for operation %llu. Destination: '%s'", static_cast<unsigned long long>(operation->snapshot.id), destination.string().c_str());
    LOG_DEBUG("editor.project_creation", "Resolved parent directory: '%s', staging directory: '%s'", parent.string().c_str(), staging.string().c_str());

    UpdateProgress(state, operation, ProjectCreationOperationState::Running, ProjectCreationOperationPhase::Validating, "validating", 0.02F);
    if (IsCancelled(cancellation, state, operation)) return;

    if (std::filesystem::exists(destination, error)) {
        if (!std::filesystem::is_directory(destination, error) || !std::filesystem::is_empty(destination, error)) {
            SetFailure(state, operation, ProjectCreationErrorCode::DestinationOccupied, "Project destination already exists and will not be overwritten.");
            return;
        }
    }
    if (error) {
        SetFailure(state, operation, ProjectCreationErrorCode::InvalidRequest, "Project destination cannot be inspected.");
        return;
    }

    UpdateProgress(state, operation, ProjectCreationOperationState::Running, ProjectCreationOperationPhase::Staging, "staging", 0.05F);
    if (IsCancelled(cancellation, state, operation)) return;
    if (const auto failure = PrepareStagingDirectory(destination, parent, staging, error)) {
        SetFailure(state, operation, failure->code, failure->message);
        return;
    }

    UpdateProgress(state, operation, ProjectCreationOperationState::Running, ProjectCreationOperationPhase::WritingMetadata, "metadata", 0.20F);
    if (IsCancelled(cancellation, state, operation)) { cleanup(); return; }
    LOG_DEBUG("editor.project_creation", "Writing metadata files (.horo/project.json, .horo/plugins.json). ProjectId: '%s'", operation->snapshot.projectId.c_str());
    std::filesystem::create_directories(staging / ".horo", error);
    if (error || !WriteDurableFile(staging / ".horo/project.json", ProjectJson(request, operation->snapshot.projectId)) ||
        !WriteDurableFile(staging / ".horo/plugins.json", "{\n  \"schemaVersion\": 1,\n  \"requestedPlugins\": []\n}\n")) {
        cleanup();
        SetFailure(state, operation, ProjectCreationErrorCode::WriteFailed, "Unable to write project metadata into the staging directory.");
        return;
    }

    UpdateProgress(state, operation, ProjectCreationOperationState::Running, ProjectCreationOperationPhase::WritingScaffolding, "scaffolding", 0.50F);
    if (IsCancelled(cancellation, state, operation)) { cleanup(); return; }
    LOG_DEBUG("editor.project_creation", "Scaffolding asset subdirectories (assets/models, textures, materials, shaders, scenes) for template '%s'", request.templateId.c_str());
    for (const char* directory : {"assets/models", "assets/textures", "assets/materials", "assets/shaders", "assets/scenes"}) {
        std::filesystem::create_directories(staging / directory, error);
        if (error) {
            cleanup();
            SetFailure(state, operation, ProjectCreationErrorCode::WriteFailed, "Unable to create project asset scaffolding.");
            return;
        }
    }
    if (request.includeStarterContent) {
        const std::filesystem::path starterScene = staging / request.defaultScene;
        LOG_DEBUG("editor.project_creation", "Writing starter scene at '%s'", starterScene.string().c_str());
        std::filesystem::create_directories(starterScene.parent_path(), error);
        if (error || !WriteDurableFile(starterScene, "{\n  \"schemaVersion\": 1,\n  \"objects\": []\n}\n")) {
            cleanup();
            SetFailure(state, operation, ProjectCreationErrorCode::WriteFailed, "Unable to write the requested starter scene.");
            return;
        }
    }
    if (request.initializeGit) {
        LOG_DEBUG("editor.project_creation", "Writing .gitignore file for initial git tracking");
        if (!WriteDurableFile(staging / ".gitignore", "build/\n.horo/local/\n.horo/editor_workspace.json\n.horo/asset_index.json\n")) {
            cleanup();
            SetFailure(state, operation, ProjectCreationErrorCode::WriteFailed, "Unable to write the project git ignore file.");
            return;
        }
    }
    if (request.generateCMakeProject) {
        LOG_DEBUG("editor.project_creation", "Generating CMakeLists.txt project file");
        if (!WriteDurableFile(staging / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.25)\nproject(HoroGame LANGUAGES CXX)\n")) {
            cleanup();
            SetFailure(state, operation, ProjectCreationErrorCode::WriteFailed, "Unable to write the project CMake file.");
            return;
        }
    }

    UpdateProgress(state, operation, ProjectCreationOperationState::Running, ProjectCreationOperationPhase::Promoting, "promoting", 0.90F);
    if (IsCancelled(cancellation, state, operation)) { cleanup(); return; }
    LOG_DEBUG("editor.project_creation", "Promoting staging directory '%s' to target destination '%s'", staging.string().c_str(), destination.string().c_str());
    if (!PromoteWithoutReplace(staging, destination, error)) {
        cleanup();
        const ProjectCreationErrorCode code = error == std::errc::file_exists
                                                  ? ProjectCreationErrorCode::DestinationOccupied
                                                  : ProjectCreationErrorCode::PromotionFailed;
        SetFailure(state, operation, code, "Unable to atomically promote the staged project directory.");
        return;
    }

    ProjectCreatedEvent created;
    ProjectCreationRevisionChangedEvent changed;
    {
        std::lock_guard lock(state->synchronization.Mutex());
        operation->snapshot.state = ProjectCreationOperationState::Succeeded;
        operation->snapshot.phase = ProjectCreationOperationPhase::Completed;
        static_cast<void>(operation->progress.Report("completed", 1.0F));
        operation->snapshot.progress = operation->progress.Snapshot().value;
        created.operationId = operation->snapshot.id;
        created.projectId = operation->snapshot.projectId;
        created.revision = ++state->revision;
        changed.revision = created.revision;
    }
    LOG_DEBUG("editor.project_creation", "Operation %llu completed cleanly. Publishing ProjectCreatedEvent (revision %llu)", static_cast<unsigned long long>(created.operationId), static_cast<unsigned long long>(created.revision));
    state->dataBus.PublishAsync(created);
    state->dataBus.PublishAsync(changed);
}

[[nodiscard]] std::optional<Error> ValidateRequestSync(const ProjectCreationRequest& request) {
    if (IsBlank(request.templateId) || IsBlank(request.projectName) || request.projectName.find('/') != std::string::npos || request.projectName.find('\\') != std::string::npos)
        return MakeFoundationError("project_creation.invalid_request", "Project name is required and must not contain path separators.");
    if (request.projectRoot.empty() || request.projectRoot.filename().empty())
        return MakeFoundationError("project_creation.invalid_request", "Project root must name a destination directory.");
    if (!IsSafeProjectRelativePath(request.defaultScene) || std::filesystem::path(request.defaultScene).filename().empty())
        return MakeFoundationError("project_creation.invalid_request", "Default scene must be a portable project-relative path.");
    if (request.targetFrameRate <= 0 || request.minimumCxxStandard < 20)
        return MakeFoundationError("project_creation.invalid_request", "Project numeric settings are outside supported bounds.");
    std::error_code error;
    if (std::filesystem::exists(request.projectRoot, error)) {
        if (!std::filesystem::is_directory(request.projectRoot, error) || !std::filesystem::is_empty(request.projectRoot, error)) {
            return MakeFoundationError("project_creation.destination_occupied", "Project destination already exists and will not be overwritten.");
        }
    }
    if (error)
        return MakeFoundationError("project_creation.invalid_request", "Project destination cannot be inspected.");
    return std::nullopt;
}

} // namespace

ProjectCreationService::ProjectCreationService(JobSystem& jobs, EngineDataBus& dataBus)
    : state_(std::make_shared<ProjectCreationServiceState>(dataBus)), jobs_(jobs) {}

Result<ProjectCreationOperationHandle> ProjectCreationService::StartCreate(ProjectCreationRequest request) {
    LOG_DEBUG("editor.project_creation", "StartCreate requested for project '%s' at path '%s', template '%s'", request.projectName.c_str(), request.projectRoot.string().c_str(), request.templateId.c_str());
    if (const auto validation = ValidateRequestSync(request)) {
        LOG_DEBUG("editor.project_creation", "StartCreate validation failed: %s", validation->message.c_str());
        return Result<ProjectCreationOperationHandle>::Failure(*validation);
    }

    auto operation = std::make_shared<ProjectCreationServiceState::Operation>();
    {
        std::lock_guard lock(state_->synchronization.Mutex());
        operation->snapshot.id = state_->nextOperationId++;
        operation->snapshot.projectRoot = request.projectRoot;
        operation->snapshot.projectId = MakeProjectId(operation->snapshot.id);
        state_->operations.emplace(operation->snapshot.id, operation);
    }
    const auto submitted = jobs_.get().Submit(JobDescriptor{}, [state = state_, operation, request = std::move(request)](const CancellationToken& cancellation) {
        RunCreate(state, operation, request, cancellation);
    });
    if (submitted.HasError()) {
        std::lock_guard lock(state_->synchronization.Mutex());
        state_->operations.erase(operation->snapshot.id);
        LOG_DEBUG("editor.project_creation", "StartCreate job submission failed: %s", submitted.ErrorValue().message.c_str());
        return Result<ProjectCreationOperationHandle>::Failure(MakeFoundationError("project_creation.job_submission_failed", submitted.ErrorValue().message.c_str()));
    }
    {
        std::lock_guard lock(state_->synchronization.Mutex());
        operation->jobId = submitted.Value().Id();
    }
    LOG_INFO("editor.project_creation", "Project creation started: '%s' path='%s' (op=%llu job=%llu).",
             operation->snapshot.projectId.c_str(),
             operation->snapshot.projectRoot.string().c_str(),
             static_cast<unsigned long long>(operation->snapshot.id),
             static_cast<unsigned long long>(operation->jobId));
    return Result<ProjectCreationOperationHandle>::Success(ProjectCreationOperationHandle{operation->snapshot.id});
}

std::optional<ProjectCreationSnapshot> ProjectCreationService::Query(const ProjectCreationOperationId id) const {
    std::lock_guard lock(state_->synchronization.Mutex());
    const auto found = state_->operations.find(id);
    if (found == state_->operations.end()) return std::nullopt;
    return found->second->snapshot;
}

Result<void> ProjectCreationService::RequestCancel(const ProjectCreationOperationId id) {
    JobId jobId = 0;
    std::shared_ptr<ProjectCreationServiceState::Operation> operation;
    {
        std::lock_guard lock(state_->synchronization.Mutex());
        const auto found = state_->operations.find(id);
        if (found == state_->operations.end()) {
            LOG_WARN("editor.project_creation", "RequestCancel: operation %llu not found.",
                     static_cast<unsigned long long>(id));
            return Result<void>::Failure(MakeFoundationError("project_creation.not_found", "Project creation operation is not known."));
        }
        operation = found->second;
        if (IsTerminal(operation->snapshot.state)) {
            LOG_DEBUG("editor.project_creation", "RequestCancel: operation %llu already in terminal state.",
                      static_cast<unsigned long long>(id));
            return Result<void>::Success();
        }
        jobId = operation->jobId;
        operation->snapshot.state = ProjectCreationOperationState::Cancelling;
    }
    if (jobId == 0) return Result<void>::Failure(MakeFoundationError("project_creation.not_ready", "Project creation operation is not ready for cancellation."));
    const auto cancelled = jobs_.get().RequestCancel(jobId);
    if (cancelled.HasError()) return cancelled;
    if (jobs_.get().Query(jobId).state == JobState::Cancelled) SetCancelled(state_, operation);
    LOG_INFO("editor.project_creation", "Cancellation requested for operation %llu (job=%llu).",
             static_cast<unsigned long long>(id),
             static_cast<unsigned long long>(jobId));
    return Result<void>::Success();
}

void ProjectCreationService::PumpMainThread() {
    state_->dataBus.DispatchQueued();
}

} // namespace Horo::Editor
