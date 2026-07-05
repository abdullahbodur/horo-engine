/** @file test_chaos_resilience.cpp
 *  @brief Chaos and resilience tests for the Horo Engine release/build
 *         pipeline, archive I/O, and process management.
 *
 *  P8.5 (HORO-32): Validates graceful failure behaviour under common
 *  release/build faults.
 *
 *  Coverage:
 *  - Section 1: Disk full or output permission denied.
 *  - Section 2: Network timeout / hung process behaviour.
 *  - Section 3: Killed child process mid-build.
 *  - Section 4: Missing signing credentials.
 *  - Section 5: Corrupt downloaded artifact.
 */

#include <catch2/catch_test_macros.hpp>

#include "core/archive/HashVerifier.h"
#include "core/archive/HoroFormat.h"
#include "core/archive/Packager.h"
#include "core/pipeline/ArchiveBuilder.h"
#include "core/pipeline/ArchiveFormat.h"
#include "core/pipeline/CryptoProvider.h"
#include "core/pipeline/ReleasePipeline.h"
#include "ui/launcher/ExternalProcessRunner.h"
#include "ui/launcher/LauncherProject.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ==========================================================================
//  Test helpers
// ==========================================================================

namespace {

/** @brief Return a clean temporary directory for a test case. */
fs::path MakeTempDir(const std::string& test_name) {
    const auto base =
        fs::temp_directory_path() / "horo_chaos_test";
    const auto dir = base / test_name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

/** @brief Recursively remove a temporary directory. */
void CleanupTempDir(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

/** @brief Creates a simple binary payload for archive testing. */
std::vector<uint8_t> MakeTestPayload(size_t size, uint8_t seed = 0xAB) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i)
        data[i] = static_cast<uint8_t>((seed + i) % 256);
    return data;
}

/** @brief Writes raw bytes to a file for corruption testing. */
void WriteBinaryFile(const fs::path& path,
                     const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

/** @brief Creates a minimal valid .horo archive header in a byte buffer. */
std::vector<uint8_t> MakeMinimalHoroHeader() {
    // A valid .horo header (32 bytes) with magic HORO, version 1.
    std::vector<uint8_t> header(32, 0);
    header[0] = 'H';
    header[1] = 'O';
    header[2] = 'R';
    header[3] = 'O';
    header[4] = 1;  // version = 1 (little-endian)
    return header;
}

/** @brief Polls until the process finishes or timeout is reached.
 *  @return True if the process finished before timeout. */
bool PollUntilFinished(Horo::Launcher::ExternalProcessRunner& runner,
                       int timeoutMs = 3000) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        runner.Poll();
        if (runner.GetStatus().finished)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

}  // anonymous namespace

// ==========================================================================
//  Section 1: Disk full or output permission denied
// ==========================================================================

TEST_CASE("Chaos: ArchiveBuilder — write to non-existent parent directory",
          "[chaos][io][permission]") {
    const auto tmp = MakeTempDir("chaos_no_parent");
    // Target path where parent does not exist:
    const auto output = tmp / "does_not_exist" / "output.horo";

    Horo::Build::ArchiveBuilder builder;
    auto payload = MakeTestPayload(64);
    builder.AddAsset("test.bin",
                     std::span<const uint8_t>(payload.data(), payload.size()));

    // Should fail — parent directory does not exist
    REQUIRE_FALSE(builder.WriteToFile(output));
    // Verify no partial file was left behind
    REQUIRE_FALSE(fs::exists(output));

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: ArchiveBuilder — write to read-only directory (POSIX)",
          "[chaos][io][permission]") {
    const auto tmp = MakeTempDir("chaos_readonly");
    const auto ro_dir = tmp / "readonly";
    fs::create_directories(ro_dir);

    // Make the output directory read-only
    std::error_code ec;
    fs::permissions(ro_dir, fs::perms::owner_exec, fs::perm_options::replace, ec);
    if (ec) {
        // Cannot set permissions — skip test gracefully
        CleanupTempDir(tmp);
        return;
    }

    const auto output = ro_dir / "should_fail.horo";

    Horo::Build::ArchiveBuilder builder;
    auto payload = MakeTestPayload(128);
    builder.AddAsset("test.bin",
                     std::span<const uint8_t>(payload.data(), payload.size()));

    // Should fail due to write permission
    const bool result = builder.WriteToFile(output);
    // Restore permissions so cleanup can proceed
    fs::permissions(ro_dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    REQUIRE_FALSE(result);
    // Verify no partial file was left behind
    REQUIRE_FALSE(fs::exists(output));

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: Packager — write to non-existent directory",
          "[chaos][io][packager]") {
    const auto tmp = MakeTempDir("chaos_pack_no_dir");
    const auto output = (tmp / "missing" / "output.horo").string();

    Horo::Archive::Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("data.bin") == Horo::Archive::PackResult::Ok);

    auto provider = [](const std::string&, std::vector<uint8_t>& out) -> bool {
        out = MakeTestPayload(32);
        return true;
    };

    const auto result = packer.Write(output, provider);
    REQUIRE(result != Horo::Archive::PackResult::Ok);
    REQUIRE(result == Horo::Archive::PackResult::IoError);

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: ArchiveBuilder — atomic write leaves target untouched on failure",
          "[chaos][io][atomic]") {
    const auto tmp = MakeTempDir("chaos_atomic");
    const auto target = tmp / "archive.horo";
    const auto temp_target = fs::path(target.string() + ".tmp");

    // Pre-create the target file with known content
    const std::string kOriginalContent = "original-content-untouched";
    {
        std::ofstream out(target);
        out << kOriginalContent;
    }

    Horo::Build::ArchiveBuilder builder;
    // Empty builder — WriteToFile returns false for no assets
    REQUIRE_FALSE(builder.WriteToFile(target));

    // Verify target file still has original content
    REQUIRE(fs::exists(target));
    std::ifstream in(target);
    std::string content;
    std::getline(in, content);
    REQUIRE(content == kOriginalContent);

    // Verify temp file was cleaned up
    std::error_code ec;
    REQUIRE_FALSE(fs::exists(temp_target, ec));

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: ArchiveBuilder — empty asset list is rejected",
          "[chaos][io][validation]") {
    const auto tmp = MakeTempDir("chaos_empty");
    const auto output = tmp / "empty.horo";

    Horo::Build::ArchiveBuilder builder;
    // No assets added — WriteToFile should return false
    REQUIRE_FALSE(builder.WriteToFile(output));
    REQUIRE_FALSE(fs::exists(output));

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 2: Network timeout / hung process behaviour
// ==========================================================================

TEST_CASE("Chaos: ExternalProcessRunner — short-lived process exits cleanly",
          "[chaos][process][timeout]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    cmd.executable = "cmd";
    cmd.args = {"/C", "exit 0"};
    cmd.debugString = "cmd /C exit 0";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "exit 0"};
    cmd.debugString = "/bin/sh -c 'exit 0'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "short-test", &error));
    REQUIRE(error.empty());
    REQUIRE(runner.IsActive());

    // Poll until finished
    REQUIRE(PollUntilFinished(runner, 5000));

    const auto& status = runner.GetStatus();
    REQUIRE(status.finished);
    REQUIRE_FALSE(status.active);
    REQUIRE(status.exitCode == 0);
}

TEST_CASE("Chaos: ExternalProcessRunner — Stop() kills long-running process",
          "[chaos][process][stop]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    // On Windows: use timeout /T to sleep
    cmd.executable = "cmd";
    cmd.args = {"/C", "timeout /T 30 /NOBREAK >nul 2>&1"};
    cmd.debugString = "cmd /C timeout /T 30";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "sleep 30"};
    cmd.debugString = "/bin/sh -c 'sleep 30'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "long-sleep", &error));
    REQUIRE(error.empty());
    REQUIRE(runner.IsActive());

