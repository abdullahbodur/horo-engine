/** @file HashVerifier.cpp
 *  @brief CRC32 and SHA-256 hash implementations for .horo archive integrity. */
#include "core/archive/HashVerifier.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace Horo::Archive {

// ============================================================================
// CRC32 — standard Ethernet/gzip polynomial (reflected)
// ============================================================================

namespace {

/** @brief Precomputed CRC32 lookup table (polynomial 0xEDB88320). */
constexpr uint32_t kCrc32Table[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u,
    0x706AF48Fu, 0xE963A535u, 0x9E6495A3u, 0x0EDB8832u, 0x79DCB8A4u,
    0xE0D5E91Eu, 0x97D2D988u, 0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u,
    0x90BF1D91u, 0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu,
    0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u, 0x136C9856u,
    0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u,
    0xFA0F3D63u, 0x8D080DF5u, 0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u,
    0xA2677172u, 0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
    0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u,
    0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u, 0x26D930ACu, 0x51DE003Au,
    0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u,
    0xB8BDA50Fu, 0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
    0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du, 0x76DC4190u,
    0x01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu,
    0x9FBFE4A5u, 0xE8B8D433u, 0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu,
    0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
    0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu, 0x6C0695EDu,
    0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u, 0x65B0D9C6u, 0x12B7E950u,
    0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u,
    0xFBD44C65u, 0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u,
    0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu, 0x4369E96Au,
    0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u, 0x44042D73u, 0x33031DE5u,
    0xAA0A4C5Fu, 0xDD0D7CC9u, 0x5005713Cu, 0x270241AAu, 0xBE0B1010u,
    0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u,
    0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu, 0xEDB88320u, 0x9ABFB3B6u,
    0x03B6E20Cu, 0x74B1D29Au, 0xEAD54739u, 0x9DD277AFu, 0x04DB2615u,
    0x73DC1683u, 0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
    0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u, 0xF00F9344u,
    0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu,
    0x196C3671u, 0x6E6B06E7u, 0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au,
    0x67DD4ACCu, 0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
    0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u,
    0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu, 0xD80D2BDAu, 0xAF0A1B4Cu,
    0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu,
    0x4669BE79u, 0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
    0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu, 0xC5BA3BBEu,
    0xB2BD0B28u, 0x2BB45A92u, 0x5CB30A04u, 0xC2D7FFA7u, 0xB5D0CF31u,
    0x2CD99E8Bu, 0x5BDEAE1Du, 0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu,
    0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
    0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u, 0x92D28E9Bu,
    0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u, 0x86D3D2D4u, 0xF1D4E242u,
    0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u,
    0x18B74777u, 0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu,
    0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u, 0xA00AE278u,
    0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u, 0xA7672661u, 0xD06016F7u,
    0x4969474Du, 0x3E6E77DBu, 0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u,
    0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u,
    0xCDD70693u, 0x54DE5729u, 0x23D967BFu, 0xB3667A2Eu, 0xC4614AB8u,
    0x5D681B02u, 0x2A6F2B94u, 0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu,
    0x2D02EF8Du
};

} // anonymous namespace

/** @copydoc ComputeCRC32 */
uint32_t ComputeCRC32(const uint8_t* data, size_t length) noexcept {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t index = static_cast<uint8_t>(crc ^ data[i]);
        crc = (crc >> 8) ^ kCrc32Table[index];
    }
    return crc ^ 0xFFFFFFFFu;
}

// ============================================================================
// SHA-256 — FIPS 180-4
// ============================================================================

