/** @file AESContext.cpp
 *  @brief Platform-optimized AES-128 block cipher, CTR mode, and PBKDF2.
 *
 *  Backend dispatch is compile-time:
 *  - __APPLE__:   CommonCrypto (hardware AES via AES-NI / ARM CE)
 *  - _WIN32:      BCrypt CNG (hardware AES via AES-NI)
 *  - default:     Software AES-128 with pre-computed lookup tables
 *
 *  PBKDF2-HMAC-SHA256 uses the platform HMAC/SHA-256 when available and
 *  a self-contained software implementation as fallback.
 */
#include "core/crypto/AESContext.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <span>
#include <ranges>

#if defined(__APPLE__)
#include <CommonCrypto/CommonCryptoError.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#ifndef BCRYPT_PBKDF2_ALGORITHM
#define BCRYPT_PBKDF2_ALGORITHM L"PBKDF2"
#endif
#endif

namespace Horo::Crypto {

namespace {

/** @brief Clears a fixed-size byte buffer through volatile stores so compilers cannot elide the wipe. */
template <typename T, size_t Size>
void SecureZeroMemory(std::array<T, Size>& buffer) noexcept {
    volatile uint8_t *bytes = reinterpret_cast<volatile uint8_t*>(buffer.data());
    for (size_t i = 0; i < Size * sizeof(T); ++i)
        bytes[i] = 0;
}

template <size_t Size>
void SecureZeroMemory(uint8_t (&buffer)[Size]) noexcept {
    volatile uint8_t *bytes = buffer;
    for (size_t i = 0; i < Size; ++i)
        bytes[i] = 0;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  Section: AESContext — platform backends
// ═══════════════════════════════════════════════════════════════════════════

// ── macOS CommonCrypto backend ────────────────────────────────────────────
#if defined(__APPLE__)

AESContext::AESContext() = default;

AESContext::~AESContext() { Destroy(); }

AESContext::AESContext(AESContext&& other) noexcept
    : m_cryptor(other.m_cryptor)
    , m_initialized(other.m_initialized) {
    other.m_cryptor = nullptr;
    other.m_initialized = false;
}

AESContext& AESContext::operator=(AESContext&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_cryptor = other.m_cryptor;
        m_initialized = other.m_initialized;
        other.m_cryptor = nullptr;
        other.m_initialized = false;
    }
    return *this;
}

void AESContext::Destroy() {
    if (m_cryptor) {
        CCCryptorRelease(m_cryptor);
        m_cryptor = nullptr;
    }
    m_initialized = false;
}

bool AESContext::Init(const uint8_t key[16]) {
    Destroy();

    // Use ECB mode for block-level operations (CTR builds on top).
    // ccNoPadding: we handle padding ourselves in CTR mode.
    CCCryptorStatus status = CCCryptorCreateWithMode(
        kCCEncrypt, kCCModeECB, kCCAlgorithmAES,
        ccNoPadding,
        nullptr,                // IV not used in ECB
        key, kCCKeySizeAES128,
        nullptr, 0, 0,           // tweak not used
        0,                       // no options
        &m_cryptor);

    m_initialized = (status == kCCSuccess);
    return m_initialized;
}

void AESContext::EncryptBlock(uint8_t block[16]) const {
    size_t moved = 0;
    CCCryptorUpdate(m_cryptor, block, kAesBlockSize,
                    block, kAesBlockSize, &moved);
}

void AESContext::GenerateKcv(uint8_t kcvOut[kKcvSize]) const {
    std::array<uint8_t, kAesBlockSize> zeroBlock{};
    EncryptBlock(zeroBlock.data());
    std::memcpy(kcvOut, zeroBlock.data(), kKcvSize);
}

// ── Windows BCrypt backend ────────────────────────────────────────────────
#elif defined(_WIN32)

AESContext::AESContext() = default;

AESContext::~AESContext() { Destroy(); }

AESContext::AESContext(AESContext&& other) noexcept
    : m_algHandle(other.m_algHandle)
    , m_keyHandle(other.m_keyHandle)
    , m_initialized(other.m_initialized) {
    other.m_algHandle = nullptr;
    other.m_keyHandle = nullptr;
    other.m_initialized = false;
}

AESContext& AESContext::operator=(AESContext&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_algHandle = other.m_algHandle;
        m_keyHandle = other.m_keyHandle;
        m_initialized = other.m_initialized;
        other.m_algHandle = nullptr;
        other.m_keyHandle = nullptr;
        other.m_initialized = false;
    }
    return *this;
}

void AESContext::Destroy() {
    if (m_keyHandle) {
        BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(m_keyHandle));
        m_keyHandle = nullptr;
    }
    if (m_algHandle) {
        BCryptCloseAlgorithmProvider(
            static_cast<BCRYPT_ALG_HANDLE>(m_algHandle), 0);
        m_algHandle = nullptr;
    }
    m_initialized = false;
}

