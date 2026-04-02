#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/Application.h"
#include "core/EngineConfig.h"

using namespace Monolith;

namespace {
std::string TmpPath(const std::string& name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

void WriteFile(const std::string& path, const std::string& content) {
  std::ofstream f(path);
  f << content;
}
}  // namespace

TEST_CASE("EngineRuntimeConfig loads TOML settings", "[config]") {
  const std::string path = TmpPath("engine_config.toml");
  WriteFile(path,
            R"(
[window]
width = 1600
height = 900
vsync = false

[runtime]
max_ram_mb = 2048
max_cpu_percent = 65
default_scene = "assets/scenes/test.json"

[assets]
directories = ["assets", "mods/base"]
)");

  EngineRuntimeConfig cfg = EngineRuntimeConfig::LoadFromFile(path);
  REQUIRE(cfg.windowWidth == 1600);
  REQUIRE(cfg.windowHeight == 900);
  REQUIRE(cfg.vsync == false);
  REQUIRE(cfg.maxRamMb == 2048);
  REQUIRE(cfg.maxCpuPercent == 65);
  REQUIRE(cfg.defaultScenePath == "assets/scenes/test.json");
  REQUIRE(cfg.assetDirectories.size() == 2);
  REQUIRE(cfg.assetDirectories[0] == "assets");
  REQUIRE(cfg.assetDirectories[1] == "mods/base");
}

TEST_CASE("AppSpec::FromConfig applies engine config", "[config]") {
  const std::string path = TmpPath("engine_config_apply.toml");
  WriteFile(path,
            R"(
[window]
width = 1920
height = 1080
vsync = true

[runtime]
max_ram_mb = 4096
max_cpu_percent = 80
default_scene = "assets/scenes/arena.json"

[assets]
directories = ["assets", "dlc"]
)");

  AppSpec spec = AppSpec::FromConfig(path, "Config Test App");
  REQUIRE(spec.name == "Config Test App");
  REQUIRE(spec.configPath == path);
  REQUIRE(spec.width == 1920);
  REQUIRE(spec.height == 1080);
  REQUIRE(spec.vsync == true);
  REQUIRE(spec.maxRamMb == 4096);
  REQUIRE(spec.maxCpuPercent == 80);
  REQUIRE(spec.defaultScenePath == "assets/scenes/arena.json");
  REQUIRE(spec.assetDirectories.size() == 2);
}

TEST_CASE("EngineRuntimeConfig::LoadOrDefault falls back on missing file", "[config]") {
  EngineRuntimeConfig cfg = EngineRuntimeConfig::LoadOrDefault("/nonexistent/engine.config.toml");
  REQUIRE(cfg.windowWidth == 1280);
  REQUIRE(cfg.windowHeight == 720);
  REQUIRE(cfg.vsync == true);
  REQUIRE(cfg.maxRamMb == 0);
  REQUIRE(cfg.maxCpuPercent == 0);
  REQUIRE(cfg.defaultScenePath == "assets/scenes/dungeon.json");
}
