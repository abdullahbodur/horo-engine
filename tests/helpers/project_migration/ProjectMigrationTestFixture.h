#pragma once

#include "Horo/Editor/ProjectOpenService.h"

#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace Horo::Tests
{
/** @brief Owns an isolated copy of the frozen 0.0.1 migration fixture. */
class ProjectMigrationTestFixture final
{
  public:
    ProjectMigrationTestFixture();
    ~ProjectMigrationTestFixture();

    ProjectMigrationTestFixture(const ProjectMigrationTestFixture&) = delete;
    ProjectMigrationTestFixture& operator=(const ProjectMigrationTestFixture&) = delete;

    [[nodiscard]] const std::filesystem::path& Root() const noexcept;
    [[nodiscard]] const std::filesystem::path& LogRoot() const noexcept;
    [[nodiscard]] std::string ReadProjectBytes() const;
    [[nodiscard]] std::string ReadHistoryBytes() const;
    [[nodiscard]] nlohmann::json ReadProjectJson() const;
    [[nodiscard]] nlohmann::json ReadHistoryJson() const;

  private:
    std::filesystem::path root_;
    std::filesystem::path logRoot_;
};

/** @brief Pumps one project-open operation to a bounded terminal snapshot. */
[[nodiscard]] Editor::ProjectOpenProgressSnapshot PumpProjectOpenToTerminal(
    Editor::ProjectOpenService& service, Editor::ProjectOpenOperationId operation,
    std::chrono::milliseconds timeout = std::chrono::seconds{15});

/** @brief Computes canonical SHA-256 text for an independent history-binding assertion. */
[[nodiscard]] std::string ComputeTestSha256(std::string_view text);
} // namespace Horo::Tests
