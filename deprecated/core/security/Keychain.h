/** @file Keychain.h
 *  @brief Platform credential-store abstraction with a mock for testing.
 *
 *  IKeychain defines a narrow interface for storing and retrieving secrets
 *  via OS-level credential managers:
 *  - macOS:   Keychain Services (SecKeychain*)
 *  - Windows: DPAPI / Credential Manager (CredRead/CredWrite)
 *  - Linux:   (future) libsecret / D-Bus Secret Service
 *
 *  The MockKeychain stores credentials in an in-memory map and is suitable
 *  for unit tests and offline development.  Real platform backends are
 *  declared here but may be implemented in platform-specific .cpp files.
 *
 *  Design constraints:
 *  - No heap allocation in hot paths — retrieval allocates a SecureString
 *    but that's a credential-load path, not per-frame.
 *  - Secrets are always returned as SecureString to guarantee wipe semantics.
 *  - Interface is intentionally minimal: Store, Retrieve, Delete, Exists.
 */
#pragma once

#include "core/security/SecureString.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Horo::Security {

/** @brief Service name used as the Keychain "where" discriminator.
 *
 *  Convention: "com.horoengine.<subsystem>" (e.g. "com.horoengine.launcher").
 *  This maps to kSecAttrService on macOS and the target name on Windows. */
using KeychainService = std::string;

/** @brief Account name — the "who" discriminator.
 *
 *  Typically a user-visible label like "default" or "abdullah@horo".
 *  Maps to kSecAttrAccount on macOS and the user name on Windows. */
using KeychainAccount = std::string;

// ═══════════════════════════════════════════════════════════════════════════
//  Abstract interface
// ═══════════════════════════════════════════════════════════════════════════

/** @brief Abstract platform credential store.
 *
 *  Implementations map to OS-level secure storage.  For testing and
 *  offline development, use MockKeychain. */
class IKeychain {
public:
    virtual ~IKeychain() = default;

    /** @brief Persists a secret for the given service + account.
     *
     *  If a secret already exists for this pair it is overwritten.
     *  @param service  Identifies the application / subsystem.
     *  @param account  Identifies the user or purpose.
     *  @param secret   The secret to store (ownership transferred to store).
     *  @return True if the secret was stored successfully. */
    virtual bool Store(KeychainService service,
                       KeychainAccount account,
                       SecureString secret) = 0;

    /** @brief Retrieves a previously stored secret.
     *
     *  @return The secret, or std::nullopt if no entry exists or an error
     *          occurred. */
    virtual std::optional<SecureString> Retrieve(
        const KeychainService& service,
        const KeychainAccount& account) = 0;

    /** @brief Removes the secret for the given service + account.
     *
     *  @return True if the entry was deleted or did not exist.
     *          False on platform error only. */
    virtual bool Delete(const KeychainService& service,
                        const KeychainAccount& account) = 0;

    /** @brief Returns true if a secret exists for this service + account. */
    virtual bool Exists(const KeychainService& service,
                        const KeychainAccount& account) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
//  MockKeychain — in-memory store for testing
// ═══════════════════════════════════════════════════════════════════════════

/** @brief In-memory credential store for unit testing.
 *
 *  Stores secrets in a std::unordered_map.  No encryption — secrets are
 *  held in plaintext (in SecureString wrappers).  Intended exclusively
 *  for test environments; never use this in production. */
class MockKeychain : public IKeychain {
public:
    bool Store(KeychainService service,
               KeychainAccount account,
               SecureString secret) override {
        auto key = MakeKey(service, account);
        m_store[std::string(key)] = std::move(secret);
        return true;
    }

    std::optional<SecureString> Retrieve(
        const KeychainService& service,
        const KeychainAccount& account) override {
        auto key = MakeKey(service, account);
        auto it = m_store.find(std::string(key));
        if (it == m_store.end()) return std::nullopt;

        // Return a copy — SecureString is move-only, so we create
        // a new one from the stored value's view.
        return SecureString::FromView(it->second.view());
    }

    bool Delete(const KeychainService& service,
                const KeychainAccount& account) override {
        auto key = MakeKey(service, account);
        auto it = m_store.find(std::string(key));
        if (it != m_store.end()) {
            // SecureString destructor wipes the value
            m_store.erase(it);
        }
        return true;
    }

    bool Exists(const KeychainService& service,
                const KeychainAccount& account) override {
        auto key = MakeKey(service, account);
        return m_store.find(std::string(key)) != m_store.end();
    }

    /** @brief Returns the number of stored entries (for test assertions). */
    size_t Count() const { return m_store.size(); }

    /** @brief Removes all entries. */
    void Clear() { m_store.clear(); }

private:
    static std::string MakeKey(const KeychainService& svc,
                                const KeychainAccount& acct) {
        std::string key;
        key.reserve(svc.size() + 1 + acct.size());
        key.append(svc);
        key.push_back(':');
        key.append(acct);
        return key;
    }

    std::unordered_map<std::string, SecureString> m_store;
};

// ═══════════════════════════════════════════════════════════════════════════
//  Platform-specific declarations
// ═══════════════════════════════════════════════════════════════════════════

#if defined(__APPLE__)
/** @brief macOS Keychain Services backend.
 *
 *  Uses SecKeychain* APIs.  Requires the Keychain Access entitlement
 *  in sandboxed builds.  Falls back gracefully when the keychain is
 *  locked or unavailable. */
class MacKeychain : public IKeychain {
public:
    MacKeychain();
    ~MacKeychain() override;

    bool Store(KeychainService service,
               KeychainAccount account,
               SecureString secret) override;
    std::optional<SecureString> Retrieve(
        const KeychainService& service,
        const KeychainAccount& account) override;
    bool Delete(const KeychainService& service,
                const KeychainAccount& account) override;
    bool Exists(const KeychainService& service,
                const KeychainAccount& account) override;

    /** @brief Returns a human-readable error from the last operation. */
    std::string LastError() const { return m_lastError; }

private:
    void* m_keychain = nullptr; // SecKeychainRef (opaque)
    std::string m_lastError;
};
#endif // __APPLE__

#if defined(_WIN32)
/** @brief Windows DPAPI / Credential Manager backend.
 *
 *  Uses CredReadW / CredWriteW with CRED_TYPE_GENERIC.  Credentials are
 *  encrypted with the current user's DPAPI master key. */
class WinDPAPI : public IKeychain {
public:
    WinDPAPI();
    ~WinDPAPI() override;

    bool Store(KeychainService service,
               KeychainAccount account,
               SecureString secret) override;
    std::optional<SecureString> Retrieve(
        const KeychainService& service,
        const KeychainAccount& account) override;
    bool Delete(const KeychainService& service,
                const KeychainAccount& account) override;
    bool Exists(const KeychainService& service,
                const KeychainAccount& account) override;

    std::string LastError() const { return m_lastError; }

private:
    std::string m_lastError;
};
#endif // _WIN32

} // namespace Horo::Security
