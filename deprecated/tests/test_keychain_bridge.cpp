/** @file test_keychain_bridge.cpp
 *  @brief Unit tests for KeychainBridge, MockKeychainProvider, and
 *         SignCommandForJob keychain integration.
 *
 *  All tests exercise the abstract IKeychainProvider interface via the
 *  mock backend.  No real Keychain access is attempted — macOS CI and
 *  non-macOS hosts are safe.
 */

#include <catch2/catch_test_macros.hpp>

#include "core/pipeline/KeychainBridge.h"
#include "core/pipeline/ReleasePipeline.h"

#include <memory>
#include <string>
#include <unordered_map>

using namespace Horo::Build;
using namespace Horo::Pipeline;

// ==========================================================================
//  Mock keychain provider
// ==========================================================================

namespace {

/** @brief In-memory keychain mock for unit tests.
 *
 *  Stores (service, account) → password mappings in a map.  Used to
 *  verify both the IKeychainProvider contract and the ReleasePipeline
 *  integration without touching the real macOS Keychain. */
class MockKeychainProvider final : public IKeychainProvider {
public:
    /** @brief Registers a password for a given (service, account) pair. */
    void SetPassword(const std::string &service,
                     const std::string &account,
                     const std::string &password) {
        m_store[MakeKey(service, account)] = password;
    }

    /** @brief Removes a (service, account) entry. */
    void RemovePassword(const std::string &service,
                        const std::string &account) {
        m_store.erase(MakeKey(service, account));
    }

    /** @brief Clears all stored entries. */
    void Clear() { m_store.clear(); }

    /** @brief Returns the number of stored entries. */
    std::size_t Count() const { return m_store.size(); }

    /** @copydoc IKeychainProvider::GetPassword */
    bool GetPassword(const std::string &service,
                     const std::string &account,
                     std::string &password) override {
        auto it = m_store.find(MakeKey(service, account));
        if (it == m_store.end())
            return false;
        password = it->second;
        return true;
    }

private:
    /** @brief Composite key from service and account. */
    static std::string MakeKey(const std::string &service,
                               const std::string &account) {
        return service + '\0' + account;
    }

    std::unordered_map<std::string, std::string> m_store;
};

} // anonymous namespace

// ==========================================================================
//  Section 1: MockKeychainProvider basic contract
// ==========================================================================

TEST_CASE("KeychainBridge: Mock provider — basic get/set", "[keychain][mock]") {
    MockKeychainProvider mock;

    SECTION("Retrieve a stored password") {
        mock.SetPassword("com.horo.test", "signing-cert", "p@ssw0rd!");
        std::string result;
        REQUIRE(mock.GetPassword("com.horo.test", "signing-cert", result));
        REQUIRE(result == "p@ssw0rd!");
    }

    SECTION("Missing entry returns false and does not modify output") {
        std::string result = "untouched";
        REQUIRE_FALSE(mock.GetPassword("nonexistent", "account", result));
        REQUIRE(result == "untouched");
    }

    SECTION("Empty password is valid") {
        mock.SetPassword("svc", "acct", "");
        std::string result = "before";
        REQUIRE(mock.GetPassword("svc", "acct", result));
        REQUIRE(result.empty());
    }

    SECTION("Service/account differentiate entries") {
        mock.SetPassword("svc", "acct1", "pass1");
        mock.SetPassword("svc", "acct2", "pass2");
        std::string result;

        REQUIRE(mock.GetPassword("svc", "acct1", result));
        REQUIRE(result == "pass1");

        REQUIRE(mock.GetPassword("svc", "acct2", result));
        REQUIRE(result == "pass2");
    }

    SECTION("Remove clears an entry") {
        mock.SetPassword("svc", "acct", "secret");
        mock.RemovePassword("svc", "acct");
        std::string result;
        REQUIRE_FALSE(mock.GetPassword("svc", "acct", result));
    }

    SECTION("Clear removes all entries") {
        mock.SetPassword("a", "1", "p1");
        mock.SetPassword("b", "2", "p2");
        REQUIRE(mock.Count() == 2);
        mock.Clear();
        REQUIRE(mock.Count() == 0);
    }
}

// ==========================================================================
//  Section 2: Factory — platform-appropriate provider
// ==========================================================================

TEST_CASE("KeychainBridge: factory creates non-null provider", "[keychain][factory]") {
    auto provider = CreateKeychainProvider();
    REQUIRE(provider != nullptr);
}

