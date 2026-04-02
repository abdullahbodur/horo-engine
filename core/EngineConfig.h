#pragma once

#include <string>
#include <vector>

namespace Monolith {

struct AppSpec;

struct EngineRuntimeConfig {
  int windowWidth = 1280;
  int windowHeight = 720;
  bool vsync = true;

  // Advisory runtime budgets for the consumer project.
  // The engine parses and exposes them; hard enforcement is left to the host app.
  int maxRamMb = 0;
  int maxCpuPercent = 0;

  std::string defaultScenePath = "assets/scenes/dungeon.json";
  std::vector<std::string> assetDirectories;

  static EngineRuntimeConfig LoadFromFile(const std::string& path);
  static EngineRuntimeConfig LoadOrDefault(const std::string& path);

  void ApplyTo(AppSpec& spec) const;
};

}  // namespace Monolith
