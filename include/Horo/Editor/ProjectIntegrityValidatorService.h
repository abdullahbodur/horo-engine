#pragma once

#include "Horo/Foundation/Platform.h"
#include "Horo/Foundation/Result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Horo::Editor {

    enum class ProjectIntegrityIssueKind {
        MissingCMakeLists,
        MissingGameModuleBootstrap,
        MissingScriptsDirectory,
        MissingScenesDirectory,
    };

    struct ProjectIntegrityIssue {
        ProjectIntegrityIssueKind kind;
        std::filesystem::path targetPath;
        std::string description;
        bool isAutoFixable{true};
    };

    struct ProjectIntegrityReport {
        std::filesystem::path projectRoot;
        std::vector<ProjectIntegrityIssue> issues;

        [[nodiscard]] bool HasIssues() const noexcept {
            return !issues.empty();
        }

        [[nodiscard]] bool HasAutoFixableIssues() const noexcept;
    };

    class ProjectIntegrityValidatorService {
    public:
        explicit ProjectIntegrityValidatorService(DurableFileSystem &durableFiles);

        /**
         * @brief Inspects the project structure and returns an integrity report.
         * @param projectRoot Absolute path to the project root directory.
         */
        [[nodiscard]] ProjectIntegrityReport Inspect(const std::filesystem::path &projectRoot) const;

        /**
         * @brief Automatically repairs fixable integrity issues in the project.
         * @param projectRoot Absolute path to the project root directory.
         * @return Success if all fixable issues were repaired, or typed Error.
         */
        Result<void> Repair(const std::filesystem::path &projectRoot) const;

    private:
        DurableFileSystem &durableFiles_;
    };

}  // namespace Horo::Editor