TEST_CASE("KeychainBridge: factory provider respects interface contract",
          "[keychain][factory]") {
    auto provider = CreateKeychainProvider();

    // On non-macOS platforms, GetPassword should always return false
    // and never modify the output string.
    std::string result = "untouched";
    bool found = provider->GetPassword("any-service", "any-account", result);

    // Either it found the password (macOS with a real Keychain entry) or
    // it didn't.  In CI, we expect false because there is no pre-seeded
    // Keychain entry.  The important invariant is that the call does not
    // crash or throw.
    if (!found) {
        REQUIRE(result == "untouched");
    }
    // If found is true (macOS with a matching Keychain entry), the result
    // should be set but we can't assert on its value.
}

// ==========================================================================
//  Section 3: SignCommandForJob — keychain integration
// ==========================================================================

namespace {

/** @brief Creates a minimal BuildPipelineDraft with signing enabled for Windows. */
BuildPipelineDraft MakeWindowsSigningDraft() {
    BuildPipelineDraft draft;
    draft.signing.enabled = true;
    draft.signing.certificatePath = "/path/to/cert.pfx";
    draft.signing.certificatePassword = Core::SecureString("stored-pwd");
    return draft;
}

/** @brief Creates a minimal BuildJob targeting Windows. */
BuildJob MakeWindowsBuildJob(const std::string &outputPath = "build/output") {
    BuildJob job;
    job.os = BuildTargetOS::Windows;
    job.arch = BuildArch::x86_64;
    job.config = BuildConfig::Release;
    job.outputPath = outputPath;
    return job;
}

} // anonymous namespace

TEST_CASE("SignCommandForJob: uses stored password when keychain is null",
          "[keychain][signing]") {
    auto draft = MakeWindowsSigningDraft();
    auto job = MakeWindowsBuildJob("C:\\build\\win_out");

    // Null keychain — should fall back to stored password.
    std::string cmd = SignCommandForJob(draft, job, nullptr);

    REQUIRE_FALSE(cmd.empty());
    // The stored password "stored-pwd" should appear in the command.
    REQUIRE(cmd.find("stored-pwd") != std::string::npos);
}

TEST_CASE("SignCommandForJob: uses stored password when useKeychainForPassword is false",
          "[keychain][signing]") {
    auto draft = MakeWindowsSigningDraft();
    auto job = MakeWindowsBuildJob();

    MockKeychainProvider mock;
    mock.SetPassword("com.horo.signing", "codesign-cert", "keychain-pwd");
    draft.signing.keychainService = "com.horo.signing";
    draft.signing.keychainAccount = "codesign-cert";
    // useKeychainForPassword defaults to false.

    std::string cmd = SignCommandForJob(draft, job, &mock);

    REQUIRE_FALSE(cmd.empty());
    // Should still use stored-pwd because useKeychainForPassword is false.
    REQUIRE(cmd.find("stored-pwd") != std::string::npos);
    REQUIRE(cmd.find("keychain-pwd") == std::string::npos);
}

TEST_CASE("SignCommandForJob: uses keychain password when useKeychainForPassword is true",
          "[keychain][signing]") {
    auto draft = MakeWindowsSigningDraft();
    auto job = MakeWindowsBuildJob();

    MockKeychainProvider mock;
    mock.SetPassword("com.horo.signing", "codesign-cert", "keychain-pwd");

    draft.signing.useKeychainForPassword = true;
    draft.signing.keychainService = "com.horo.signing";
    draft.signing.keychainAccount = "codesign-cert";

    std::string cmd = SignCommandForJob(draft, job, &mock);

    REQUIRE_FALSE(cmd.empty());
    // Should use keychain password, not stored password.
    REQUIRE(cmd.find("keychain-pwd") != std::string::npos);
    REQUIRE(cmd.find("stored-pwd") == std::string::npos);
}

TEST_CASE("SignCommandForJob: falls back to stored password when keychain lookup fails",
          "[keychain][signing]") {
    auto draft = MakeWindowsSigningDraft();
    auto job = MakeWindowsBuildJob();

    MockKeychainProvider mock;
    // Do NOT seed any passwords — lookup will fail.

    draft.signing.useKeychainForPassword = true;
    draft.signing.keychainService = "com.horo.signing";
    draft.signing.keychainAccount = "missing-cert";

    std::string cmd = SignCommandForJob(draft, job, &mock);

    REQUIRE_FALSE(cmd.empty());
    // Should fall back to stored password since keychain lookup failed.
    REQUIRE(cmd.find("stored-pwd") != std::string::npos);
}

TEST_CASE("SignCommandForJob: falls back when keychain service is empty",
          "[keychain][signing]") {
    auto draft = MakeWindowsSigningDraft();
    auto job = MakeWindowsBuildJob();

    MockKeychainProvider mock;
    mock.SetPassword("com.horo.signing", "codesign-cert", "keychain-pwd");

    draft.signing.useKeychainForPassword = true;
    draft.signing.keychainService = ""; // empty service
    draft.signing.keychainAccount = "codesign-cert";

    std::string cmd = SignCommandForJob(draft, job, &mock);

    REQUIRE_FALSE(cmd.empty());
    // Should fall back — empty service disables keychain lookup.
    REQUIRE(cmd.find("stored-pwd") != std::string::npos);
}