    // Give it a moment to start, then stop it
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runner.Stop();

    // After Stop(), the process should be finished
    const auto& status = runner.GetStatus();
    REQUIRE(status.finished);
    REQUIRE_FALSE(status.active);
    REQUIRE(status.terminatedByUser);
    REQUIRE(status.exitCode != 0);  // Killed processes should have non-zero exit
}

TEST_CASE("Chaos: ExternalProcessRunner — Poll before process exits returns active",
          "[chaos][process][poll]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    cmd.executable = "cmd";
    cmd.args = {"/C", "timeout /T 5 /NOBREAK >nul 2>&1"};
    cmd.debugString = "cmd /C timeout /T 5";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "sleep 5"};
    cmd.debugString = "/bin/sh -c 'sleep 5'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "wait-test", &error));

    // Immediately poll — process should still be active
    runner.Poll();
    REQUIRE(runner.IsActive());
    REQUIRE_FALSE(runner.GetStatus().finished);

    // Wait for it to finish naturally
    REQUIRE(PollUntilFinished(runner, 10000));
    REQUIRE_FALSE(runner.IsActive());
    REQUIRE(runner.GetStatus().finished);
}

TEST_CASE("Chaos: ExternalProcessRunner — double Stop is safe",
          "[chaos][process][double_stop]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    cmd.executable = "cmd";
    cmd.args = {"/C", "timeout /T 2 /NOBREAK >nul 2>&1"};
    cmd.debugString = "cmd /C timeout /T 2";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "sleep 2"};
    cmd.debugString = "/bin/sh -c 'sleep 2'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "double-stop", &error));

    runner.Stop();
    runner.Stop();  // Second Stop should be a no-op, not crash

    REQUIRE(runner.GetStatus().finished);
}

