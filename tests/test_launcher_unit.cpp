// Launcher-focused unit tests (paths, manifest helpers). End-to-end ImGui UI
// scenarios run only in HoroEditorUiTest + UiAutomationRunner.cpp.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "studio/LauncherProject.h"

using namespace Monolith::Launcher;

namespace {

namespace fs = std::filesystem;

}  // namespace

TEST_CASE("SanitizeProjectId normalizes names for manifest ids", "[launcher][project]") {
  REQUIRE(SanitizeProjectId("My Game") == "my_game");
  REQUIRE(SanitizeProjectId("Foo-Bar_Baz") == "foo_bar_baz");
  REQUIRE(SanitizeProjectId("!!!") == "project");
  REQUIRE(SanitizeProjectId("A  B") == "a_b");
}

TEST_CASE("ResolveProjectManifestPath points at .horo/project.json", "[launcher][project]") {
  const fs::path root = fs::path("workspace") / "MyProject";
  REQUIRE(ResolveProjectManifestPath(root) == root / ".horo" / "project.json");
}

TEST_CASE("IsLauncherProjectRoot is true only when manifest exists", "[launcher][project]") {
  const fs::path root = fs::temp_directory_path() / "horo_launcher_unit_manifest";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".horo", ec);
  REQUIRE_FALSE(IsLauncherProjectRoot(root));

  {
    std::ofstream out(root / ".horo" / "project.json");
    REQUIRE(out.is_open());
    out << R"({"schemaVersion":1,"projectName":"T"})";
  }
  REQUIRE(IsLauncherProjectRoot(root));

  fs::remove_all(root, ec);
}
