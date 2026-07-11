#include "Horo/Editor/ProjectCreationService.h"

#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/Progress.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
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
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
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
    struct Operation {
        ProjectCreationSnapshot snapshot;
        ProgressTracker progress;
        JobId jobId = 0;
    };

    explicit ProjectCreationServiceState(EngineDataBus& bus) : dataBus(bus) {}

    [[nodiscard]] std::mutex& Mutex() const noexcept { return mutex; }

private:
    mutable std::mutex mutex;

public:
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
    std::lock_guard lock(state->Mutex());
    if (IsTerminal(operation->snapshot.state)) return;
    if (operation->snapshot.state != ProjectCreationOperationState::Cancelling)
        operation->snapshot.state = operationState;
    operation->snapshot.phase = phase;
    static_cast<void>(operation->progress.Report(progressPhase, progress));
    operation->snapshot.progress = operation->progress.Snapshot().value;
}

void SetFailure(const std::shared_ptr<ProjectCreationServiceState>& state,
                const std::shared_ptr<ProjectCreationServiceState::Operation>& operation,
                const ProjectCreationErrorCode code,
                std::string message) {
    std::lock_guard lock(state->Mutex());
    if (IsTerminal(operation->snapshot.state)) return;
    operation->snapshot.state = ProjectCreationOperationState::Failed;
    operation->snapshot.phase = ProjectCreationOperationPhase::Failed;
    operation->snapshot.error = ProjectCreationError{code, std::move(message)};
}

void SetCancelled(const std::shared_ptr<ProjectCreationServiceState>& state,
                  const std::shared_ptr<ProjectCreationServiceState::Operation>& operation) {
    std::lock_guard lock(state->Mutex());
    if (IsTerminal(operation->snapshot.state)) return;
    operation->snapshot.state = ProjectCreationOperationState::Cancelled;
    operation->snapshot.phase = ProjectCreationOperationPhase::Cancelled;
    operation->snapshot.error = ProjectCreationError{ProjectCreationErrorCode::Cancelled, "Project creation was cancelled."};
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

    UpdateProgress(state, operation, ProjectCreationOperationState::Running, ProjectCreationOperationPhase::Staging, "staging", 0.05F);
    if (IsCancelled(cancellation, state, operation)) return;
    if (std::filesystem::exists(destination, error)) {
        if (!std::filesystem::is_directory(destination, error) || !std::filesystem::is_empty(destination, error)) {
            SetFailure(state, operation, ProjectCreationErrorCode::DestinationOccupied, "Project destination became occupied before promotion.");
            return;
        }
    }
    if (error) {
        SetFailure(state, operation, ProjectCreationErrorCode::DestinationOccupied, "Project destination became occupied before promotion.");
        return;
    }
    std::filesystem::create_directories(parent, error);
    if (error) {
        SetFailure(state, operation, ProjectCreationErrorCode::ParentUnavailable, "Unable to create the project destination parent directory.");
        return;
    }
    std::filesystem::create_directory(staging, error);
    if (error) {
        SetFailure(state, operation, ProjectCreationErrorCode::StagingFailed, "Unable to create a sibling project staging directory.");
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
        std::lock_guard lock(state->Mutex());
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

[[nodiscard]] std::optional<Error> ValidateRequest(const ProjectCreationRequest& request) {
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
    : state_(std::make_shared<ProjectCreationServiceState>(dataBus)), jobs_(&jobs) {}

ProjectCreationService::~ProjectCreationService() = default;
ProjectCreationService::ProjectCreationService(ProjectCreationService&&) noexcept = default;
ProjectCreationService& ProjectCreationService::operator=(ProjectCreationService&&) noexcept = default;

Result<ProjectCreationOperationHandle> ProjectCreationService::StartCreate(ProjectCreationRequest request) {
    LOG_DEBUG("editor.project_creation", "StartCreate requested for project '%s' at path '%s', template '%s'", request.projectName.c_str(), request.projectRoot.string().c_str(), request.templateId.c_str());
    if (const auto validation = ValidateRequest(request)) {
        LOG_DEBUG("editor.project_creation", "StartCreate validation failed: %s", validation->message.c_str());
        return Result<ProjectCreationOperationHandle>::Failure(*validation);
    }

    auto operation = std::make_shared<ProjectCreationServiceState::Operation>();
    {
        std::lock_guard lock(state_->Mutex());
        operation->snapshot.id = state_->nextOperationId++;
        operation->snapshot.projectRoot = request.projectRoot;
        operation->snapshot.projectId = MakeProjectId(operation->snapshot.id);
        state_->operations.emplace(operation->snapshot.id, operation);
    }
    const auto submitted = jobs_->Submit(JobDescriptor{}, [state = state_, operation, request = std::move(request)](const CancellationToken& cancellation) {
        RunCreate(state, operation, request, cancellation);
    });
    if (submitted.HasError()) {
        std::lock_guard lock(state_->Mutex());
        state_->operations.erase(operation->snapshot.id);
        LOG_DEBUG("editor.project_creation", "StartCreate job submission failed: %s", submitted.ErrorValue().message.c_str());
        return Result<ProjectCreationOperationHandle>::Failure(MakeFoundationError("project_creation.job_submission_failed", submitted.ErrorValue().message.c_str()));
    }
    {
        std::lock_guard lock(state_->Mutex());
        operation->jobId = submitted.Value().Id();
    }
    LOG_DEBUG("editor.project_creation", "Job %llu dispatched successfully for operation %llu (projectId=%s)", static_cast<unsigned long long>(operation->jobId), static_cast<unsigned long long>(operation->snapshot.id), operation->snapshot.projectId.c_str());
    return Result<ProjectCreationOperationHandle>::Success(ProjectCreationOperationHandle{operation->snapshot.id});
}

std::optional<ProjectCreationSnapshot> ProjectCreationService::Query(const ProjectCreationOperationId id) const {
    std::lock_guard lock(state_->Mutex());
    const auto found = state_->operations.find(id);
    if (found == state_->operations.end()) return std::nullopt;
    return found->second->snapshot;
}

Result<void> ProjectCreationService::RequestCancel(const ProjectCreationOperationId id) {
    JobId jobId = 0;
    std::shared_ptr<ProjectCreationServiceState::Operation> operation;
    {
        std::lock_guard lock(state_->Mutex());
        const auto found = state_->operations.find(id);
        if (found == state_->operations.end()) return Result<void>::Failure(MakeFoundationError("project_creation.not_found", "Project creation operation is not known."));
        operation = found->second;
        if (IsTerminal(operation->snapshot.state)) return Result<void>::Success();
        jobId = operation->jobId;
        operation->snapshot.state = ProjectCreationOperationState::Cancelling;
    }
    if (jobId == 0) return Result<void>::Failure(MakeFoundationError("project_creation.not_ready", "Project creation operation is not ready for cancellation."));
    const auto cancelled = jobs_->RequestCancel(jobId);
    if (cancelled.HasError()) return cancelled;
    if (jobs_->Query(jobId).state == JobState::Cancelled) SetCancelled(state_, operation);
    return Result<void>::Success();
}

void ProjectCreationService::PumpMainThread() {
    state_->dataBus.DispatchQueued();
}

} // namespace Horo::Editor
