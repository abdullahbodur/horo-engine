#include "Horo/Editor/ProjectIntegrityValidatorService.h"
#include "Horo/Foundation/Platform.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
    using namespace Horo;

    struct TemporaryProject {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     ("horo-integrity-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        TemporaryProject() {
            std::filesystem::create_directories(root);
        }

        ~TemporaryProject() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    };

    void WriteFile(const std::filesystem::path &path, const std::string &content) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        REQUIRE(stream.good());
        stream << content;
    }
}  // namespace

TEST_CASE("ProjectIntegrityValidatorService detects missing CMakeLists.txt and repairs it", "[unit][editor][integrity]") {
    TemporaryProject project;
    WriteFile(project.root / "source" / "gameplay" / "MyPlayer.cpp", "// C++ behavior\n");

    NativeDurableFileSystem files;
    Editor::ProjectIntegrityValidatorService validator{files};

    Editor::ProjectIntegrityReport report = validator.Inspect(project.root);
    REQUIRE(report.HasIssues());
    REQUIRE(report.HasAutoFixableIssues());

    const auto it = std::ranges::find_if(report.issues, [](const Editor::ProjectIntegrityIssue &issue) {
        return issue.kind == Editor::ProjectIntegrityIssueKind::MissingCMakeLists;
    });
    REQUIRE(it != report.issues.end());

    const Result<void> repaired = validator.Repair(project.root);
    REQUIRE(repaired.HasValue());

    REQUIRE(std::filesystem::is_regular_file(project.root / "CMakeLists.txt"));
    REQUIRE(std::filesystem::is_regular_file(project.root / "source" / "gameplay" / "GameModule.cpp"));

    Editor::ProjectIntegrityReport cleanReport = validator.Inspect(project.root);
    const auto missingCMake = std::ranges::find_if(cleanReport.issues, [](const Editor::ProjectIntegrityIssue &issue) {
        return issue.kind == Editor::ProjectIntegrityIssueKind::MissingCMakeLists;
    });
    REQUIRE(missingCMake == cleanReport.issues.end());
}
