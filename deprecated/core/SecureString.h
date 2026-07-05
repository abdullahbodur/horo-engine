/** @file SecureString.h
 *  @brief Move-only secure string buffer with deterministic zeroization on
 *         destruction. Protects signing credentials, passwords, and key
 *         material from lingering in memory.
 *
 *  @note Limitations: This class does NOT use @c mlock / @c mprotect to
 *        prevent paging to swap, nor does it guard against core-dump
 *        extraction or cold-boot attacks. It provides defense-in-depth
 *        against accidental leakage via dangling heap/stack memory but is
 *        not a hardware security module replacement.
 */
#pragma once

#include <cstddef>
#include <string_view>

namespace Horo::Core {

/** @brief Move-only, fixed-capacity secure buffer that zeros memory on
 *         destruction and move-from.
 *
 *  Designed for signing keys, certificate passwords, and similar short-lived
 *  secrets. The fixed capacity avoids heap allocation entirely — secrets stay
 *  on the stack where normal RAII tear-down suffices.
 *
 *  Move semantics transfer ownership of the secret; the moved-from object is
 *  zeroed and set to an empty state. Copy construction and copy assignment are
 *  explicitly deleted.
 *
 *  @warning This is NOT a cryptographic-grade secure allocator. It does not
 *           prevent the OS from paging the buffer to disk or capturing it in a
 *           core dump. Do not use for long-lived key material or compliance-
 *           grade secret storage.
 */
class SecureString {
public:
    /** @brief Maximum number of characters (excluding null terminator). */
    static constexpr std::size_t kCapacity = 512;

    /** @brief Constructs an empty secure string. */
    SecureString() noexcept = default;

    /** @brief Constructs from a string view, truncating to @ref kCapacity.
     *  @param data The secret data to store. */
    explicit SecureString(std::string_view data) noexcept;

    /** @brief Zeroes the internal buffer on destruction. */
    ~SecureString();

    // ── Move ───────────────────────────────────────────────────────────

    /** @brief Move constructor — transfers ownership, zeroes the source. */
    SecureString(SecureString&& other) noexcept;

    /** @brief Move assignment — transfers ownership, zeroes the source. */
    SecureString& operator=(SecureString&& other) noexcept;

    // ── Copy (deleted) ─────────────────────────────────────────────────

    SecureString(const SecureString&) = delete;
    SecureString& operator=(const SecureString&) = delete;

    // ── Accessors ──────────────────────────────────────────────────────

    /** @brief Returns a pointer to the internal null-terminated buffer.
     *  @return Non-null pointer (always valid, even when empty). */
    [[nodiscard]] const char* Data() const noexcept;

    /** @brief Returns the number of characters stored (excluding terminator).
     *  @return Byte count in [0, kCapacity]. */
    [[nodiscard]] std::size_t Size() const noexcept;

    /** @brief Returns true when no secret is stored.
     *  @return @c true if @ref Size() is zero. */
    [[nodiscard]] bool Empty() const noexcept;

    /** @brief Clears the stored secret and zeroes the buffer. */
    void Clear() noexcept;

    /** @brief Returns a non-owning view of the stored data.
     *  @return View spanning the full stored byte range. */
    [[nodiscard]] std::string_view View() const noexcept;

    // ── Comparison ─────────────────────────────────────────────────────

    /** @brief Constant-time-ish byte comparison against another secure string. */
    bool operator==(const SecureString& other) const noexcept;

    /** @brief Comparison against a plain string view (use sparingly). */
    bool operator==(std::string_view sv) const noexcept;

private:
    /** @brief Overwrites the entire buffer with zeros and resets size. */
    void SecureZero() noexcept;

    char m_data[kCapacity]{};
    std::size_t m_size = 0;
};

} // namespace Horo::Core