bool AESContext::Init(const uint8_t key[16]) {
    Destroy();

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        static_cast<BCRYPT_ALG_HANDLE*>(&m_algHandle),
        BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status))
        return false;

    // Set ECB chaining mode (BCrypt defaults to something else on some versions).
    status = BCryptSetProperty(
        static_cast<BCRYPT_ALG_HANDLE>(m_algHandle),
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
        static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_ECB)), 0);

    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(
            static_cast<BCRYPT_ALG_HANDLE>(m_algHandle), 0);
        m_algHandle = nullptr;
        return false;
    }

    status = BCryptGenerateSymmetricKey(
        static_cast<BCRYPT_ALG_HANDLE>(m_algHandle),
        static_cast<BCRYPT_KEY_HANDLE*>(&m_keyHandle),
        nullptr, 0,
        const_cast<PUCHAR>(key), kAes128KeySize, 0);

    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(
            static_cast<BCRYPT_ALG_HANDLE>(m_algHandle), 0);
        m_algHandle = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

void AESContext::EncryptBlock(uint8_t block[16]) const {
    ULONG outLen = 0;
    BCryptEncrypt(static_cast<BCRYPT_KEY_HANDLE>(m_keyHandle),
                  block, kAesBlockSize,
                  nullptr, nullptr, 0,    // no padding info
                  block, kAesBlockSize,
                  &outLen, 0);
}

void AESContext::GenerateKcv(uint8_t kcvOut[kKcvSize]) const {
    std::array<uint8_t, kAesBlockSize> zeroBlock{};
    EncryptBlock(zeroBlock.data());
    std::memcpy(kcvOut, zeroBlock.data(), kKcvSize);
}

// ── Software AES-128 backend ──────────────────────────────────────────────
#else

// NOLINTBEGIN(readability-magic-numbers)

namespace {

/** @brief AES S-box (substitution table, FIPS 197). */
constexpr std::array<uint8_t, 256> kSbox = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16,
};

/** @brief AES round constants for key expansion. */
constexpr std::array<uint8_t, 11> kRcon = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
};

/** @brief Galois field multiply by 2 (used in MixColumns). */
inline uint8_t GfMul2(uint8_t v) {
    return static_cast<uint8_t>((v << 1) ^ ((v & 0x80) ? 0x1b : 0x00)); // NOSONAR
}

/** @brief AES-128 key expansion: 16-byte key → 176-byte round keys. */
void KeyExpansion(const uint8_t key[16], uint8_t roundKeys[176]) {
    std::memcpy(roundKeys, key, 16);

    std::array<uint8_t, 4> temp{};
    for (size_t word = 4; word < 44; ++word) {
        const size_t previousWordOffset = (word - 1) * temp.size();
        std::copy_n(roundKeys + previousWordOffset, temp.size(), temp.begin());

        if (word % 4 == 0) {
            std::ranges::rotate(temp, temp.begin() + 1);
            for (uint8_t &value : temp)
                value = kSbox[value];
            temp[0] ^= kRcon[word / 4]; // NOSONAR
        }

        const size_t sourceWordOffset = (word - 4) * temp.size();
        const size_t outputWordOffset = word * temp.size();
        for (size_t byte = 0; byte < temp.size(); ++byte)
            roundKeys[outputWordOffset + byte] =
                roundKeys[sourceWordOffset + byte] ^ temp[byte]; // NOSONAR
    }
}

} // namespace

AESContext::AESContext() { m_roundKeys.fill(0); }

AESContext::~AESContext() { Destroy(); }

AESContext::AESContext(AESContext&& other) noexcept
    : m_initialized(other.m_initialized) {
    m_roundKeys = other.m_roundKeys;
    other.m_roundKeys.fill(0);
    other.m_initialized = false;
}

AESContext& AESContext::operator=(AESContext&& other) noexcept {
    if (this != &other) {
        m_roundKeys = other.m_roundKeys;
        m_initialized = other.m_initialized;
        other.m_roundKeys.fill(0);
        other.m_initialized = false;
    }
    return *this;
}

void AESContext::Destroy() {
    m_roundKeys.fill(0);
    m_initialized = false;
}

bool AESContext::Init(const uint8_t key[16]) {
    KeyExpansion(key, m_roundKeys.data());
    m_initialized = true;
    return true;
}