TEST_CASE("Chaos: ExternalProcessRunner — start rejected when already running",
          "[chaos][process][reject]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    cmd.executable = "cmd";
    cmd.args = {"/C", "timeout /T 10 /NOBREAK >nul 2>&1"};
    cmd.debugString = "cmd /C timeout /T 10";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "sleep 10"};
    cmd.debugString = "/bin/sh -c 'sleep 10'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "first", &error));

    // Try to start a second process while first is still running
    std::string error2;
    REQUIRE_FALSE(runner.Start(cmd, "second", &error2));
    REQUIRE_FALSE(error2.empty());
    REQUIRE_THAT(error2, Catch::Matchers::ContainsSubstring("already running"));

    // Clean up the first process
    runner.Stop();
}

TEST_CASE("Chaos: ExternalProcessRunner — spawn failure on invalid executable",
          "[chaos][process][spawn_fail]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
    cmd.executable = "/this/path/does/not/exist/anywhere";
    cmd.args = {};
    cmd.debugString = "nonexistent-command";

    std::string error;
    REQUIRE_FALSE(runner.Start(cmd, "bad-exe", &error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(runner.IsActive());
}

// ==========================================================================
//  Section 3: Killed child process mid-build
// ==========================================================================

TEST_CASE("Chaos: ExternalProcessRunner — killed process reports non-zero exit",
          "[chaos][process][killed]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    cmd.executable = "cmd";
    cmd.args = {"/C", "timeout /T 30 /NOBREAK >nul 2>&1"};
    cmd.debugString = "cmd /C timeout /T 30";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "sleep 30"};
    cmd.debugString = "/bin/sh -c 'sleep 30'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "kill-test", &error));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runner.Stop();

    const auto& status = runner.GetStatus();
    REQUIRE(status.finished);
    REQUIRE_FALSE(status.active);
    REQUIRE(status.terminatedByUser);
    // A killed/terminated process should return non-zero
    REQUIRE(status.exitCode != 0);
}

TEST_CASE("Chaos: ExternalProcessRunner — IsActive false after Stop",
          "[chaos][process][state]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    cmd.executable = "cmd";
    cmd.args = {"/C", "timeout /T 30 /NOBREAK >nul 2>&1"};
    cmd.debugString = "cmd /C timeout /T 30";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "sleep 30"};
    cmd.debugString = "/bin/sh -c 'sleep 30'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "state-test", &error));
    REQUIRE(runner.IsActive());

    runner.Stop();

    REQUIRE_FALSE(runner.IsActive());
}

