#pragma once

#include <string>

#include "editor/SceneDocument.h"
#include "math/Vec3.h"

namespace Monolith {
namespace Editor {

struct BlueprintSnapshotOptions {
  // Output path prefix without extension.
  // .ppm and .svg files are written as "<outputBasePath>.ppm" / ".svg".
  std::string outputBasePath = "blueprint";
  int imageSize = 1400;  // pixels (square)
  int padding = 48;      // pixels of whitespace border
};

struct BlueprintSnapshotAnalysis {
  Vec3 min = Vec3::Zero();
  Vec3 max = Vec3::Zero();
  int panelCount = 0;
  int propCount = 0;
  int lightCount = 0;
};

struct BlueprintSnapshotResult {
  BlueprintSnapshotAnalysis analysis;
  std::string sourceDescription;
  std::string ppmPath;
  std::string svgPath;
  int width = 0;
  int height = 0;
};

// Headless, CPU-only renderer that reads a SceneDocument and writes a
// top-down 2D map as a PPM raster image and an SVG vector graphic.
// No GPU or window context required — safe to call from CI pipelines.
//
// Usage (game project CI):
//   auto doc = SceneSerializer::LoadFromFile("assets/scenes/dungeon.json");
//   auto result = BlueprintSnapshot::Generate(doc, opts);
class BlueprintSnapshot {
 public:
  static BlueprintSnapshotAnalysis Analyze(const SceneDocument& doc);
  static BlueprintSnapshotResult Generate(const SceneDocument& doc,
                                          const BlueprintSnapshotOptions& opts = {});
};

}  // namespace Editor
}  // namespace Monolith