void AESContext::EncryptBlock(uint8_t block[16]) const {
    // Round 0: AddRoundKey
    for (int i = 0; i < 16; ++i)
        block[i] ^= m_roundKeys[i]; // NOSONAR

    // Rounds 1-9: SubBytes, ShiftRows, MixColumns, AddRoundKey
    for (int round = 1; round < 10; ++round) {
        for (int i = 0; i < 16; ++i)
            block[i] = kSbox[block[i]];

        // ShiftRows (row 0 stays, rows 1/2/3 rotate)
        uint8_t tmp = block[1];
        block[1] = block[5]; block[5] = block[9];
        block[9] = block[13]; block[13] = tmp;
        tmp = block[2]; block[2] = block[10]; block[10] = tmp;
        tmp = block[6]; block[6] = block[14]; block[14] = tmp;
        tmp = block[3]; block[3] = block[15]; block[15] = block[11];
        block[11] = block[7]; block[7] = tmp;

        // MixColumns per column
        for (int col = 0; col < 4; ++col) {
            const int idx = col * 4;
            const uint8_t a0 = block[idx];
            const uint8_t a1 = block[idx + 1];
            const uint8_t a2 = block[idx + 2];
            const uint8_t a3 = block[idx + 3];
            const uint8_t mixed = a0 ^ a1 ^ a2 ^ a3;
            block[idx] = a0 ^ mixed ^ GfMul2(a0 ^ a1); // NOSONAR
            block[idx + 1] = a1 ^ mixed ^ GfMul2(a1 ^ a2); // NOSONAR
            block[idx + 2] = a2 ^ mixed ^ GfMul2(a2 ^ a3); // NOSONAR
            block[idx + 3] = a3 ^ mixed ^ GfMul2(a3 ^ a0); // NOSONAR
        }

        // AddRoundKey
        const uint8_t *rk = m_roundKeys.data() + round * 16;
        for (int i = 0; i < 16; ++i)
            block[i] ^= rk[i]; // NOSONAR
    }

    // Final round (no MixColumns): SubBytes, ShiftRows, AddRoundKey
    for (int i = 0; i < 16; ++i)
        block[i] = kSbox[block[i]];

    uint8_t tmp = block[1];
    block[1] = block[5]; block[5] = block[9];
    block[9] = block[13]; block[13] = tmp;
    tmp = block[2]; block[2] = block[10]; block[10] = tmp;
    tmp = block[6]; block[6] = block[14]; block[14] = tmp;
    tmp = block[3]; block[3] = block[15]; block[15] = block[11];
    block[11] = block[7]; block[7] = tmp;

    const uint8_t *rk = m_roundKeys.data() + 160; // round 10
    for (int i = 0; i < 16; ++i)
        block[i] ^= rk[i]; // NOSONAR
}

void AESContext::GenerateKcv(uint8_t kcvOut[kKcvSize]) const {
    std::array<uint8_t, kAesBlockSize> zeroBlock{};
    EncryptBlock(zeroBlock.data());
    std::memcpy(kcvOut, zeroBlock.data(), kKcvSize);
}

// NOLINTEND(readability-magic-numbers)

#endif // Platform backends

// ═══════════════════════════════════════════════════════════════════════════
//  Section: AesCtrProcess — CTR mode keystream
// ═══════════════════════════════════════════════════════════════════════════

