#include <catch2/catch_test_macros.hpp>
#include "core/pipeline/TargetCapability.h"
#include "core/pipeline/ToolchainSettings.h"

using namespace Horo::Build;

TEST_CASE("TargetCapability Evaluates Native and Cross Compilation Scenarios", "[TargetCapability]") {
    ToolchainSettingsStore store;

    // Simulate Native Host
#if defined(_WIN32)
    const BuildTargetOS nativeOS = BuildTargetOS::Windows;
    const BuildTargetOS crossOS = BuildTargetOS::Linux;
#elif defined(__APPLE__)
    const BuildTargetOS nativeOS = BuildTargetOS::MacOS;
    const BuildTargetOS crossOS = BuildTargetOS::Windows;
#else
    const BuildTargetOS nativeOS = BuildTargetOS::Linux;
    const BuildTargetOS crossOS = BuildTargetOS::Windows;
#endif

    const BuildArch defaultArch = BuildArch::x86_64;

    SECTION("Native targets are always capable and enabled") {
        auto cap = EvaluateTargetCapability(nativeOS, defaultArch, store);
        CHECK(cap.os == nativeOS);
        CHECK(cap.arch == defaultArch);
        CHECK(cap.state == TargetCapabilityState::Native);
        CHECK(cap.IsEnabled());
        CHECK(cap.disableReason.empty());
    }

    SECTION("Missing toolchain results in unsupported state") {
        auto cap = EvaluateTargetCapability(crossOS, defaultArch, store);
        CHECK(cap.os == crossOS);
        CHECK(cap.arch == defaultArch);
        CHECK(cap.state == TargetCapabilityState::Unsupported);
        CHECK_FALSE(cap.IsEnabled());
        CHECK(cap.disableReason == "No cross-compilation toolchain configured for this target.");
        CHECK(FormatTargetCapabilityBlockReason(cap) ==
              std::string("Build blocked: ") + GetBuildTargetOSLabel(crossOS) +
                  " x86_64 target cannot be built locally. Reason: No cross-compilation toolchain configured for this target.");
    }

    SECTION("Configured but disabled toolchain results in disabled state") {
        ToolchainConfig config;
        config.targetTriple = {crossOS, defaultArch};
        config.name = "Test-Disabled";
        config.enabled = false;
        config.lastValidationResult.status = ToolchainValidationResult::Status::Valid;
        store.AddToolchain(config);

        auto cap = EvaluateTargetCapability(crossOS, defaultArch, store);
        CHECK(cap.state == TargetCapabilityState::DisabledToolchain);
        CHECK_FALSE(cap.IsEnabled());
        CHECK(cap.disableReason == "Toolchain is configured but disabled in settings.");
    }

    SECTION("Enabled but invalid toolchain results in invalid state") {
        ToolchainConfig config;
        config.targetTriple = {crossOS, defaultArch};
        config.name = "Test-Invalid";
        config.enabled = true;
        config.lastValidationResult.status = ToolchainValidationResult::Status::Invalid;
        store.AddToolchain(config);

        auto cap = EvaluateTargetCapability(crossOS, defaultArch, store);
        CHECK(cap.state == TargetCapabilityState::InvalidToolchain);
        CHECK_FALSE(cap.IsEnabled());
        CHECK(cap.disableReason == "Enabled toolchain failed validation checks.");
    }

    SECTION("Enabled and valid toolchain results in ready state") {
        ToolchainConfig config;
        config.targetTriple = {crossOS, defaultArch};
        config.name = "Test-Valid";
        config.enabled = true;
        config.lastValidationResult.status = ToolchainValidationResult::Status::Valid;
        store.AddToolchain(config);

        auto cap = EvaluateTargetCapability(crossOS, defaultArch, store);
        CHECK(cap.state == TargetCapabilityState::ReadyCrossCompile);
        CHECK(cap.IsEnabled());
        CHECK(cap.disableReason.empty());
    }

    SECTION("Fallback to valid toolchain when multiple exist and one is disabled/invalid") {
        ToolchainConfig invalidConfig;
        invalidConfig.targetTriple = {crossOS, defaultArch};
        invalidConfig.name = "Invalid-TC";
        invalidConfig.enabled = true;
        invalidConfig.lastValidationResult.status = ToolchainValidationResult::Status::Invalid;
        store.AddToolchain(invalidConfig);

        ToolchainConfig disabledConfig;
        disabledConfig.targetTriple = {crossOS, defaultArch};
        disabledConfig.name = "Disabled-TC";
        disabledConfig.enabled = false;
        disabledConfig.lastValidationResult.status = ToolchainValidationResult::Status::Valid;
        store.AddToolchain(disabledConfig);

        ToolchainConfig validConfig;
        validConfig.targetTriple = {crossOS, defaultArch};
        validConfig.name = "Valid-TC";
        validConfig.enabled = true;
        validConfig.lastValidationResult.status = ToolchainValidationResult::Status::Valid;
        store.AddToolchain(validConfig);

        auto cap = EvaluateTargetCapability(crossOS, defaultArch, store);
        CHECK(cap.state == TargetCapabilityState::ReadyCrossCompile);
        CHECK(cap.IsEnabled());
    }
}
