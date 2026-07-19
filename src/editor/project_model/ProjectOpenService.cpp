#include "Horo/Editor/ProjectOpenService.h"

#include "Horo/Application/ProjectCompatibility.h"
#include "Horo/Application/ProjectMigrationCatalog.h"
#include "editor/EditorServiceErrors.h"
#include "editor/project_model/ProjectMetadata.h"
#include "editor/project_model/RendererAvailability.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

namespace Horo::Editor
{
namespace
{
using namespace Application;

[[nodiscard]] Error OpenError(const ErrorCodeDescriptor &code, std::string message = {})
{
    return MakeError(code, std::move(message));
}

[[nodiscard]] float PhaseProgress(const ProjectOpenPhase phase) noexcept
{
    switch (phase)
    {
    case ProjectOpenPhase::Inspecting: return 0.05F;
    case ProjectOpenPhase::CleaningRecovery: return 0.12F;
    case ProjectOpenPhase::Recovering: return 0.20F;
    case ProjectOpenPhase::ValidatingCompatibility: return 0.30F;
    case ProjectOpenPhase::PlanningMigration: return 0.40F;
    case ProjectOpenPhase::Migrating: return 0.62F;
    case ProjectOpenPhase::UpdatingProjectMetadata: return 0.68F;
    case ProjectOpenPhase::RebuildingDerivedState: return 0.78F;
    case ProjectOpenPhase::RendererPreflight: return 0.88F;
    case ProjectOpenPhase::PreparingWorkspace: return 0.96F;
    case ProjectOpenPhase::ReadyToActivate:
    case ProjectOpenPhase::RequiresRendererRestart:
    case ProjectOpenPhase::Failed:
    case ProjectOpenPhase::Cancelled: return 1.0F;
    }
    return 0.0F;
}

[[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
{
    std::vector<std::byte> result(text.size());
    std::memcpy(result.data(), text.data(), text.size());
    return result;
}

[[nodiscard]] std::string SourceFingerprint(const std::filesystem::path &projectRoot)
{
    const auto path = projectRoot / ".horo/project.json";
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};
    const std::streamsize size = input.tellg();
    if (size <= 0 || size > 64 * 1024)
        return {};
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    input.read(contents.data(), size);
    return input ? contents : std::string{};
}

[[nodiscard]] Result<void> UpdatePatchMarker(DurableFileSystem &files, ProjectMutationCoordinator &mutations,
                                             const std::filesystem::path &projectRoot,
                                             const ReleaseCompatibilityDecision &target,
                                             const ProjectOpenOperationId operation)
{
    auto lease = mutations.TryAcquire(
        {projectRoot, ProjectMutationOwner::Migration, "project-open-" + std::to_string(operation.value)});
    if (lease.HasError())
        return Result<void>::Failure(lease.ErrorValue());
    const auto metadataPath = projectRoot / ".horo/project.json";
    std::ifstream stream(metadataPath, std::ios::binary);
    if (!stream)
        return Result<void>::Failure(OpenError(ProjectOpenErrors::MetadataUpdateFailed));
    nlohmann::json metadata;
    try
    {
        stream >> metadata;
    }
    catch (...)
    {
        return Result<void>::Failure(OpenError(ProjectOpenErrors::MetadataUpdateFailed));
    }
    metadata["horoVersion"] = FormatHoroVersion(target.release.value);
    metadata["persistentContract"] = FormatPersistentContractHash(target.persistentContract);
    metadata.erase("compatibilityProof");
    const std::string serialized = metadata.dump(2) + "\n";
    const auto temporary = projectRoot / ".horo/project.json.open.tmp";
    if (auto written = files.WriteDurable(temporary, Bytes(serialized)); written.HasError())
        return written;
    return files.AtomicReplace(temporary, metadataPath);
}

struct PreparedDerivedState
{
    std::string contributorId;
    std::unique_ptr<IPreparedProjectOpenDerivedState> candidate;
};

struct BackgroundResult
{
    ProjectMetadata metadata;
    std::vector<PreparedDerivedState> derived;
    bool cancellationDeferred{};
};

struct BackgroundCompletion
{
    std::mutex mutex;
    std::optional<BackgroundResult> result;
    std::optional<Error> error;
    std::atomic<ProjectOpenPhase> phase{ProjectOpenPhase::Inspecting};
};

void SetPhase(const std::shared_ptr<BackgroundCompletion> &completion, const ProjectOpenPhase phase) noexcept
{
    completion->phase.store(phase, std::memory_order_release);
}
} // namespace

struct ProjectSessionActivationLease::State
{
    enum class Status : std::uint8_t { Empty, Ready, Reserved, Consumed };
    std::mutex mutex;
    Status status{Status::Empty};
    std::optional<ProjectSessionCandidate> candidate;
    bool shutdown{};
};

struct ProjectOpenService::State
{
    struct Operation
    {
        ProjectOpenProgressSnapshot snapshot;
        ProjectOpenRequest request;
        CancellationSource cancellation;
        std::shared_ptr<BackgroundCompletion> completion;
        std::optional<JobHandle> job;
    };