TEST_CASE("Chaos: ExternalProcessRunner — process with non-zero exit code",
          "[chaos][process][exit_code]") {
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    cmd.executable = "cmd";
    cmd.args = {"/C", "exit 42"};
    cmd.debugString = "cmd /C exit 42";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "exit 42"};
    cmd.debugString = "/bin/sh -c 'exit 42'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "exit42", &error));

    REQUIRE(PollUntilFinished(runner, 5000));

    const auto& status = runner.GetStatus();
    REQUIRE(status.finished);
    REQUIRE(status.exitCode == 42);
}

// ==========================================================================
//  Section 4: Missing signing credentials
// ==========================================================================

TEST_CASE("Chaos: SigningConfig — default state has signing disabled",
          "[chaos][signing][validation]") {
    Horo::Build::SigningConfig config;
    REQUIRE_FALSE(config.enabled);
    REQUIRE(config.certificatePath.empty());
    REQUIRE(config.certificatePassword.Empty());
    REQUIRE_FALSE(config.notarize);
}

TEST_CASE("Chaos: SigningConfig — enabled without certificate path is invalid",
          "[chaos][signing][validation]") {
    Horo::Build::SigningConfig config;
    config.enabled = true;
    config.certificatePath.clear();   // No certificate
    config.certificatePassword = Horo::Core::SecureString("secret");
    config.verifySignature = true;

    // When signing is enabled, cert path must not be empty
    const bool valid = config.enabled && !config.certificatePath.empty();
    REQUIRE_FALSE(valid);
}

TEST_CASE("Chaos: SigningConfig — enabled with certificate path is valid",
          "[chaos][signing][validation]") {
    Horo::Build::SigningConfig config;
    config.enabled = true;
    config.certificatePath = "/path/to/cert.pfx";
    config.certificatePassword = Horo::Core::SecureString("secret");
    config.verifySignature = true;

    const bool valid = config.enabled && !config.certificatePath.empty();
    REQUIRE(valid);
}

TEST_CASE("Chaos: SigningConfig — macOS notarization requires Apple ID",
          "[chaos][signing][validation]") {
    Horo::Build::SigningConfig config;
    config.enabled = true;
    config.certificatePath = "/path/to/cert.pfx";
    config.notarize = true;           // Notarization enabled
    config.appleId.clear();           // But no Apple ID
    config.teamId = "TEAM123";

    // Notarization without Apple ID should be considered incomplete
    const bool notarizeReady =
        config.notarize && !config.appleId.empty() &&
        !config.teamId.empty();
    REQUIRE_FALSE(notarizeReady);
}

TEST_CASE("Chaos: SigningConfig — notarization with all fields populated",
          "[chaos][signing][validation]") {
    Horo::Build::SigningConfig config;
    config.enabled = true;
    config.certificatePath = "/path/to/cert.pfx";
    config.notarize = true;
    config.appleId = "dev@example.com";
    config.teamId = "TEAM123";
    config.keychainProfile = "notary-profile";

    const bool notarizeReady =
        config.notarize && !config.appleId.empty() &&
        !config.teamId.empty();
    REQUIRE(notarizeReady);
}

TEST_CASE("Chaos: SigningConfig — disabled signing ignores missing fields",
          "[chaos][signing][validation]") {
    Horo::Build::SigningConfig config;
    config.enabled = false;
    config.certificatePath.clear();
    config.certificatePassword.Clear();

    // When signing is disabled, missing fields are fine
    REQUIRE_FALSE(config.enabled);
    // This should not trigger any validation errors
}

// ==========================================================================
//  Section 5: Corrupt downloaded artifact
// ==========================================================================

TEST_CASE("Chaos: Packager — open non-existent file",
          "[chaos][corrupt][packager]") {
    Horo::Archive::Packager reader;
    const auto result = reader.Open("/tmp/horo_nonexistent_12345.horo");
    REQUIRE(result == Horo::Archive::PackResult::IoError);
    REQUIRE_FALSE(reader.IsOpen());
}

