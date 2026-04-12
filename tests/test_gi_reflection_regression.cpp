#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "renderer/SceneTextureResources.h"

using json = nlohmann::json;
using namespace Monolith;

namespace {

constexpr float kFloatEpsilon = 1e-5f;

std::filesystem::path RepoRoot() {
  return std::filesystem::path(__FILE__).parent_path().parent_path();
}

json ReadJsonFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  REQUIRE(in.is_open());
  json parsed = json::parse(in, nullptr, true, true);
  return parsed;
}

TemporalQualityTier ParseTier(const std::string& tier) {
  if (tier == "Low")
    return TemporalQualityTier::Low;
  if (tier == "Medium")
    return TemporalQualityTier::Medium;
  if (tier == "High")
    return TemporalQualityTier::High;
  if (tier == "Ultra")
    return TemporalQualityTier::Ultra;
  FAIL("Unknown tier string: " << tier);
  return TemporalQualityTier::Disabled;
}

float ComputeTemporalInstabilityIndex(const ScreenSpaceReflectionQualityConfig& ssr,
                                      const ScreenSpaceGlobalIlluminationQualityConfig& ssgi,
                                      const RadianceCacheFinalGatherTierExpectation& finalGather) {
  const float reusePenalty = std::clamp(1.0f - finalGather.expectedTemporalReuse, 0.0f, 1.0f);
  const float historyPenalty = ssgi.enableTemporalAccumulation
                                   ? std::clamp(1.0f - ssgi.temporalHistoryBlend, 0.0f, 1.0f)
                                   : 1.0f;
  const float reflectionPenalty = ssr.enableTemporalAccumulation
                                      ? std::clamp(ssr.missFallbackBlend, 0.0f, 1.0f)
                                      : 1.0f;
  return std::clamp((0.45f * reusePenalty) + (0.35f * historyPenalty) + (0.20f * reflectionPenalty),
                    0.0f,
                    1.0f);
}

bool NearlyEqual(float a, float b, float epsilon = kFloatEpsilon) {
  return std::fabs(a - b) <= epsilon;
}

}  // namespace

TEST_CASE("GI/reflection regression baseline matches renderer tier contracts",
          "[renderer][gi][reflection][regression]") {
  const std::filesystem::path baselinePath =
      RepoRoot() / "assets" / "regression" / "gi_reflection" / "baseline_expectations.json";
  const json baseline = ReadJsonFile(baselinePath);

  REQUIRE(baseline.at("version").get<int>() == 1);
  const json& tiers = baseline.at("qualityTiers");

  const std::array<std::string, 4> orderedTiers = {"Low", "Medium", "High", "Ultra"};
  int previousGatherSamples = -1;
  float previousInstabilityBudget = 2.0f;

  for (const std::string& tierName : orderedTiers) {
    INFO("tier: " << tierName);
    const json& tierBaseline = tiers.at(tierName);
    const TemporalQualityTier tier = ParseTier(tierName);

    const ScreenSpaceReflectionQualityConfig ssr = BuildScreenSpaceReflectionQualityConfig(tier);
    const ScreenSpaceGlobalIlluminationQualityConfig ssgi =
        BuildScreenSpaceGlobalIlluminationQualityConfig(tier);
    const RadianceCacheFinalGatherTierExpectation finalGather =
        BuildRadianceCacheFinalGatherTierExpectation(tier);

    const json& ssrBaseline = tierBaseline.at("ssr");
    REQUIRE(ssr.maxTraceSteps == ssrBaseline.at("maxTraceSteps").get<uint32_t>());
    REQUIRE(ssr.resolveStride == ssrBaseline.at("resolveStride").get<uint32_t>());
    REQUIRE(ssr.binaryRefinementSteps == ssrBaseline.at("binaryRefinementSteps").get<uint32_t>());
    REQUIRE(NearlyEqual(ssr.maxRoughnessForTracing, ssrBaseline.at("maxRoughnessForTracing").get<float>()));
    REQUIRE(NearlyEqual(ssr.thickness, ssrBaseline.at("thickness").get<float>()));
    REQUIRE(NearlyEqual(ssr.missFallbackBlend, ssrBaseline.at("missFallbackBlend").get<float>()));
    REQUIRE(ssr.enableTemporalAccumulation ==
            ssrBaseline.at("enableTemporalAccumulation").get<bool>());

    const json& ssgiBaseline = tierBaseline.at("ssgi");
    REQUIRE(ssgi.maxTraceSteps == ssgiBaseline.at("maxTraceSteps").get<uint32_t>());
    REQUIRE(ssgi.sampleKernelSize == ssgiBaseline.at("sampleKernelSize").get<uint32_t>());
    REQUIRE(ssgi.resolveStride == ssgiBaseline.at("resolveStride").get<uint32_t>());
    REQUIRE(NearlyEqual(ssgi.thickness, ssgiBaseline.at("thickness").get<float>()));
    REQUIRE(NearlyEqual(ssgi.temporalHistoryBlend, ssgiBaseline.at("temporalHistoryBlend").get<float>()));
    REQUIRE(NearlyEqual(ssgi.emissiveContributionScale,
                        ssgiBaseline.at("emissiveContributionScale").get<float>()));
    REQUIRE(ssgi.enableTemporalAccumulation ==
            ssgiBaseline.at("enableTemporalAccumulation").get<bool>());
    REQUIRE(ssgi.enableEmissiveContribution ==
            ssgiBaseline.at("enableEmissiveContribution").get<bool>());

    const json& finalGatherBaseline = tierBaseline.at("finalGather");
    REQUIRE(finalGather.gatherSamplesPerPixel ==
            finalGatherBaseline.at("gatherSamplesPerPixel").get<uint32_t>());
    REQUIRE(finalGather.probeUpdateBudget == finalGatherBaseline.at("probeUpdateBudget").get<uint32_t>());
    REQUIRE(finalGather.raysPerProbe == finalGatherBaseline.at("raysPerProbe").get<uint32_t>());
    REQUIRE(NearlyEqual(finalGather.expectedMinCacheHitRatio,
                        finalGatherBaseline.at("expectedMinCacheHitRatio").get<float>()));
    REQUIRE(NearlyEqual(finalGather.expectedTemporalReuse,
                        finalGatherBaseline.at("expectedTemporalReuse").get<float>()));

    const json& temporalBaseline = tierBaseline.at("temporal");
    const float computedInstability = ComputeTemporalInstabilityIndex(ssr, ssgi, finalGather);
    REQUIRE(NearlyEqual(computedInstability, temporalBaseline.at("baselineInstabilityIndex").get<float>()));
    const float maxBudget = temporalBaseline.at("maxTemporalInstabilityIndex").get<float>();
    REQUIRE(computedInstability <= maxBudget);

    REQUIRE(static_cast<int>(finalGather.gatherSamplesPerPixel) > previousGatherSamples);
    REQUIRE(maxBudget < previousInstabilityBudget);
    previousGatherSamples = static_cast<int>(finalGather.gatherSamplesPerPixel);
    previousInstabilityBudget = maxBudget;
  }
}

