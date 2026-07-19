#pragma once

/** @file RecentProjectInspectionService.h @brief Bounded background compatibility projection for recent projects. */

#include "Horo/Editor/ProjectOpenService.h"
#include "Horo/Editor/RecentProject.h"

#include <memory>
#include <span>
#include <vector>

namespace Horo::Editor
{
/** @brief Generation-safe result for one inspected recent-project root. */
struct RecentProjectInspectionUpdate
{
    std::uint64_t generation{};
    std::string rootPath;
    RecentProjectCompatibilityProjection projection;
};

/** @brief Owns bounded non-authoritative compatibility inspection work. */
class RecentProjectInspectionService
{
  public:
    /**
     * @brief Creates the host-owned recent-project inspection service.
     * @param jobs Scheduler used for bounded background inspection.
     * @param preflight Shared read-only project-open classification authority.
     */
    RecentProjectInspectionService(JobSystem &jobs, const ProjectOpenPreflightService &preflight);
    /** @brief Cancels and joins any accepted inspection work. */
    ~RecentProjectInspectionService();
    RecentProjectInspectionService(const RecentProjectInspectionService &) = delete;
    RecentProjectInspectionService &operator=(const RecentProjectInspectionService &) = delete;

    /**
     * @brief Starts a new bounded refresh and invalidates results from older generations.
     * @param projects Displayable recent-project entries; at most 128 are admitted.
     * @return New generation identity or a typed scheduling/lifecycle failure.
     */
    [[nodiscard]] Result<std::uint64_t> Refresh(std::span<const RecentProjectEntry> projects);
    /**
     * @brief Drains completed updates for the current generation on the owner thread.
     * @return Fresh display projections; stale generation results are omitted.
     */
    [[nodiscard]] std::vector<RecentProjectInspectionUpdate> DrainUpdates();
    /** @brief Cancels and joins accepted inspection work; idempotent. */
    void Shutdown() noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace Horo::Editor
