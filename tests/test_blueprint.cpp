#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "editor/BlueprintSnapshot.h"
#include "editor/SceneDocument.h"

using namespace Monolith;
using namespace Monolith::Editor;

static SceneDocument MakeTestScene()
{
    SceneDocument doc;
    doc.sceneId = "test_scene";

    // Floor panel (low Y half-extent)
    SceneObject floor;
    floor.id = "floor";
    floor.type = SceneObjectType::Panel;
    floor.position = {4.0f, 0.0f, 4.0f};
    floor.scale = {4.0f, 0.1f, 4.0f};
    doc.objects.push_back(floor);

    // Wall panel
    SceneObject wall;
    wall.id = "wall_south";
    wall.type = SceneObjectType::Panel;
    wall.position = {4.0f, 1.5f, 0.0f};
    wall.scale = {4.0f, 1.5f, 0.2f};
    doc.objects.push_back(wall);

    // Prop
    SceneObject prop;
    prop.id = "crate";
    prop.type = SceneObjectType::Prop;
    prop.position = {3.0f, 0.0f, 3.0f};
    prop.scale = {0.5f, 0.5f, 0.5f};
    doc.objects.push_back(prop);

    // Light
    SceneObject light;
    light.id = "light_0";
    light.type = SceneObjectType::Light;
    light.position = {4.0f, 3.0f, 4.0f};
    doc.objects.push_back(light);

    return doc;
}

TEST_CASE("BlueprintSnapshot::Analyze counts objects and computes bounds", "[blueprint]")
{
    auto doc = MakeTestScene();
    auto a = BlueprintSnapshot::Analyze(doc);

    REQUIRE(a.panelCount == 2);
    REQUIRE(a.propCount == 1);
    REQUIRE(a.lightCount == 1);

    // floor panel: center=(4,0,4) scale=(4,0.1,4) → x:[0,8] z:[0,8]
    REQUIRE(a.min.x <= 0.0f);
    REQUIRE(a.max.x >= 8.0f);
    REQUIRE(a.min.z <= 0.0f);
    REQUIRE(a.max.z >= 8.0f);
}

TEST_CASE("BlueprintSnapshot::Generate writes PPM and SVG files", "[blueprint]")
{
    namespace fs = std::filesystem;

    auto doc = MakeTestScene();

    BlueprintSnapshotOptions opts;
    opts.outputBasePath = "test_out/blueprint_test";
    opts.imageSize = 64;
    opts.padding = 4;

    auto result = BlueprintSnapshot::Generate(doc, opts);

    REQUIRE(result.width == 64);
    REQUIRE(result.height == 64);
    REQUIRE(!result.ppmPath.empty());
    REQUIRE(!result.svgPath.empty());

    REQUIRE(fs::exists(result.ppmPath));
    REQUIRE(fs::exists(result.svgPath));

    // PPM: at minimum header + pixel data
    REQUIRE(fs::file_size(result.ppmPath) > 10);
    // SVG: at minimum an opening <svg tag
    REQUIRE(fs::file_size(result.svgPath) > 10);

    // SVG content should reference the sceneId
    std::ifstream svg(result.svgPath);
    std::string content((std::istreambuf_iterator<char>(svg)), std::istreambuf_iterator<char>());
    REQUIRE(content.find("<svg") != std::string::npos);
    REQUIRE(content.find("test_scene") != std::string::npos);

    // Cleanup (ignore errors — Windows may briefly lock the directory)
    std::error_code ec;
    fs::remove_all("test_out", ec);
}

TEST_CASE("BlueprintSnapshot::Generate handles empty scene", "[blueprint]")
{
    namespace fs = std::filesystem;

    SceneDocument empty;
    empty.sceneId = "empty";

    BlueprintSnapshotOptions opts;
    opts.outputBasePath = "test_out/empty_blueprint";
    opts.imageSize = 32;
    opts.padding = 2;

    auto result = BlueprintSnapshot::Generate(empty, opts);

    REQUIRE(result.analysis.panelCount == 0);
    REQUIRE(result.analysis.propCount == 0);
    REQUIRE(result.analysis.lightCount == 0);
    REQUIRE(fs::exists(result.ppmPath));
    REQUIRE(fs::exists(result.svgPath));

    fs::remove_all("test_out");
}