    JobSystem &jobs;
    DurableFileSystem &files;
    ProjectOpenPreflightService &preflight;
    ProjectMutationCoordinator &mutations;
    ProjectMigrationTransactionService &transactions;
    const RendererAvailabilitySnapshot &rendererAvailability;
    std::vector<IProjectOpenDerivedStateContributor *> derivedContributors;
    std::optional<Operation> operation;
    std::shared_ptr<ProjectSessionActivationLease::State> sessions{
        std::make_shared<ProjectSessionActivationLease::State>()};
    std::uint64_t nextOperation{1};
    std::uint64_t nextSession{1};
    bool shutdown{};
};

ProjectSessionActivationLease::ProjectSessionActivationLease(std::shared_ptr<State> state,
                                                             const ProjectSessionCandidateId id) noexcept
    : state_(std::move(state)), id_(id)
{
}

ProjectSessionActivationLease::ProjectSessionActivationLease(ProjectSessionActivationLease &&other) noexcept
    : state_(std::move(other.state_)), id_(other.id_), committed_(other.committed_)
{
    other.committed_ = true;
}

ProjectSessionActivationLease &ProjectSessionActivationLease::operator=(ProjectSessionActivationLease &&other) noexcept
{
    if (this != &other)
    {
        Release();
        state_ = std::move(other.state_);
        id_ = other.id_;
        committed_ = other.committed_;
        other.committed_ = true;
    }
    return *this;
}

ProjectSessionActivationLease::~ProjectSessionActivationLease()
{
    Release();
}

const ProjectSessionCandidate &ProjectSessionActivationLease::Candidate() const noexcept
{
    assert(state_ && state_->candidate.has_value() && state_->candidate->id == id_);
    return *state_->candidate;
}

Result<void> ProjectSessionActivationLease::Commit()
{
    if (!state_)
        return Result<void>::Failure(OpenError(ProjectOpenErrors::SessionStale));
    std::lock_guard lock(state_->mutex);
    if (state_->status != State::Status::Reserved || !state_->candidate.has_value() ||
        state_->candidate->id != id_)
        return Result<void>::Failure(OpenError(ProjectOpenErrors::SessionStale));
    state_->status = State::Status::Consumed;
    committed_ = true;
    return Result<void>::Success();
}

void ProjectSessionActivationLease::Release() noexcept
{
    if (!state_ || committed_)
        return;
    std::lock_guard lock(state_->mutex);
    if (!state_->shutdown && state_->status == State::Status::Reserved && state_->candidate.has_value() &&
        state_->candidate->id == id_)
        state_->status = State::Status::Ready;
    committed_ = true;
}

/** @copydoc ProjectOpenPreflightService::ProjectOpenPreflightService */
ProjectOpenPreflightService::ProjectOpenPreflightService(ProjectMigrationTransactionService &transactions)
    : transactions_(transactions)
{
    auto catalog = BuildBuiltInProjectMigrationCatalog();
    if (catalog.HasError())
        catalogError_ = catalog.ErrorValue();
    else
    {
        auto registry = ProjectMigrationRegistry::Create(catalog.Value());
        if (registry.HasError())
            catalogError_ = registry.ErrorValue();
        else
            registry_.emplace(std::move(registry).Value());
    }
    auto support = BuildBuiltInProjectMigrationSupportDescriptor();
    if (support.HasError())
        catalogError_ = support.ErrorValue();
    else
        support_.emplace(std::move(support).Value());
}

/** @copydoc ProjectOpenPreflightService::Inspect */
ProjectOpenPreflightSnapshot ProjectOpenPreflightService::Inspect(const std::filesystem::path &projectRoot) const
{
    ProjectOpenPreflightSnapshot snapshot{.compatibility = InspectProjectCompatibility(projectRoot),
                                          .recovery = transactions_.InspectPendingRecovery(projectRoot)};
    if (snapshot.recovery.action != MigrationRecoveryAction::None)
    {
        snapshot.compatibility.status = ProjectCompatibilityStatus::RecoveryRequired;
        snapshot.compatibility.diagnostic = snapshot.recovery.diagnostic;
        return snapshot;
    }
    if ((snapshot.compatibility.status == ProjectCompatibilityStatus::MigrationPathMissing ||
         snapshot.compatibility.status == ProjectCompatibilityStatus::AutomaticMigrationRequired) &&
        snapshot.compatibility.metadata.has_value() && snapshot.compatibility.sourceBaseline.has_value())
    {
        if (!registry_.has_value() || !support_.has_value())
        {
            snapshot.compatibility.status = ProjectCompatibilityStatus::MigrationPathMissing;
            snapshot.compatibility.diagnostic = catalogError_;
            return snapshot;
        }
        auto plan = registry_->Plan(*snapshot.compatibility.sourceBaseline,
                                    snapshot.compatibility.metadata->persistentContract, *support_);
        if (plan.HasValue())
        {
            snapshot.compatibility.status = ProjectCompatibilityStatus::AutomaticMigrationRequired;
            snapshot.migrationPlan.emplace(std::move(plan).Value());
        }
        else
        {
            snapshot.compatibility.status =
                plan.ErrorValue().code.Value() == "project.migration.provider_missing"
                    ? ProjectCompatibilityStatus::RequiredProviderUnavailable
                    : ProjectCompatibilityStatus::MigrationPathMissing;
            snapshot.compatibility.diagnostic = plan.ErrorValue();
        }
    }
    return snapshot;
}

/** @copydoc ProjectOpenService::ProjectOpenService */
ProjectOpenService::ProjectOpenService(JobSystem &jobs, DurableFileSystem &files, ProjectOpenPreflightService &preflight,
                                       ProjectMutationCoordinator &mutations,
                                       ProjectMigrationTransactionService &transactions,
                                       const RendererAvailabilitySnapshot &rendererAvailability,
                                       const std::span<IProjectOpenDerivedStateContributor *const> contributors)
{
    state_ = std::make_unique<State>(State{jobs, files, preflight, mutations, transactions, rendererAvailability,
                                           {contributors.begin(), contributors.end()}});
}

ProjectOpenService::~ProjectOpenService()
{
    Shutdown();
}

/** @copydoc ProjectOpenService::Start */
Result<ProjectOpenOperationHandle> ProjectOpenService::Start(ProjectOpenRequest request)
{
    if (state_->shutdown ||
        (state_->operation.has_value() && state_->operation->snapshot.outcome == ProjectOpenOutcome::Running))
        return Result<ProjectOpenOperationHandle>::Failure(OpenError(ProjectOpenErrors::Busy));
    {
        std::lock_guard lock(state_->sessions->mutex);
        if (state_->sessions->status == ProjectSessionActivationLease::State::Status::Ready ||
            state_->sessions->status == ProjectSessionActivationLease::State::Status::Reserved)
            return Result<ProjectOpenOperationHandle>::Failure(OpenError(ProjectOpenErrors::Busy));
        state_->sessions->candidate.reset();
        state_->sessions->status = ProjectSessionActivationLease::State::Status::Empty;
    }

    const ProjectOpenOperationId id{state_->nextOperation++};
    ProjectOpenProgressSnapshot snapshot{.operationId = id,
                                         .phase = ProjectOpenPhase::Inspecting,
                                         .outcome = ProjectOpenOutcome::Running,
                                         .progress = PhaseProgress(ProjectOpenPhase::Inspecting),
                                         .projectRoot = request.projectRoot,
                                         .projectName = request.expectedProjectName};
    auto completion = std::make_shared<BackgroundCompletion>();
    State::Operation operation{std::move(snapshot), std::move(request), {}, completion};

    const auto requestCopy = operation.request;
    const CancellationToken cancellation = operation.cancellation.Token();
    auto submitted = state_->jobs.SubmitResult(
        JobDescriptor{.parentCancellation = cancellation},
        [state = state_.get(), completion, requestCopy, id](const CancellationToken &jobCancellation) -> Result<void> {
            const auto fail = [&](Error error) -> Result<void> {
                std::lock_guard lock(completion->mutex);
                completion->error = error;
                return Result<void>::Failure(std::move(error));
            };
            SetPhase(completion, ProjectOpenPhase::CleaningRecovery);
            // Cleanup is non-authoritative; a failure remains a warning and must not block open.
            static_cast<void>(state->transactions.CleanupCommittedMigrations(requestCopy.projectRoot));
            if (state->transactions.InspectPendingRecovery(requestCopy.projectRoot).action !=
                MigrationRecoveryAction::None)
            {
                SetPhase(completion, ProjectOpenPhase::Recovering);
                if (auto recovered = state->transactions.Recover(requestCopy.projectRoot, jobCancellation);
                    recovered.HasError())
                    return fail(recovered.ErrorValue());
            }
            if (jobCancellation.IsCancellationRequested())
                return fail(OpenError(ProjectOpenErrors::Cancelled));

            SetPhase(completion, ProjectOpenPhase::ValidatingCompatibility);
            ProjectOpenPreflightSnapshot preflight = state->preflight.Inspect(requestCopy.projectRoot);
            ProjectCompatibilitySnapshot compatibility = preflight.compatibility;
            std::optional<ProjectMigrationPlan> plan;
            bool cancellationDeferred{};
            if (compatibility.status == ProjectCompatibilityStatus::CompatibleReleaseLine &&
                compatibility.markerUpdateRequired)
            {
                SetPhase(completion, ProjectOpenPhase::UpdatingProjectMetadata);
                const auto *target = BuiltInReleaseCompatibilityRegistry().Find(CurrentEngineReleaseVersion());
                if (target == nullptr)
                    return fail(OpenError(ProjectOpenErrors::MigrationPlanMissing));
                if (auto updated = UpdatePatchMarker(state->files, state->mutations, requestCopy.projectRoot,
                                                     *target, id); updated.HasError())
                    return fail(updated.ErrorValue());
                compatibility = state->preflight.Inspect(requestCopy.projectRoot).compatibility;
            }
            else if (compatibility.status == ProjectCompatibilityStatus::AutomaticMigrationRequired)
            {
                SetPhase(completion, ProjectOpenPhase::PlanningMigration);
                if (!preflight.migrationPlan.has_value() || !compatibility.sourceBaseline.has_value() ||
                    !compatibility.metadata.has_value())
                    return fail(compatibility.diagnostic.value_or(OpenError(ProjectOpenErrors::MigrationPlanMissing)));
                const auto *target = BuiltInReleaseCompatibilityRegistry().Find(CurrentEngineReleaseVersion());
                if (target == nullptr)
                    return fail(OpenError(ProjectOpenErrors::MigrationPlanMissing));
                plan.emplace(std::move(*preflight.migrationPlan));
                SetPhase(completion, ProjectOpenPhase::Migrating);
                if (state->jobs.WorkerCount() < 2)
                    return fail(OpenError(ProjectOpenErrors::WorkerCapacityInsufficient));
                ProjectMigrationTransactionRequest transaction{
                    .projectRoot = requestCopy.projectRoot,
                    .sourceMetadata = *compatibility.metadata,
                    .sourceBaseline = *compatibility.sourceBaseline,
                    .targetDecision = *target,
                    .plan = *plan,
                    .engineBuildIdentity = requestCopy.engineBuildIdentity,
                    .limits = requestCopy.migrationLimits,
                    .cancellation = jobCancellation};
                auto migrated = state->transactions.Execute(transaction);
                if (migrated.HasError())
                    return fail(migrated.ErrorValue());
                cancellationDeferred = migrated.Value().cancellationDeferred;
                compatibility = state->preflight.Inspect(requestCopy.projectRoot).compatibility;
            }
            else if (compatibility.status != ProjectCompatibilityStatus::Current &&
                     compatibility.status != ProjectCompatibilityStatus::CompatibleReleaseLine)
                return fail(compatibility.diagnostic.value_or(OpenError(ProjectOpenErrors::CompatibilityBlocked)));

            if (!compatibility.metadata.has_value())
                return fail(OpenError(ProjectOpenErrors::CompatibilityBlocked));
            if (jobCancellation.IsCancellationRequested() && !cancellationDeferred)
                return fail(OpenError(ProjectOpenErrors::Cancelled));

            SetPhase(completion, ProjectOpenPhase::RebuildingDerivedState);
            BackgroundResult result{.metadata = *compatibility.metadata,
                                    .cancellationDeferred = cancellationDeferred};
            result.derived.reserve(state->derivedContributors.size());
            for (IProjectOpenDerivedStateContributor *contributor : state->derivedContributors)
            {
                auto prepared = contributor->Prepare(requestCopy.projectRoot, jobCancellation);
                if (prepared.HasError())
                    return fail(OpenError(ProjectOpenErrors::DerivedStateFailed,
                                          std::string(contributor->Id()) + ": " + prepared.ErrorValue().message));
                result.derived.push_back({std::string(contributor->Id()), std::move(prepared).Value()});
            }
            std::lock_guard lock(completion->mutex);
            completion->result.emplace(std::move(result));
            return Result<void>::Success();
        });
    if (submitted.HasError())
        return Result<ProjectOpenOperationHandle>::Failure(submitted.ErrorValue());
    operation.job.emplace(std::move(submitted).Value());
    state_->operation.emplace(std::move(operation));
    return Result<ProjectOpenOperationHandle>::Success(ProjectOpenOperationHandle{id});
}

/** @copydoc ProjectOpenService::Query */
std::optional<ProjectOpenProgressSnapshot> ProjectOpenService::Query(const ProjectOpenOperationId operation) const
{
    if (!state_->operation.has_value() || state_->operation->snapshot.operationId != operation)
        return std::nullopt;
    return state_->operation->snapshot;
}

/** @copydoc ProjectOpenService::RequestCancel */
Result<void> ProjectOpenService::RequestCancel(const ProjectOpenOperationId operation)
{
    if (!state_->operation.has_value() || state_->operation->snapshot.operationId != operation)
        return Result<void>::Failure(OpenError(ProjectOpenErrors::NotFound));
    state_->operation->cancellation.RequestCancellation();
    if (state_->operation->job.has_value())
        static_cast<void>(state_->jobs.RequestCancel(state_->operation->job->Id()));
    return Result<void>::Success();
}

Result<ProjectSessionActivationLease> ProjectOpenService::ReserveSession(const ProjectSessionCandidateId session)
{
    std::lock_guard lock(state_->sessions->mutex);
    if (state_->sessions->shutdown || state_->sessions->status != ProjectSessionActivationLease::State::Status::Ready ||
        !state_->sessions->candidate.has_value() || state_->sessions->candidate->id != session ||
        SourceFingerprint(state_->sessions->candidate->projectRoot) != state_->sessions->candidate->sourceFingerprint)
        return Result<ProjectSessionActivationLease>::Failure(OpenError(ProjectOpenErrors::SessionStale));
    state_->sessions->status = ProjectSessionActivationLease::State::Status::Reserved;
    return Result<ProjectSessionActivationLease>::Success(ProjectSessionActivationLease{state_->sessions, session});
}

Result<void> ProjectOpenService::DiscardSession(const ProjectSessionCandidateId session)
{
    std::lock_guard lock(state_->sessions->mutex);
    if (!state_->sessions->candidate.has_value() || state_->sessions->candidate->id != session ||
        state_->sessions->status == ProjectSessionActivationLease::State::Status::Reserved)
        return Result<void>::Failure(OpenError(ProjectOpenErrors::SessionStale));
    state_->sessions->candidate.reset();
    state_->sessions->status = ProjectSessionActivationLease::State::Status::Empty;
    return Result<void>::Success();
}

void ProjectOpenService::PumpOwnerThread()
{
    if (!state_->operation.has_value() || state_->operation->snapshot.outcome != ProjectOpenOutcome::Running)
        return;
    auto &operation = *state_->operation;
    auto &snapshot = operation.snapshot;
    snapshot.phase = operation.completion->phase.load(std::memory_order_acquire);
    snapshot.progress = std::max(snapshot.progress, PhaseProgress(snapshot.phase));
    if (!operation.job.has_value())
        return;
    const JobSnapshot job = state_->jobs.Query(operation.job->Id());
    if (job.state == JobState::Queued || job.state == JobState::Running)
        return;

    std::optional<BackgroundResult> result;
    std::optional<Error> error;
    {
        std::lock_guard lock(operation.completion->mutex);
        result = std::move(operation.completion->result);
        error = operation.completion->error;
    }
    if (!result.has_value())
    {
        snapshot.phase = operation.cancellation.Token().IsCancellationRequested() ? ProjectOpenPhase::Cancelled
                                                                                   : ProjectOpenPhase::Failed;
        snapshot.outcome = operation.cancellation.Token().IsCancellationRequested() ? ProjectOpenOutcome::Cancelled
                                                                                     : ProjectOpenOutcome::Failed;
        snapshot.progress = 1.0F;
        snapshot.diagnostic = error.has_value() ? std::move(error) : job.error;
        return;
    }
    snapshot.projectName = result->metadata.name;
    snapshot.cancellationDeferred = result->cancellationDeferred;
    if (result->cancellationDeferred || operation.cancellation.Token().IsCancellationRequested())
    {
        snapshot.phase = ProjectOpenPhase::Cancelled;
        snapshot.outcome = ProjectOpenOutcome::Cancelled;
        snapshot.progress = 1.0F;
        return;
    }

    snapshot.phase = ProjectOpenPhase::RebuildingDerivedState;
    std::vector<std::string> revisions;
    revisions.reserve(result->derived.size());
    for (auto &prepared : result->derived)
    {
        auto installed = prepared.candidate->Install();
        if (installed.HasError())
        {
            snapshot.phase = ProjectOpenPhase::Failed;
            snapshot.outcome = ProjectOpenOutcome::Failed;
            snapshot.progress = 1.0F;
            snapshot.diagnostic = OpenError(ProjectOpenErrors::DerivedStateFailed,
                                            prepared.contributorId + ": " + installed.ErrorValue().message);
            return;
        }
        revisions.push_back(prepared.contributorId + "@" + installed.Value());
    }

    snapshot.phase = ProjectOpenPhase::RendererPreflight;
    snapshot.progress = std::max(snapshot.progress, PhaseProgress(snapshot.phase));
    const ProjectOpenPreflight preflight = PreflightProjectOpen(operation.request.projectRoot,
                                                                state_->rendererAvailability);
    snapshot.requiredRendererBackend = preflight.requiredBackendId;
    if (preflight.status == ProjectOpenPreflightStatus::RequiresRendererRestart)
    {
        snapshot.phase = ProjectOpenPhase::RequiresRendererRestart;
        snapshot.outcome = ProjectOpenOutcome::RequiresRendererRestart;
        snapshot.progress = 1.0F;
        return;
    }
    if (preflight.status != ProjectOpenPreflightStatus::Ready)
    {
        snapshot.phase = ProjectOpenPhase::Failed;
        snapshot.outcome = ProjectOpenOutcome::Failed;
        snapshot.progress = 1.0F;
        snapshot.diagnostic = OpenError(ProjectOpenErrors::CompatibilityBlocked, preflight.diagnostic);
        return;
    }

    snapshot.phase = ProjectOpenPhase::PreparingWorkspace;
    const std::string fingerprint = SourceFingerprint(operation.request.projectRoot);
    if (fingerprint.empty())
    {
        snapshot.phase = ProjectOpenPhase::Failed;
        snapshot.outcome = ProjectOpenOutcome::Failed;
        snapshot.progress = 1.0F;
        snapshot.diagnostic = OpenError(ProjectOpenErrors::SessionStale);
        return;
    }
    const ProjectSessionCandidateId sessionId{state_->nextSession++};
    {
        std::lock_guard lock(state_->sessions->mutex);
        state_->sessions->candidate = ProjectSessionCandidate{sessionId, snapshot.operationId,
                                                               operation.request.projectRoot, snapshot.projectName,
                                                               snapshot.requiredRendererBackend, fingerprint,
                                                               std::move(revisions)};
        state_->sessions->status = ProjectSessionActivationLease::State::Status::Ready;
    }
    snapshot.readySession = sessionId;
    snapshot.phase = ProjectOpenPhase::ReadyToActivate;
    snapshot.outcome = ProjectOpenOutcome::ReadyToActivate;
    snapshot.progress = 1.0F;
}

void ProjectOpenService::Shutdown() noexcept
{
    if (!state_ || state_->shutdown)
        return;
    state_->shutdown = true;
    if (state_->operation.has_value() && state_->operation->snapshot.outcome == ProjectOpenOutcome::Running)
    {
        state_->operation->cancellation.RequestCancellation();
        if (state_->operation->job.has_value())
        {
            static_cast<void>(state_->jobs.RequestCancel(state_->operation->job->Id()));
            static_cast<void>(state_->operation->job->Wait());
        }
        state_->operation->snapshot.phase = ProjectOpenPhase::Cancelled;
        state_->operation->snapshot.outcome = ProjectOpenOutcome::Cancelled;
        state_->operation->snapshot.progress = 1.0F;
    }
    std::lock_guard lock(state_->sessions->mutex);
    state_->sessions->shutdown = true;
    state_->sessions->candidate.reset();
    state_->sessions->status = ProjectSessionActivationLease::State::Status::Empty;
}
} // namespace Horo::Editor