namespace {

// Initial hash values (first 32 bits of the fractional parts of the
// square roots of the first 8 primes 2..19).
constexpr uint32_t kSha256Init[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
};

// Round constants (first 32 bits of the fractional parts of the cube
// roots of the first 64 primes 2..311).
constexpr uint32_t kSha256K[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

// SHA-256 helper functions.

inline uint32_t RotR(uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t Sigma0(uint32_t x) noexcept {
    return RotR(x, 2) ^ RotR(x, 13) ^ RotR(x, 22);
}

inline uint32_t Sigma1(uint32_t x) noexcept {
    return RotR(x, 6) ^ RotR(x, 11) ^ RotR(x, 25);
}

inline uint32_t sigma0(uint32_t x) noexcept {
    return RotR(x, 7) ^ RotR(x, 18) ^ (x >> 3);
}

inline uint32_t sigma1(uint32_t x) noexcept {
    return RotR(x, 17) ^ RotR(x, 19) ^ (x >> 10);
}

} // anonymous namespace

// ============================================================================
// Sha256Hasher
// ============================================================================

/** @copydoc Sha256Hasher::Sha256Hasher */
Sha256Hasher::Sha256Hasher() noexcept {
    Reset();
}

/** @copydoc Sha256Hasher::Reset */
void Sha256Hasher::Reset() noexcept {
    std::memcpy(m_state, kSha256Init, sizeof(m_state));
    m_bit_count   = 0;
    m_buffer_used = 0;
    std::memset(m_buffer, 0, sizeof(m_buffer));
}

/** @copydoc Sha256Hasher::Update */
void Sha256Hasher::Update(const uint8_t* data, size_t length) noexcept {
    if (length == 0) return;

    m_bit_count += static_cast<uint64_t>(length) * 8;

    // Fill the buffer if it already has partial data.
    if (m_buffer_used > 0) {
        const size_t space = 64 - m_buffer_used;
        const size_t copy  = std::min(length, space);
        std::memcpy(m_buffer + m_buffer_used, data, copy);
        m_buffer_used += copy;
        data          += copy;
        length        -= copy;

        if (m_buffer_used == 64) {
            ProcessBlock(m_buffer);
            m_buffer_used = 0;
        }
    }

    // Process full blocks directly from input.
    while (length >= 64) {
        ProcessBlock(data);
        data   += 64;
        length -= 64;
    }

    // Save remainder.
    if (length > 0) {
        std::memcpy(m_buffer, data, length);
        m_buffer_used = length;
    }
}

/** @copydoc Sha256Hasher::Finish */
void Sha256Hasher::Finish(uint8_t out_digest[kSha256Size]) noexcept {
    // Padding: append 0x80, then zeros, then 64-bit bit count (big-endian).
    m_buffer[m_buffer_used++] = 0x80;

    if (m_buffer_used > 56) {
        // Not enough room for the length — pad this block and start a new one.
        std::memset(m_buffer + m_buffer_used, 0, 64 - m_buffer_used);
        ProcessBlock(m_buffer);
        m_buffer_used = 0;
    }

    std::memset(m_buffer + m_buffer_used, 0, 56 - m_buffer_used);

    // Append bit count as 64-bit big-endian.
    const uint64_t bits = m_bit_count;
    for (int i = 0; i < 8; ++i) {
        m_buffer[56 + i] = static_cast<uint8_t>(bits >> (56 - i * 8));
    }

    ProcessBlock(m_buffer);

    // Emit digest as big-endian 32-bit words.
    for (int i = 0; i < 8; ++i) {
        out_digest[i * 4 + 0] = static_cast<uint8_t>(m_state[i] >> 24);
        out_digest[i * 4 + 1] = static_cast<uint8_t>(m_state[i] >> 16);
        out_digest[i * 4 + 2] = static_cast<uint8_t>(m_state[i] >> 8);
        out_digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i]);
    }
}

/** @brief Process one 64-byte block through the SHA-256 compression function. */
void Sha256Hasher::ProcessBlock(const uint8_t block[64]) noexcept {
    // Message schedule.
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4])     << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8)  |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
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
        const uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + kSha256K[i] + w[i];
        const uint32_t t2 = Sigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

// ============================================================================
// Convenience one-shot
// ============================================================================

/** @copydoc ComputeSHA256 */
void ComputeSHA256(const uint8_t* data, size_t length,
                   uint8_t out_digest[kSha256Size]) noexcept {
    Sha256Hasher hasher;
    hasher.Update(data, length);
    hasher.Finish(out_digest);
}

// ============================================================================
// File-level streaming hash
// ============================================================================

/** @copydoc ComputeFileSHA256 */
bool ComputeFileSHA256(const std::filesystem::path& file_path,
                       uint8_t out_digest[kSha256Size]) noexcept {
    // Open in binary mode, start at beginning.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file_path, ec) || ec) {
        return false;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // 64 KiB read buffer — fits in L1 cache comfortably on most CPUs.
    constexpr size_t kChunkSize = 64 * 1024;
    std::array<uint8_t, kChunkSize> buffer{};

    Sha256Hasher hasher;

    while (file.good()) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const auto bytes_read = static_cast<size_t>(file.gcount());
        if (bytes_read == 0) break;

        hasher.Update(buffer.data(), bytes_read);
    }

    // Distinguish EOF from a read error.
    if (file.bad()) {
        return false;
    }

    hasher.Finish(out_digest);
    return true;
}

} // namespace Horo::Archive
