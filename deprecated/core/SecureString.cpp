/** @file SecureString.cpp
 *  @brief Implementation of the SecureString move-only zeroizing buffer.
 */
#include "core/SecureString.h"

#include <algorithm>
#include <cstring>

namespace Horo::Core {

/** @copydoc SecureString::SecureString(std::string_view) */
SecureString::SecureString(std::string_view data) noexcept {
    if (data.empty())
        return;

    const std::size_t len = std::min(data.size(), kCapacity);
    std::memcpy(m_data, data.data(), len);
    m_size = len;
    // Null-terminate for safe Data() access even when full
    if (m_size < kCapacity)
        m_data[m_size] = '\0';
}

/** @copydoc SecureString::~SecureString */
SecureString::~SecureString() {
    SecureZero();
}

/** @copydoc SecureString::SecureString(SecureString&&) */
SecureString::SecureString(SecureString&& other) noexcept {
    std::memcpy(m_data, other.m_data, kCapacity);
    m_size = other.m_size;
    other.SecureZero();
}

/** @copydoc SecureString::operator=(SecureString&&) */
SecureString& SecureString::operator=(SecureString&& other) noexcept {
    if (this != &other) {
        SecureZero();
        std::memcpy(m_data, other.m_data, kCapacity);
        m_size = other.m_size;
        other.SecureZero();
    }
    return *this;
}

/** @copydoc SecureString::Data */
const char* SecureString::Data() const noexcept {
    return m_data;
}

/** @copydoc SecureString::Size */
std::size_t SecureString::Size() const noexcept {
    return m_size;
}

/** @copydoc SecureString::Empty */
bool SecureString::Empty() const noexcept {
    return m_size == 0;
}

/** @copydoc SecureString::Clear */
void SecureString::Clear() noexcept {
    SecureZero();
}

/** @copydoc SecureString::View */
std::string_view SecureString::View() const noexcept {
    return { m_data, m_size };
}

/** @copydoc SecureString::operator==(const SecureString&) */
bool SecureString::operator==(const SecureString& other) const noexcept {
    if (m_size != other.m_size)
        return false;

    // Constant-time-ish: no early exit, iterate full compare length
    // (not cryptographically constant-time due to compiler optimizations,
    //  but good enough for defense-in-depth)
    int diff = 0;
    for (std::size_t i = 0; i < m_size; ++i)
        diff |= static_cast<unsigned char>(m_data[i]) ^
                static_cast<unsigned char>(other.m_data[i]);
    return diff == 0;
}

/** @copydoc SecureString::operator==(std::string_view) */
bool SecureString::operator==(std::string_view sv) const noexcept {
    if (m_size != sv.size())
        return false;

    int diff = 0;
    for (std::size_t i = 0; i < m_size; ++i)
        diff |= static_cast<unsigned char>(m_data[i]) ^
                static_cast<unsigned char>(sv[i]);
    return diff == 0;
}

/** @copydoc SecureString::SecureZero */
void SecureString::SecureZero() noexcept {
    // Use volatile pointer to discourage the compiler from optimizing
    // away the zeroing store as a "dead write."
    volatile char* p = m_data;
    for (std::size_t i = 0; i < kCapacity; ++i)
        p[i] = '\0';
    m_size = 0;
}

} // namespace Horo::Core
