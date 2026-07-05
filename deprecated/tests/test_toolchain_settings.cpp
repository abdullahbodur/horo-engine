/**
 * @file test_toolchain_settings.cpp
 * @brief Unit tests for ToolchainSettings and ToolchainSettingsStore.
 */

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "core/pipeline/ToolchainSettings.h"
#include "core/pipeline/ReleaseTypes.h"

using namespace Horo::Build;

TEST_CASE("ReleaseTargetTriple::StringRoundTrip") {
    ReleaseTargetTriple triple{BuildTargetOS::Linux, BuildArch::x86_64};

    std::string str = triple.ToString();
    ReleaseTargetTriple parsed;
    bool success = ReleaseTargetTriple::TryParse(str, parsed);

    CHECK(str == "linux-x86_64");
    CHECK(success);
    CHECK(parsed.os == BuildTargetOS::Linux);
    CHECK(parsed.arch == BuildArch::x86_64);
}

TEST_CASE("ReleaseTargetTriple::AllPlatformFormats") {
    ReleaseTargetTriple win64{BuildTargetOS::Windows, BuildArch::x86_64};
    CHECK(win64.ToString() == "windows-x86_64");

    ReleaseTargetTriple macArm{BuildTargetOS::MacOS, BuildArch::Arm64};
    CHECK(macArm.ToString() == "macos-arm64");
}

TEST_CASE("ReleaseTargetTriple::ParseInvalid") {
    ReleaseTargetTriple triple;
    CHECK(!ReleaseTargetTriple::TryParse("invalid", triple));
    CHECK(!ReleaseTargetTriple::TryParse("linux-invalid", triple));
}

TEST_CASE("ToolchainConfig::IsConfigured") {
    ToolchainConfig config;
    config.name = "test";
    config.targetTriple = {BuildTargetOS::Linux, BuildArch::x86_64};

    CHECK(!config.IsConfigured());

    config.cmakeToolchainFilePath = "/path/to/toolchain.cmake";
    config.compilerPath = "/usr/bin/g++";
    config.linkerPath = "/usr/bin/ld";
    config.sysrootPath = "/path/to/sysroot";
    config.cmakeGenerator = "Ninja";

    CHECK(config.IsConfigured());
}

TEST_CASE("ToolchainConfig::IsValid") {
    ToolchainConfig config;
    config.targetTriple = {BuildTargetOS::Linux, BuildArch::x86_64};

    CHECK(!config.IsValid());

    config.lastValidationResult.status = ToolchainValidationResult::Status::Valid;
    CHECK(config.IsValid());

    config.lastValidationResult.status = ToolchainValidationResult::Status::Invalid;
    CHECK(!config.IsValid());
}

TEST_CASE("ToolchainSettingsStore::AddAndRetrieve") {
    ToolchainSettingsStore store;
    ToolchainConfig config;
    config.name = "MyGCC";
    config.targetTriple = {BuildTargetOS::Linux, BuildArch::x86_64};

    store.AddToolchain(config);

    auto found = store.FindToolchain(config.targetTriple, "MyGCC");
    CHECK(found != nullptr);
    CHECK(found->name == "MyGCC");
}

TEST_CASE("ToolchainSettingsStore::Remove") {
    ToolchainSettingsStore store;
    ToolchainConfig config;
    config.name = "MyGCC";
    config.targetTriple = {BuildTargetOS::Linux, BuildArch::x86_64};
    store.AddToolchain(config);

    store.RemoveToolchain(config.targetTriple, "MyGCC");

    auto found = store.FindToolchain(config.targetTriple, "MyGCC");
    CHECK(found == nullptr);
}

TEST_CASE("ToolchainSettingsStore::GetToolchainsForTarget") {
    ToolchainSettingsStore store;
    ReleaseTargetTriple linuxTarget{BuildTargetOS::Linux, BuildArch::x86_64};
    ReleaseTargetTriple windowsTarget{BuildTargetOS::Windows, BuildArch::x86_64};

    ToolchainConfig config1;
    config1.name = "GCC";
    config1.targetTriple = linuxTarget;

    ToolchainConfig config2;
    config2.name = "MSVC";
    config2.targetTriple = windowsTarget;

    store.AddToolchain(config1);
    store.AddToolchain(config2);

    auto linuxTools = store.GetToolchainsForTarget(linuxTarget);
    auto windowsTools = store.GetToolchainsForTarget(windowsTarget);

    CHECK(linuxTools.size() == 1);
    CHECK(windowsTools.size() == 1);
}

TEST_CASE("ToolchainSettingsStore::Validate") {
    ToolchainSettingsStore store;
    ToolchainConfig config;
    config.name = "test";
    config.targetTriple = {BuildTargetOS::Linux, BuildArch::x86_64};
    config.cmakeToolchainFilePath = "/nonexistent/toolchain.cmake";
    config.compilerPath = "/nonexistent/g++";
    config.linkerPath = "/nonexistent/ld";
    config.sysrootPath = "/nonexistent/sysroot";
    config.cmakeGenerator = "Ninja";

    store.ValidateToolchain(config);

    CHECK(config.lastValidationResult.status == ToolchainValidationResult::Status::Invalid);
    CHECK(config.lastValidationResult.checks.size() >= 4);
    CHECK(config.lastValidationTime > 0);
}

