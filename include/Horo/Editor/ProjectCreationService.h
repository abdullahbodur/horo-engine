#pragma once

#include "Horo/Editor/ProjectCreationScreen.h"
#include "Horo/Foundation/Result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Horo {
class EngineDataBus;
class JobSystem;
}

namespace Horo::Editor {

struct ProjectCreationServiceState;

/** @file ProjectCreationService.h
 * @brief Job-backed, durable project-creation operation contract.
 */

using ProjectCreationOperationId = std::uint64_t;
using ProjectCreationRevision = std::uint64_t;

/** @brief User-facing lifecycle state of one project creation operation. */
enum class ProjectCreationOperationState : std::uint8_t {
    Queued,
    Running,
    Cancelling,
    Succeeded,
    Failed,
    Cancelled,
};

/** @brief Named creation phase reported by the authoritative operation snapshot. */
enum class ProjectCreationOperationPhase : std::uint8_t {
    Validating,
    Staging,
    WritingMetadata,
    WritingScaffolding,
    Promoting,
    Completed,
    Cancelled,
    Failed,
};

/** @brief Stable project-creation failure classification. */
enum class ProjectCreationErrorCode : std::uint8_t {
    InvalidRequest,
    DestinationOccupied,
    ParentUnavailable,
    StagingFailed,
    WriteFailed,
    PromotionFailed,
    Cancelled,
    JobSubmissionFailed,
};

/** @brief Typed terminal diagnostic retained in a project creation snapshot. */
struct ProjectCreationError {
    ProjectCreationErrorCode code = ProjectCreationErrorCode::InvalidRequest;
    std::string message;
};

/** @brief Immutable view of one project creation operation held by ProjectCreationService. */
struct ProjectCreationSnapshot {
    ProjectCreationOperationId id = 0;
    ProjectCreationOperationState state = ProjectCreationOperationState::Queued;
    ProjectCreationOperationPhase phase = ProjectCreationOperationPhase::Validating;
    float progress = 0.0F;
    std::filesystem::path projectRoot;
    std::string projectId;
    std::optional<ProjectCreationError> error;
};

/** @brief Handle returned after a project-creation operation was accepted by JobSystem. */
struct ProjectCreationOperationHandle {
    ProjectCreationOperationId id = 0;
};

/** @brief Lightweight notification emitted only after a project directory is promoted. */
struct ProjectCreatedEvent {
    static constexpr std::string_view HoroEventTypeName = "horo::editor::ProjectCreatedEvent";
    ProjectCreationOperationId operationId = 0;
    std::string projectId;
    ProjectCreationRevision revision = 0;
};

/** @brief Lightweight invalidation notification for committed project creation state. */
struct ProjectCreationRevisionChangedEvent {
    static constexpr std::string_view HoroEventTypeName = "horo::editor::ProjectCreationRevisionChangedEvent";
    ProjectCreationRevision revision = 0;
};

/** @brief Creates a project through a staged, cancellable JobSystem operation. */
class ProjectCreationService {
public:
    /**
     * @brief Constructs the service with process-owned execution and notification dependencies.
     * @param jobs Process job system, which must outlive this service and its accepted work.
     * @param dataBus Process data bus, which must outlive this service and its accepted work.
     */
    ProjectCreationService(JobSystem& jobs, EngineDataBus& dataBus);
    ~ProjectCreationService();
    ProjectCreationService(const ProjectCreationService&) = delete;
    ProjectCreationService& operator=(const ProjectCreationService&) = delete;
    ProjectCreationService(ProjectCreationService&&) noexcept;
    ProjectCreationService& operator=(ProjectCreationService&&) noexcept;

    /**
     * @brief Validates and submits a project creation operation without writing from the caller thread.
     * @param request Typed creation values, including the final project root.
     * @return Accepted operation handle or a typed foundation error when validation/submission is rejected.
     * @pre The caller retains the service and injected process services for the operation lifetime.
     */
    [[nodiscard]] Result<ProjectCreationOperationHandle> StartCreate(ProjectCreationRequest request);

    /**
     * @brief Returns the authoritative snapshot for an accepted operation, if retained.
     * @param id Accepted project creation operation identifier.
     * @return The latest immutable snapshot, or empty when the identifier is unknown.
     */
    [[nodiscard]] std::optional<ProjectCreationSnapshot> Query(ProjectCreationOperationId id) const;

    /**
     * @brief Requests cooperative cancellation for a non-terminal operation.
     * @param id Accepted project creation operation identifier.
     * @return Success after the cancellation request was forwarded, or a typed error when unknown.
     */
    [[nodiscard]] Result<void> RequestCancel(ProjectCreationOperationId id);

    /**
     * @brief Dispatches worker-published notifications at the owning main-thread synchronization point.
     * @pre Called by the owner thread of EngineDataBus, never from a worker callback.
     */
    void PumpMainThread();

private:
    std::shared_ptr<ProjectCreationServiceState> state_;
    JobSystem* jobs_ = nullptr;
};

} // namespace Horo::Editor