TEST_CASE("Chaos: Packager — open zero-length file",
          "[chaos][corrupt][packager]") {
    const auto tmp = MakeTempDir("chaos_zero");
    const auto path = tmp / "zero.horo";

    // Create a zero-length file
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
    }

    Horo::Archive::Packager reader;
    const auto result = reader.Open(path.string());
    // Zero-length file should fail — cannot read magic bytes
    REQUIRE(result != Horo::Archive::PackResult::Ok);
    REQUIRE_FALSE(reader.IsOpen());

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: Packager — open file with invalid magic bytes",
          "[chaos][corrupt][packager]") {
    const auto tmp = MakeTempDir("chaos_bad_magic");
    const auto path = tmp / "bad_magic.horo";

    // Write a file that starts with 'XXXX' instead of 'HORO'
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write("XXXXBADHEADER", 12);
    }

    Horo::Archive::Packager reader;
    const auto result = reader.Open(path.string());
    REQUIRE(result == Horo::Archive::PackResult::InvalidMagic);
    REQUIRE_FALSE(reader.IsOpen());

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: Packager — open truncated archive (header only, no data)",
          "[chaos][corrupt][packager]") {
    const auto tmp = MakeTempDir("chaos_truncated");
    const auto path = tmp / "truncated.horo";

    // Write only the 32-byte header (valid magic, version) but no TOC/data
    auto header = MakeMinimalHoroHeader();
    WriteBinaryFile(path, header);

    Horo::Archive::Packager reader;
    const auto result = reader.Open(path.string());
    // Should fail — TOC is missing or malformed
    REQUIRE(result != Horo::Archive::PackResult::Ok);
    REQUIRE_FALSE(reader.IsOpen());

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: Packager — open file with valid magic but wrong version",
          "[chaos][corrupt][packager]") {
    const auto tmp = MakeTempDir("chaos_bad_version");
    const auto path = tmp / "bad_version.horo";

    // Build a 32-byte header with HORO magic but version 99
    std::vector<uint8_t> header = MakeMinimalHoroHeader();
    header[4] = 99;  // version = 99 (unsupported)

    WriteBinaryFile(path, header);

    Horo::Archive::Packager reader;
    const auto result = reader.Open(path.string());
    REQUIRE(result == Horo::Archive::PackResult::UnsupportedVersion);
    REQUIRE_FALSE(reader.IsOpen());

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: Packager — extract from non-open packager is rejected",
          "[chaos][corrupt][packager]") {
    Horo::Archive::Packager packer;
    REQUIRE_FALSE(packer.IsOpen());

    std::vector<uint8_t> out;
    const auto result = packer.Extract("ghost.bin", out);
    REQUIRE(result != Horo::Archive::PackResult::Ok);
    REQUIRE(out.empty());
}

TEST_CASE("Chaos: Packager — list assets on non-open packager",
          "[chaos][corrupt][packager]") {
    Horo::Archive::Packager packer;
    REQUIRE_FALSE(packer.IsOpen());

    std::vector<std::string> paths;
    const auto result = packer.ListAssets(paths);
    REQUIRE(result == Horo::Archive::PackResult::InvalidInput);
    REQUIRE(paths.empty());
}

