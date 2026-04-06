#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "mcp/McpController.h"

using namespace Monolith;
using namespace Monolith::Mcp;

namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  REQUIRE(in.is_open());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("Architecture docs exist and are discoverable from README", "[architecture][docs]") {
  const std::filesystem::path root =
      std::filesystem::path(__FILE__).parent_path().parent_path();
  const std::filesystem::path docsRoot = root / "docs" / "architecture";

  REQUIRE(std::filesystem::is_directory(docsRoot));
  REQUIRE(std::filesystem::is_regular_file(docsRoot / "README.md"));
  REQUIRE(std::filesystem::is_regular_file(docsRoot / "module-boundaries.md"));
  REQUIRE(std::filesystem::is_regular_file(docsRoot / "ownership-lifecycle.md"));
  REQUIRE(std::filesystem::is_regular_file(docsRoot / "error-result-model.md"));
  REQUIRE(std::filesystem::is_regular_file(docsRoot / "threading-and-mutation.md"));

  const std::string readme = ReadTextFile(root / "README.md");
  REQUIRE(readme.find("docs/architecture") != std::string::npos);
  REQUIRE(readme.find("new headers are internal by default") != std::string::npos);

  const std::string moduleBoundaries = ReadTextFile(docsRoot / "module-boundaries.md");
  REQUIRE(moduleBoundaries.find("core") != std::string::npos);
  REQUIRE(moduleBoundaries.find("math") != std::string::npos);
  REQUIRE(moduleBoundaries.find("scene") != std::string::npos);
  REQUIRE(moduleBoundaries.find("renderer") != std::string::npos);
  REQUIRE(moduleBoundaries.find("physics") != std::string::npos);
  REQUIRE(moduleBoundaries.find("input") != std::string::npos);
  REQUIRE(moduleBoundaries.find("editor") != std::string::npos);
  REQUIRE(moduleBoundaries.find("mcp") != std::string::npos);
}

TEST_CASE("McpController lifecycle calls are safe to repeat", "[architecture][lifecycle][mcp]") {
  McpController controller;

  REQUIRE_NOTHROW(controller.Shutdown());
  REQUIRE_NOTHROW(controller.Initialize());
  REQUIRE_NOTHROW(controller.Initialize());

  const McpStatusSnapshot afterInit = controller.GetStatusSnapshot();
  REQUIRE(afterInit.toolCount > 0);
  REQUIRE(afterInit.resourceCount > 0);

  REQUIRE_NOTHROW(controller.Shutdown());
  REQUIRE_NOTHROW(controller.Shutdown());

  const McpStatusSnapshot afterShutdown = controller.GetStatusSnapshot();
  REQUIRE_FALSE(afterShutdown.running);
  REQUIRE(afterShutdown.activeConnections == 0);
  REQUIRE(afterShutdown.activeRequests == 0);
}