TEST_CASE("ToolchainSettingsStore::InvalidateAll") {
    ToolchainSettingsStore store;
    ToolchainConfig config;
    config.name = "test";
    config.targetTriple = {BuildTargetOS::Linux, BuildArch::x86_64};
    config.lastValidationTime = 12345;
    config.lastValidationResult.status = ToolchainValidationResult::Status::Valid;

    store.AddToolchain(config);
    store.InvalidateAll();

    auto found = store.FindToolchain(config.targetTriple, "test");
    CHECK(found->lastValidationTime == 0);
    CHECK(found->lastValidationResult.status == ToolchainValidationResult::Status::NotValidated);
}

TEST_CASE("ToolchainSettingsStore::Clear") {
    ToolchainSettingsStore store;
    ToolchainConfig config;
    config.name = "test";
    config.targetTriple = {BuildTargetOS::Linux, BuildArch::x86_64};

    store.AddToolchain(config);
    CHECK(!store.IsEmpty());

    store.Clear();
    CHECK(store.IsEmpty());
}

TEST_CASE("ToolchainSettingsStore::FindValidEnabledToolchain") {
    ToolchainSettingsStore store;
    ReleaseTargetTriple target{BuildTargetOS::Linux, BuildArch::x86_64};

    ToolchainConfig validTool;
    validTool.name = "valid";
    validTool.targetTriple = target;
    validTool.enabled = true;
    validTool.lastValidationResult.status = ToolchainValidationResult::Status::Valid;

    store.AddToolchain(validTool);

    auto found = store.FindValidEnabledToolchain(target);
    CHECK(found != nullptr);
    CHECK(found->name == "valid");
}

TEST_CASE("ToolchainSettingsStore::FindValidEnabledToolchain_Disabled") {
    ToolchainSettingsStore store;
    ReleaseTargetTriple target{BuildTargetOS::Linux, BuildArch::x86_64};

    ToolchainConfig config;
    config.name = "test";
    config.targetTriple = target;
    config.enabled = false;
    config.lastValidationResult.status = ToolchainValidationResult::Status::Valid;

    store.AddToolchain(config);

    auto found = store.FindValidEnabledToolchain(target);
    CHECK(found == nullptr);
}

TEST_CASE("ToolchainSettingsStore::Validation_CMakeGenerator") {
    ToolchainSettingsStore store;
    ToolchainConfig config;
    config.name = "test_cmake";
    config.targetTriple = {BuildTargetOS::Linux, BuildArch::x86_64};
    config.cmakeToolchainFilePath = "/path/to/toolchain.cmake";
    config.compilerPath = "/usr/bin/g++";
    config.linkerPath = "/usr/bin/ld";
    config.sysrootPath = "/path/to/sysroot";

    // Without generator, it's not configured
    CHECK(!config.IsConfigured());

    // Add generator, should be configured
    config.cmakeGenerator = "Ninja";
    CHECK(config.IsConfigured());

    store.ValidateToolchain(config);

    // The paths are fake, so we expect Invalid status overall, but we want to check if cmake_generate passes/fails.
    bool foundCmakeCheck = false;
    for (const auto& check : config.lastValidationResult.checks) {
        if (check.checkName == "cmake_generate") {
            foundCmakeCheck = true;
            CHECK(check.severity == ToolchainCheckResult::Severity::Pass);
        }
    }
    CHECK(foundCmakeCheck);
}

TEST_CASE("ToolchainSettingsStore::Serialization") {
    ToolchainSettingsStore store;
    ToolchainConfig config;
    config.name = "test_serial";
    config.targetTriple = {BuildTargetOS::Windows, BuildArch::x86_64};
    config.cmakeToolchainFilePath = "/path/to/toolchain.cmake";
    config.cmakeGenerator = "Visual Studio 17 2022";
    config.compilerPath = "/path/to/cl.exe";
    config.linkerPath = "/path/to/link.exe";
    config.sysrootPath = "/path/to/sdk";
    config.enabled = false;

    store.AddToolchain(config);

    auto tempDir = std::filesystem::temp_directory_path() / "horo_test_toolchains";
    std::filesystem::create_directories(tempDir);
    auto tempFile = tempDir / "toolchains.json";

    bool saved = store.SaveToFile(tempFile);
    CHECK(saved);
    CHECK(std::filesystem::exists(tempFile));

    ToolchainSettingsStore store2;
    bool loaded = store2.LoadFromFile(tempFile);
    CHECK(loaded);

    auto loadedTc = store2.FindToolchain({BuildTargetOS::Windows, BuildArch::x86_64}, "test_serial");
    REQUIRE(loadedTc != nullptr);
    CHECK(loadedTc->name == "test_serial");
    CHECK(loadedTc->cmakeGenerator == "Visual Studio 17 2022");
    CHECK(loadedTc->enabled == false);

    std::filesystem::remove_all(tempDir);
}
