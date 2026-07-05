/** @file AESContext.h
 *  @brief Platform-optimized AES-128 block cipher and AES-CTR stream cipher.
 *
 *  Provides a minimal, allocation-free AES-128 encryption context with
 *  compile-time backend selection:
 *  - macOS:       CommonCrypto (hardware AES via AES-NI / ARM Crypto Extensions)
 *  - Windows:     BCrypt CNG (hardware AES via AES-NI)
 *  - Linux/other: Software implementation (constant-time-style lookup table)
 *
 *  Also includes AES-CTR stream processing and PBKDF2-HMAC-SHA256 key
 *  derivation — no external crypto library required.
 *
 *  Reference: FIPS PUB 197 (AES), NIST SP 800-38A (CTR), RFC 8018 (PBKDF2).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

// Forward-declare platform types to keep the header lean.
// Full includes live in the .cpp to avoid polluting translation units.
#if defined(__APPLE__)
#include <CommonCrypto/CommonCryptor.h>
#endif

namespace Horo::Crypto {

// ── Constants ────────────────────────────────────────────────────────────

/** @brief AES block size in bytes (always 16). */
inline constexpr uint32_t kAesBlockSize = 16;

/** @brief AES-128 key size in bytes. */
inline constexpr uint32_t kAes128KeySize = 16;

/** @brief Key Check Value size in bytes (first 8 bytes of an encrypted zero block). */
inline constexpr uint32_t kKcvSize = 8;

/** @brief Minimum PBKDF2 iteration count for production use. */
inline constexpr uint32_t kMinPbkdf2Iterations = 100'000;

/** @brief SHA-256 output size in bytes. */
inline constexpr uint32_t kSha256DigestSize = 32;

/** @brief Maximum derived key length supported by PBKDF2-HMAC-SHA256. */
inline constexpr uint32_t kMaxPbkdf2KeySize = 32;

// ── AES Context ──────────────────────────────────────────────────────────

/**
 * @brief Platform-optimized AES-128 block cipher context.
 *
 * Once initialized with a 128-bit key, the context can encrypt individual
 * 16-byte blocks. CTR mode is built on top: callers manage their own
 * counter block and XOR the resulting keystream via AesCtrProcess().
 *
 * The context is move-only — platform resources (CCCryptorRef, BCrypt
 * handles) are not trivially copyable. Moves transfer ownership and
 * leave the source uninitialized.
 *
 * Thread safety: EncryptBlock() is re-entrant but the context itself is
 * not thread-safe (no internal mutex). Use one context per thread.
 */
class AESContext {
public:
    /** @brief Creates an uninitialized context — Init() must be called. */
    AESContext();

    /** @brief Releases platform resources. */
    ~AESContext();

    // Non-copyable
    AESContext(const AESContext&) = delete;
    AESContext& operator=(const AESContext&) = delete;

    // Movable
    AESContext(AESContext&& other) noexcept;
    AESContext& operator=(AESContext&& other) noexcept;

    /**
     * @brief Initializes the context with a 128-bit AES key.
     * @param key 16-byte buffer; must not be null.
     * @return True on success. May fail if the platform crypto provider
     *         is unavailable (extremely rare).
     */
    bool Init(const uint8_t key[16]);

    /** @brief Returns true after a successful Init() call. */
    bool IsInitialized() const { return m_initialized; }

    /**
     * @brief Encrypts a single 16-byte block in-place.
     *
     * In CTR mode, this is used to encrypt counter blocks to produce
     * keystream — decryption uses the same forward cipher.
     *
     * @pre IsInitialized() == true
     * @param block 16-byte buffer, encrypted in-place.
     */
    void EncryptBlock(uint8_t block[16]) const;

    /**
     * @brief Generates the Key Check Value for password verification.
     *
     * Encrypts a 16-byte zero block and writes the first 8 bytes.
     * Same key always produces the same KCV.
     *
     * @pre IsInitialized() == true
     * @param kcvOut Buffer of at least kKcvSize (8) bytes.
     */
    void GenerateKcv(uint8_t kcvOut[kKcvSize]) const;

private:
    void Destroy();

#if defined(__APPLE__)
    CCCryptorRef m_cryptor = nullptr;
#elif defined(_WIN32)
    void* m_algHandle = nullptr;   // BCRYPT_ALG_HANDLE
    void* m_keyHandle = nullptr;   // BCRYPT_KEY_HANDLE
#else
    /** @brief Expanded round keys: 11 rounds × 16 bytes = 176 bytes. */
    std::array<uint8_t, 176> m_roundKeys{};
#endif

    bool m_initialized = false;
};

// ── CTR Mode ─────────────────────────────────────────────────────────────

/**
 * @brief Processes data through AES-CTR keystream.
 *
 * Encrypts the current counter block to produce 16 bytes of keystream,
 * XORs them with up to 16 bytes of data, then increments the counter
 * (big-endian, last 8 bytes).
 *
 * This is intentionally a free function rather than a method of AESContext:
 * the context is purely a block cipher — callers own the counter state,
 * nonce management, and buffer layout.
 *
 * @param ctx       Initialized AES context (precondition: IsInitialized()).
 * @param counter   16-byte counter block, modified in-place on each call.
 *                  Format: first 8 bytes = nonce, last 8 bytes = big-endian counter.
 * @param data      Input/output buffer.
 * @param size      Number of bytes to process, must be ≤ kAesBlockSize.
 */
void AesCtrProcess(const AESContext& ctx,
                   uint8_t counter[kAesBlockSize],
                   uint8_t* data,
                   uint32_t size);

// ── PBKDF2 Key Derivation ────────────────────────────────────────────────

/**
 * @brief Derives a cryptographic key from a password using PBKDF2-HMAC-SHA256.
 *
 * Thread-safe (no shared mutable state).
 *
 * @param password      Password string.
 * @param passwordLen   Length of password in bytes.
 * @param salt          Salt bytes (should be random, unique per key).
 * @param saltLen       Length of salt in bytes.
 * @param iterations    Iteration count (recommended ≥ kMinPbkdf2Iterations).
 * @param outKey        Output buffer for the derived key.
 * @param outKeyLen     Desired key length in bytes (≤ kMaxPbkdf2KeySize).
 * @return True on success.
 */
bool DeriveKeyPbkdf2(const char* password, size_t passwordLen,
                     const uint8_t* salt, size_t saltLen,
                     uint32_t iterations,
                     uint8_t* outKey, size_t outKeyLen);

} // namespace Horo::Crypto