TEST_CASE("GI/reflection scene regression pack encodes tier and temporal stability coverage",
          "[renderer][gi][reflection][regression]") {
  const std::filesystem::path root = RepoRoot();
  const json baseline =
      ReadJsonFile(root / "assets" / "regression" / "gi_reflection" / "baseline_expectations.json");
  const json pack = ReadJsonFile(root / "assets" / "regression" / "gi_reflection" / "scene_pack.json");

  REQUIRE(pack.at("version").get<int>() == 1);
  const json& tiers = baseline.at("qualityTiers");
  const json& scenarios = pack.at("scenarios");
  REQUIRE_FALSE(scenarios.empty());

  std::set<std::string> referencedTiers;
  bool hasStable = false;
  bool hasUnstable = false;

  for (const json& scenario : scenarios) {
    const std::string id = scenario.at("id").get<std::string>();
    INFO("scenario: " << id);

    const std::string tierName = scenario.at("tier").get<std::string>();
    REQUIRE(tiers.contains(tierName));
    referencedTiers.insert(tierName);

    const std::string expectedStability = scenario.at("expectedStability").get<std::string>();
    REQUIRE((expectedStability == "stable" || expectedStability == "unstable"));
    hasStable = hasStable || expectedStability == "stable";
    hasUnstable = hasUnstable || expectedStability == "unstable";

    const TemporalQualityTier tier = ParseTier(tierName);
    const ScreenSpaceReflectionQualityConfig ssr = BuildScreenSpaceReflectionQualityConfig(tier);
    const ScreenSpaceGlobalIlluminationQualityConfig ssgi =
        BuildScreenSpaceGlobalIlluminationQualityConfig(tier);
    const RadianceCacheFinalGatherTierExpectation finalGather =
        BuildRadianceCacheFinalGatherTierExpectation(tier);
    const float baselineInstability = ComputeTemporalInstabilityIndex(ssr, ssgi, finalGather);

    const float instabilityBias = scenario.value("instabilityBias", 0.0f);
    const float scenarioInstability = std::clamp(baselineInstability + instabilityBias, 0.0f, 1.0f);
    const float maxBudget =
        tiers.at(tierName).at("temporal").at("maxTemporalInstabilityIndex").get<float>();
    const bool stable = scenarioInstability <= maxBudget;

    if (expectedStability == "stable") {
      REQUIRE(stable);
    } else {
      REQUIRE_FALSE(stable);
    }
  }

  REQUIRE(referencedTiers.size() >= 3u);
  REQUIRE(hasStable);
  REQUIRE(hasUnstable);
}