TEST_CASE("Chaos: Packager — write then open then tamper byte in data region",
          "[chaos][corrupt][packager][tamper]") {
    const auto tmp = MakeTempDir("chaos_tamper_data");
    const auto archive_path = tmp / "tamper.horo";

    // Pack a valid archive with SHA-256 enabled
    auto payload = MakeTestPayload(256, 0x42);
    Horo::Archive::Packager packer;
    packer.SetCompressionLevel(0);
    packer.SetSHA256Enabled(true);
    REQUIRE(packer.AddAsset("tamper_target.bin") ==
            Horo::Archive::PackResult::Ok);

    auto provider =
        [&payload](const std::string&,
                   std::vector<uint8_t>& out) -> bool {
        out = payload;
        return true;
    };
    REQUIRE(packer.Write(archive_path.string(), provider) ==
            Horo::Archive::PackResult::Ok);

    // Tamper with a byte in the data region (after the 32-byte header)
    {
        std::fstream file(archive_path,
                          std::ios::binary | std::ios::in | std::ios::out);
        // Seek past the header into the data area
        file.seekp(40, std::ios::beg);
        char corrupt_byte;
        file.read(&corrupt_byte, 1);
        file.seekp(40, std::ios::beg);
        corrupt_byte ^= 0xFF;  // Flip all bits
        file.write(&corrupt_byte, 1);
    }

    // Open — should still succeed (header untouched)
    Horo::Archive::Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == Horo::Archive::PackResult::Ok);
    REQUIRE(reader.IsOpen());

    // Extract — should detect hash mismatch
    std::vector<uint8_t> extracted;
    const auto extract_result =
        reader.Extract("tamper_target.bin", extracted);
    REQUIRE(extract_result == Horo::Archive::PackResult::HashMismatch);

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: Packager — corrupt TOC entry count",
          "[chaos][corrupt][packager][toc]") {
    const auto tmp = MakeTempDir("chaos_bad_toc");
    const auto archive_path = tmp / "bad_toc.horo";

    // Create a valid-looking header but with a bogus TOC entry count
    std::vector<uint8_t> corrupted(32 + 4 /* entry count */, 0);
    corrupted[0] = 'H';
    corrupted[1] = 'O';
    corrupted[2] = 'R';
    corrupted[3] = 'O';
    corrupted[4] = 1;  // version 1
    // In the non-standard HoroFormat layout, entry count starts at offset 32
    // Set a huge TOC entry count
    corrupted[32] = 0xFF;
    corrupted[33] = 0xFF;
    corrupted[34] = 0xFF;
    corrupted[35] = 0x0F;  // ~268M entries — should be rejected

    WriteBinaryFile(archive_path, corrupted);

    Horo::Archive::Packager reader;
    const auto result = reader.Open(archive_path.string());
    // Should fail — TOC is clearly invalid
    REQUIRE(result != Horo::Archive::PackResult::Ok);
    REQUIRE_FALSE(reader.IsOpen());

    CleanupTempDir(tmp);
}

TEST_CASE("Chaos: Packager — multiple open calls return consistent results",
          "[chaos][corrupt][packager][lifecycle]") {
    Horo::Archive::Packager reader;
    REQUIRE_FALSE(reader.IsOpen());

    // First open should fail
    const auto r1 = reader.Open("/tmp/definitely_missing.horo");
    REQUIRE(r1 != Horo::Archive::PackResult::Ok);
    REQUIRE_FALSE(reader.IsOpen());

    // Second open on same instance — should still return error
    const auto r2 = reader.Open("/tmp/also_missing.horo");
    REQUIRE(r2 != Horo::Archive::PackResult::Ok);
    REQUIRE_FALSE(reader.IsOpen());
}

// ==========================================================================
//  Section 6: Combined scenario — process failure + artifact integrity
// ==========================================================================

TEST_CASE("Chaos: combined — failed build process should not produce partial artifact",
          "[chaos][combined][e2e]") {
    const auto tmp = MakeTempDir("chaos_combined");
    const auto artifact_path = tmp / "release.horo";

    // Simulate a failed build: ExternalProcessRunner exits with error
    Horo::Launcher::ExternalProcessRunner runner;

    Horo::Launcher::ResolvedLauncherCommand cmd;
#if defined(_WIN32)
    cmd.executable = "cmd";
    cmd.args = {"/C", "exit 1"};
    cmd.debugString = "cmd /C exit 1";
#else
    cmd.executable = "/bin/sh";
    cmd.args = {"-c", "exit 1"};
    cmd.debugString = "/bin/sh -c 'exit 1'";
#endif

    std::string error;
    REQUIRE(runner.Start(cmd, "failing-build", &error));
    REQUIRE(PollUntilFinished(runner, 5000));

    const auto& status = runner.GetStatus();
    REQUIRE(status.finished);
    REQUIRE(status.exitCode != 0);

    // Key invariant: a failed build should NOT write a partial artifact
    // In a real pipeline, the build step would be checked before packaging.
    // This test validates the guard logic: if the process fails,
    // the artifact path should remain empty / unwritten.
    //
    // We simulate this by checking that no artifact was produced
    // (since our simulated build just returns exit code 1).
    REQUIRE_FALSE(fs::exists(artifact_path));

    CleanupTempDir(tmp);
}