void AesCtrProcess(const AESContext& ctx,
                   uint8_t counter[kAesBlockSize],
                   uint8_t* data,
                   uint32_t size) {
    // Encrypt the counter block to produce keystream
    std::array<uint8_t, kAesBlockSize> keystream{};
    std::memcpy(keystream.data(), counter, kAesBlockSize);
    ctx.EncryptBlock(keystream.data());

    // XOR keystream with data
    for (uint32_t i = 0; i < size; ++i)
        data[i] ^= keystream[i]; // NOSONAR

    // Increment counter (big-endian, last 8 bytes)
    for (int j = kAesBlockSize - 1; j >= 8; --j) {
        if (++counter[j] != 0)
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section: SHA-256 — software implementation (FIPS 180-4)
// ═══════════════════════════════════════════════════════════════════════════
//
// Used as the PRF for PBKDF2 on platforms that lack CommonCrypto/BCrypt
// HMAC. The implementation is self-contained and constant-time-ish (no
// secret-dependent branching on the input data).

#if !defined(__APPLE__) && !defined(_WIN32)
// NOLINTBEGIN(readability-magic-numbers)

namespace {

/** @brief SHA-256 initial hash values (first 32 bits of fractional parts
 *         of the square roots of the first 8 primes). */
constexpr std::array<uint32_t, 8> kSha256Init = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

/** @brief SHA-256 round constants (first 32 bits of fractional parts
 *         of the cube roots of the first 64 primes). */
constexpr std::array<uint32_t, 64> kSha256K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t RotR(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

/**
 * @brief Software SHA-256 context.
 *
 * Processes data via Update() and finalizes with Finish(). Supports
 * the full streaming API needed for HMAC and PBKDF2.
 */
class Sha256 {
public:
    Sha256() { Reset(); }

    void Reset() {
        m_state[0] = kSha256Init[0];
        m_state[1] = kSha256Init[1];
        m_state[2] = kSha256Init[2];
        m_state[3] = kSha256Init[3];
        m_state[4] = kSha256Init[4];
        m_state[5] = kSha256Init[5];
        m_state[6] = kSha256Init[6];
        m_state[7] = kSha256Init[7];
        m_bitCount = 0;
        m_bufLen = 0;
    }

    /** @brief Feeds data into the hash. */
    void Update(std::span<const uint8_t> data) {
        for (uint8_t byte : data) {
            m_buffer[m_bufLen++] = byte;
            if (m_bufLen == 64) {
                ProcessBlock(m_buffer);
                m_bitCount += 512;
                m_bufLen = 0;
            }
        }
    }

    /** @brief Finalizes the hash and writes the 32-byte digest. */
    void Finish(uint8_t digest[kSha256DigestSize]) {
        m_bitCount += static_cast<uint64_t>(m_bufLen) * 8;

        // Padding: append 0x80, then zeros, then bit length
        m_buffer[m_bufLen++] = 0x80;
        if (m_bufLen > 56) {
            // Not enough room for length — pad and process this block
            while (m_bufLen < 64)
                m_buffer[m_bufLen++] = 0;
            ProcessBlock(m_buffer);
            m_bufLen = 0;
        }
        while (m_bufLen < 56)
            m_buffer[m_bufLen++] = 0;

        // Append bit length as big-endian 64-bit integer
        for (int i = 7; i >= 0; --i)
            m_buffer[m_bufLen++] = static_cast<uint8_t>(m_bitCount >> (i * 8));

        ProcessBlock(m_buffer);

        // Write digest in big-endian byte order
        for (int i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>(m_state[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(m_state[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(m_state[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i]);
        }
    }

private:
    /** @brief Processes a 64-byte block. */
    void ProcessBlock(const std::array<uint8_t, 64>& block) {
        uint32_t w[64];

        // Prepare message schedule
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^
                                (w[i - 15] >> 3);
            const uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^
                                (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];

        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
            const uint32_t S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;

            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        m_state[0] += a; m_state[1] += b;
        m_state[2] += c; m_state[3] += d;
        m_state[4] += e; m_state[5] += f;
        m_state[6] += g; m_state[7] += h;
    }

    std::array<uint32_t, 8> m_state{};
    uint64_t m_bitCount = 0;
    std::array<uint8_t, 64> m_buffer{};
    uint32_t m_bufLen = 0;
};

/**
 * @brief HMAC-SHA256 implementation.
 *
 * HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))
 */
void HmacSha256(const uint8_t* key, size_t keyLen,
                const uint8_t* message, size_t messageLen,
                uint8_t mac[kSha256DigestSize]) {
    constexpr size_t kBlockSize = 64; // SHA-256 block size

    std::array<uint8_t, kBlockSize> keyBlock{};

    // If key is longer than block size, hash it first
    if (keyLen > kBlockSize) {
        Sha256 hasher;
        hasher.Update(std::span<const uint8_t>{key, keyLen});
        hasher.Finish(keyBlock.data());
    } else {
        std::memcpy(keyBlock.data(), key, keyLen);
    }

    // Inner hash: H((key ^ ipad) || message)
    std::array<uint8_t, kBlockSize> innerKey{};
    for (size_t i = 0; i < kBlockSize; ++i)
        innerKey[i] = keyBlock[i] ^ 0x36; // NOSONAR

    Sha256 inner;
    inner.Update(innerKey);
    inner.Update(std::span<const uint8_t>{message, messageLen});
    std::array<uint8_t, kSha256DigestSize> innerHash{};
    inner.Finish(innerHash.data());

    // Outer hash: H((key ^ opad) || innerHash)
    std::array<uint8_t, kBlockSize> outerKey{};
    for (size_t i = 0; i < kBlockSize; ++i)
        outerKey[i] = keyBlock[i] ^ 0x5c; // NOSONAR

    Sha256 outer;
    outer.Update(outerKey);
    outer.Update(innerHash);
    outer.Finish(mac);

    // Clear key material with non-elidable stores.
    SecureZeroMemory(keyBlock);
    SecureZeroMemory(innerKey);
    SecureZeroMemory(outerKey);
}

} // namespace
// NOLINTEND(readability-magic-numbers)
#endif // !__APPLE__ && !_WIN32

// ═══════════════════════════════════════════════════════════════════════════
//  Section: PBKDF2-HMAC-SHA256
// ═══════════════════════════════════════════════════════════════════════════

bool DeriveKeyPbkdf2(const char* password, size_t passwordLen,
                     const uint8_t* salt, size_t saltLen,
                     uint32_t iterations,
                     uint8_t* outKey, size_t outKeyLen) {
    if (!password || !salt || !outKey || outKeyLen == 0 ||
        outKeyLen > kMaxPbkdf2KeySize || iterations == 0)
        return false;

#if defined(__APPLE__)
    // ── macOS: CommonCrypto CCKeyDerivationPBKDF ──────────────────────────
    int result = CCKeyDerivationPBKDF(
        kCCPBKDF2,
        password,       passwordLen,
        salt,           saltLen,
        kCCPRFHmacAlgSHA256,
        iterations,
        outKey,         outKeyLen);

    return (result == kCCSuccess);
#elif defined(_WIN32)
    // ── Windows: BCryptDeriveKeyPBKDF2 ────────────────────────────────────
    //
    // Open an algorithm provider first; BCryptDeriveKeyPBKDF2 is a global
    // function that needs a handle to identify the PRF, but does not use
    // it for keyed state (PBKDF2 is deterministic, no per-key state).
    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &alg, BCRYPT_SHA256_ALGORITHM, nullptr,
        BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status))
        return false;

    status = BCryptDeriveKeyPBKDF2(
        alg,
        const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(password)),
        static_cast<ULONG>(passwordLen),
        const_cast<PUCHAR>(salt),
        static_cast<ULONG>(saltLen),
        static_cast<ULONGLONG>(iterations),
        outKey,
        static_cast<ULONG>(outKeyLen),
        0);

    BCryptCloseAlgorithmProvider(alg, 0);
    return BCRYPT_SUCCESS(status);
#else
    // ── Software PBKDF2-HMAC-SHA256 (RFC 8018) ────────────────────────────
    //
    // DK = T_1 || T_2 || ... || T_l
    // where T_i = U_1 ^ U_2 ^ ... ^ U_c
    //       U_1 = PRF(Password, Salt || INT_32_BE(i))
    //       U_j = PRF(Password, U_{j-1})

    const uint32_t hLen = kSha256DigestSize; // 32 bytes
    const uint32_t blockCount =
        static_cast<uint32_t>((outKeyLen + hLen - 1) / hLen);

    std::array<uint8_t, kSha256DigestSize> work{};
    std::array<uint8_t, kSha256DigestSize> uBlock{};

    std::vector<uint8_t> saltExt(saltLen + 4);

    for (uint32_t blockIdx = 1; blockIdx <= blockCount; ++blockIdx) {
        // Build salt || INT_32_BE(blockIdx).
        std::memcpy(saltExt.data(), salt, saltLen);
        saltExt[saltLen + 0] = static_cast<uint8_t>(blockIdx >> 24);
        saltExt[saltLen + 1] = static_cast<uint8_t>(blockIdx >> 16);
        saltExt[saltLen + 2] = static_cast<uint8_t>(blockIdx >> 8);
        saltExt[saltLen + 3] = static_cast<uint8_t>(blockIdx);

        // U_1 = HMAC-SHA256(password, salt || INT_32_BE(i))
        HmacSha256(reinterpret_cast<const uint8_t*>(password), passwordLen,
                   saltExt.data(), saltExt.size(), uBlock.data());
        std::memcpy(work.data(), uBlock.data(), hLen);

        // U_j = HMAC-SHA256(password, U_{j-1}) for j = 2..c
        for (uint32_t iter = 2; iter <= iterations; ++iter) {
            HmacSha256(reinterpret_cast<const uint8_t*>(password), passwordLen,
                       uBlock.data(), hLen, uBlock.data());
            for (uint32_t b = 0; b < hLen; ++b)
                work[b] ^= uBlock[b]; // NOSONAR
        }

        // Copy this block's output to the result
        const size_t copyLen =
            std::min<size_t>(hLen, outKeyLen - (blockIdx - 1) * hLen);
        std::memcpy(outKey + (blockIdx - 1) * hLen, work.data(), copyLen);
    }

    // Clear sensitive state with non-elidable stores.
    SecureZeroMemory(work);
    SecureZeroMemory(uBlock);
    return true;
#endif
}

} // namespace Horo::Crypto
