#pragma once

/** @file ProjectMutation.h @brief Shared project mutation serialization contract. */

#include "Horo/Foundation/Platform.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Horo::Editor
{
/** @brief Names the subsystem requesting exclusive authored-project mutation authority. */
enum class ProjectMutationOwner : std::uint8_t
{
    Migration,
    Save,
    Autosave,
    Package,
    Cli,
    Mcp
};

/** @brief Identifies one immediate project mutation admission request. */
struct ProjectMutationRequest
{
    std::filesystem::path projectRoot;
    ProjectMutationOwner owner{ProjectMutationOwner::Migration};
    std::string operationId;
};

/** @brief Move-only proof that one process and host operation owns project mutation authority. */
class ProjectMutationLease
{
  public:
    struct State;
    ~ProjectMutationLease();
    ProjectMutationLease(ProjectMutationLease &&) noexcept;
    ProjectMutationLease &operator=(ProjectMutationLease &&) noexcept;
    ProjectMutationLease(const ProjectMutationLease &) = delete;
    ProjectMutationLease &operator=(const ProjectMutationLease &) = delete;

  private:
    friend class ProjectMutationCoordinator;
    explicit ProjectMutationLease(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/** @brief Serializes in-process and cross-process writers through one project-local lock. */
class ProjectMutationCoordinator
{
  public:
    struct State;
    /** @brief Constructs a host-owned coordinator. @param files Durable native filesystem service. */
    explicit ProjectMutationCoordinator(DurableFileSystem &files);
    ~ProjectMutationCoordinator();
    ProjectMutationCoordinator(const ProjectMutationCoordinator &) = delete;
    ProjectMutationCoordinator &operator=(const ProjectMutationCoordinator &) = delete;

    /**
     * @brief Immediately attempts to acquire exclusive mutation authority.
     * @param request Project root and diagnostic operation identity.
     * @return Move-only lease, or a typed busy/filesystem failure without waiting.
     */
    [[nodiscard]] Result<ProjectMutationLease> TryAcquire(const ProjectMutationRequest &request);

  private:
    std::shared_ptr<State> state_;
};
} // namespace Horo::Editor
