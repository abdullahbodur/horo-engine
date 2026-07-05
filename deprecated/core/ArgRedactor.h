/** @file ArgRedactor.h
 *  @brief Centralized command-line redaction to prevent credentials from
 *         appearing in process logs and UI log panels.
 *
 *  Redaction is applied at display/logging time only — the actual command
 *  arguments passed to the OS process API are never modified.  This
 *  provides defense-in-depth without breaking command execution.
 */
#pragma once

#include <string>
#include <string_view>

namespace Horo {

/** @brief Returns a redacted copy of a command line suitable for logging and UI display.
 *
 *  Recognises password, token, certificate, and API-key argument patterns
 *  across POSIX-style (--flag=value, --flag value, -f value) and
 *  Windows-style (/flag value, /flag:value) conventions.  Matched values
 *  are replaced with "***".
 *
 *  The redacted string MUST NOT be used as the actual command — only for
 *  human-readable previews, log output, and UI panels.
 *
 *  @param commandLine  The raw command line to redact.
 *  @return A redacted copy safe for display. */
std::string RedactCommandLine(std::string_view commandLine);

/** @brief Unified entry point for redacting secrets from any string.
 *
 *  Applies both command-line pattern redaction (ArgRedactor) and
 *  prefix-based string redaction (CredentialRedactor).  Safe to call
 *  on any string — returns the input unmodified if no secrets found.
 *
 *  This is the recommended API for new code.  Call it on any string
 *  before display, logging, or IPC serialisation.
 *
 *  @param input  Arbitrary string that may contain secrets.
 *  @return A redacted copy safe for display, logging, or IPC. */
std::string RedactForDisplay(std::string_view input);

/** @brief Fast pre-scan to determine whether @p input MAY contain secrets.
 *
 *  Checks for known sensitive flag prefixes and credential-label prefixes
 *  without allocating.  Returns true if @p input contains any substring
 *  that could match redaction patterns.
 *
 *  Conservative: may return true for strings with no actual secrets
 *  (e.g. "--password-strength") but MUST NOT return false for strings
 *  containing real secrets.  Used by LogImpl to avoid allocation in
 *  the common case.
 *
 *  @param input  The string to scan.
 *  @return true if @p input may contain secrets requiring redaction. */
bool ContainsSensitivePrefix(std::string_view input);

} // namespace Horo