TEST_CASE("SignCommandForJob: falls back when keychain account is empty",
          "[keychain][signing]") {
    auto draft = MakeWindowsSigningDraft();
    auto job = MakeWindowsBuildJob();

    MockKeychainProvider mock;
    mock.SetPassword("com.horo.signing", "codesign-cert", "keychain-pwd");

    draft.signing.useKeychainForPassword = true;
    draft.signing.keychainService = "com.horo.signing";
    draft.signing.keychainAccount = ""; // empty account

    std::string cmd = SignCommandForJob(draft, job, &mock);

    REQUIRE_FALSE(cmd.empty());
    // Should fall back — empty account disables keychain lookup.
    REQUIRE(cmd.find("stored-pwd") != std::string::npos);
}

TEST_CASE("SignCommandForJob: returns empty when signing is disabled",
          "[keychain][signing]") {
    BuildPipelineDraft draft;
    draft.signing.enabled = false;
    BuildJob job = MakeWindowsBuildJob();

    MockKeychainProvider mock;
    mock.SetPassword("svc", "acct", "pwd");
    draft.signing.useKeychainForPassword = true;
    draft.signing.keychainService = "svc";
    draft.signing.keychainAccount = "acct";

    std::string cmd = SignCommandForJob(draft, job, &mock);
    REQUIRE(cmd.empty());

    // Also test the no-keychain overload.
    cmd = SignCommandForJob(draft, job);
    REQUIRE(cmd.empty());
}

TEST_CASE("SignCommandForJob: macOS notarization is unaffected by keychain",
          "[keychain][signing][macos]") {
    BuildPipelineDraft draft;
    draft.signing.enabled = true;
    draft.signing.notarize = true;
    draft.signing.teamId = "ABC1234567";
    draft.signing.certificatePassword = Core::SecureString("ignored-for-macos");

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.arch = BuildArch::Arm64;
    job.config = BuildConfig::Release;
    job.outputPath = "/build/macos/MyApp.app";

    MockKeychainProvider mock;
    mock.SetPassword("svc", "acct", "should-not-appear");
    draft.signing.useKeychainForPassword = true;
    draft.signing.keychainService = "svc";
    draft.signing.keychainAccount = "acct";

    // macOS codesign command uses the team ID, not the certificate password.
    // The keychain password should NOT appear in the output.
    std::string cmd = SignCommandForJob(draft, job, &mock);

    REQUIRE_FALSE(cmd.empty());
    REQUIRE(cmd.find("Developer ID Application: ABC1234567") != std::string::npos);
    REQUIRE(cmd.find("should-not-appear") == std::string::npos);
    REQUIRE(cmd.find("stored-pwd") == std::string::npos); // macOS never uses the password
}

TEST_CASE("SignCommandForJob: stored-password overload is backward compatible",
          "[keychain][signing][compat]") {
    auto draft = MakeWindowsSigningDraft();
    auto job = MakeWindowsBuildJob();

    // The original overload (no keychain) should still work.
    std::string cmd = SignCommandForJob(draft, job);

    REQUIRE_FALSE(cmd.empty());
    REQUIRE(cmd.find("stored-pwd") != std::string::npos);
}

// ==========================================================================
//  Section 4: Edge cases
// ==========================================================================

TEST_CASE("KeychainBridge: service/account with special characters",
          "[keychain][mock][edge]") {
    MockKeychainProvider mock;

    SECTION("Unicode characters") {
        mock.SetPassword("café-service", "naïve-account", "Sésame");
        std::string result;
        REQUIRE(mock.GetPassword("café-service", "naïve-account", result));
        REQUIRE(result == "Sésame");
    }

    SECTION("Long password (near SecureString capacity)") {
        std::string longPassword(500, 'x');
        mock.SetPassword("svc", "acct", longPassword);
        std::string result;
        REQUIRE(mock.GetPassword("svc", "acct", result));
        REQUIRE(result == longPassword);
        REQUIRE(result.size() == 500);
    }

    SECTION("Password with null bytes") {
        std::string withNulls = std::string("pre") + '\0' + std::string("post");
        mock.SetPassword("svc", "acct", withNulls);
        std::string result;
        REQUIRE(mock.GetPassword("svc", "acct", result));
        // Embedded nulls are preserved.
        REQUIRE(result.size() == 8); // "pre\0post" = 8 bytes
    }
}
