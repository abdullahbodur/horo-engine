/** @file PasswordFD.h
 *  @brief Reads a password or secret from a file descriptor.
 *
 *  Some deployment environments pass secrets via file descriptors (e.g.
 *  systemd's LoadCredential, Docker secrets mounted as files, or a parent
 *  process writing to a pipe).  PasswordFD provides a safe way to read
 *  such secrets into a SecureString.
 *
 *  Platform availability:
 *  - POSIX (Linux, macOS): full support via read(2).
 *  - Windows:           FD-based password reading is rare; this header
 *                       declares the API but always returns an empty string.
 *
 *  The caller is responsible for closing the file descriptor unless
 *  ReadPasswordFromFD is called with closeFd=true.
 */
#pragma once

#include "core/security/SecureString.h"

#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Horo::Security {

/** @brief Reads up to @p maxBytes from file descriptor @p fd into a
 *         SecureString.
 *
 *  Reads until EOF, a newline ('\n'), or @p maxBytes is reached (whichever
 *  comes first).  Carriage returns ('\r') are stripped.  The result does
 *  NOT include the newline terminator.
 *
 *  @param fd         Open file descriptor to read from.
 *  @param maxBytes   Maximum number of bytes to read (prevents runaway reads).
 *  @param closeFd    If true, close(fd) is called after reading.
 *  @return A SecureString containing the password, or an empty string on
 *          error / EOF before any data / unsupported platform.
 */
inline SecureString ReadPasswordFromFD(int fd, size_t maxBytes = 4096,
                                        bool closeFd = true) {
#if defined(_WIN32)
    (void)fd;
    (void)maxBytes;
    (void)closeFd;
    return SecureString();
#else
    if (fd < 0 || maxBytes == 0) {
        if (closeFd && fd >= 0) ::close(fd);
        return SecureString();
    }

    // Stack-local buffer. Content is wiped before return.
    constexpr size_t kBufSize = 4096;
    char buf[kBufSize];

    // Accumulator — uses std::string temporarily, then moved into SecureString.
    std::string accum;
    accum.reserve(maxBytes < kBufSize ? maxBytes : kBufSize);

    while (accum.size() < maxBytes) {
        size_t remaining = maxBytes - accum.size();
        size_t chunk = (remaining < kBufSize) ? remaining : kBufSize;

        ssize_t n = ::read(fd, buf, chunk);
        if (n < 0) {
            Detail::SecureWipe(buf, kBufSize);
            Detail::SecureWipe(accum.data(), accum.capacity());
            if (closeFd) ::close(fd);
            return SecureString();
        }
        if (n == 0) break; // EOF

        // Scan for newline within this chunk
        bool foundNewline = false;
        size_t keep = 0;
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                foundNewline = true;
                keep = static_cast<size_t>(i);
                break;
            }
        }

        if (foundNewline) {
            // Append pre-newline data, strip trailing \r
            if (keep > 0) {
                size_t len = keep;
                if (buf[len - 1] == '\r') --len;
                accum.append(buf, len);
            }
            break;
        }

        // No newline — append entire chunk
        accum.append(buf, static_cast<size_t>(n));
    }

    // Build SecureString from accumulator
    SecureString result = accum.empty()
        ? SecureString()
        : SecureString::FromView(accum);

    // Wipe temporaries
    Detail::SecureWipe(accum.data(), accum.capacity());
    Detail::SecureWipe(buf, kBufSize);

    if (closeFd) ::close(fd);
    return result;
#endif
}

} // namespace Horo::Security
