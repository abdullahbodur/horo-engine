/** @file ArgRedactor.cpp
 *  @brief Implements command-line redaction using position-based scanning. */
#include "core/ArgRedactor.h"
#include "core/security/CredentialRedactor.h"

#include <string>

namespace Horo {
namespace {

/** @brief Finds the end of the current shell token (arg or quoted value).
 *
 *  Handles double-quoted and single-quoted strings, returning the position
 *  just past the closing quote (or end of the unquoted token). */
size_t FindTokenEnd(std::string_view s, size_t pos) {
    if (pos >= s.size())
        return pos;

    if (s[pos] == '"') {
        // Double-quoted: scan past closing '"'
        ++pos;
        while (pos < s.size() && s[pos] != '"')
            ++pos;
        return pos < s.size() ? pos + 1 : pos; // past closing '"' or end
    }
    if (s[pos] == '\'') {
        // Single-quoted: scan past closing '\''
        ++pos;
        while (pos < s.size() && s[pos] != '\'')
            ++pos;
        return pos < s.size() ? pos + 1 : pos;
    }
    // Unquoted: scan to next whitespace, quote char, or end
    while (pos < s.size() && s[pos] != ' ' && s[pos] != '\t' &&
           s[pos] != '"' && s[pos] != '\'')
        ++pos;
    return pos;
}

/** @brief Given the start of a value (after flag+separator), finds the end
 *  of the value token and returns the position to resume scanning from. */
size_t FindValueEnd(std::string_view s, size_t valueStart) {
    if (valueStart >= s.size())
        return valueStart;

    // If the value is a quoted string, the token end IS the value end.
    // If unquoted, scan to the next whitespace.
    return FindTokenEnd(s, valueStart);
}

/** @brief Returns the length of whitespace at the start of `s`. */
size_t LeadingWhitespaceLen(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    return i;
}

/** @brief Structure describing a sensitive flag pattern. */
struct SensitivePattern {
    std::string_view longFlag;   /**< e.g. "--password" */
    std::string_view shortFlag;  /**< e.g. "-p" (empty if none) */
    std::string_view winFlag;    /**< e.g. "/p" (empty if none) */
};

/** @brief Ordered list of sensitive flag patterns checked during scanning. */
constexpr SensitivePattern kSensitivePatterns[] = {
    {"--password",            "",           ""   },
    {"--token",               "",           ""   },
    {"--secret",              "",           ""   },
    {"--api-key",             "",           ""   },
    {"--key",                 "",           ""   },
    {"--passphrase",          "",           ""   },
    {"--certificate-password","",           ""   },
    {"--archive-password",    "",           ""   },
    {"--signing-password",    "",           ""   },
    {"--pfx-password",        "",           ""   },
    {"--notarization-password","",          ""   },
    {"--access-key",          "",           ""   },
    {"--private-key",         "",           ""   },
    // Windows-style signtool flags
    {"",                      "",           "/p" },  // signtool password
    {"",                      "",           "/f" },  // signtool certificate file
};

/** @brief Returns the position just past the flag name if `s[pos]` matches
 *  `flag` (followed by '=' or whitespace).  Returns `pos` (no match) otherwise. */
size_t MatchLongFlag(std::string_view s, size_t pos, std::string_view flag) {
    if (pos + flag.size() > s.size())
        return pos;
    if (s.substr(pos, flag.size()) != flag)
        return pos;
    const size_t afterFlag = pos + flag.size();

    // --flag=value  → sensitive
    if (afterFlag < s.size() && s[afterFlag] == '=')
        return afterFlag + 1;  // point at value start

    // --flag value  → sensitive (need at least one whitespace and a value)
    if (afterFlag < s.size() && (s[afterFlag] == ' ' || s[afterFlag] == '\t'))
        return afterFlag;  // point at whitespace before value

    // --flag at end of line or followed by non-whitespace/non-equals → not sensitive
    return pos;
}

/** @brief Returns the position just past the flag name if `s[pos]` matches
 *  Windows-style `/flag` (followed by space, tab, or colon). */
size_t MatchWinFlag(std::string_view s, size_t pos, std::string_view flag) {
    if (pos + flag.size() > s.size())
        return pos;
    if (s.substr(pos, flag.size()) != flag)
        return pos;
    const size_t afterFlag = pos + flag.size();

    // /flag:value  → sensitive
    if (afterFlag < s.size() && s[afterFlag] == ':')
        return afterFlag + 1;

    // /flag value  → sensitive
    if (afterFlag < s.size() && (s[afterFlag] == ' ' || s[afterFlag] == '\t'))
        return afterFlag;

    // /flag at end or followed by non-whitespace/non-colon → not sensitive
    return pos;
}

/** @brief Redacts a matched "flag value" pattern at `valueStart`.
 *
 *  `valueStart` points at either:
 *    - The value character itself (for `--flag=value` or `/flag:value`)
 *    - The whitespace before the value (for `--flag value` or `/flag value`)
 *
 *  When @p skipWs is true, leading whitespace before the value is skipped
 *  (the caller already emitted a space separator).  This is the space-form path.
 *  When false (equals/colon-form), the separator ('=' or ':') was already
 *  emitted by the caller; whitespace after it means the value is empty,
 *  so we must NOT skip into the next token.
 *
 *  Appends `***` to `out` and returns the position to resume scanning from. */
size_t RedactValue(std::string_view s, size_t valueStart, std::string &out,
                   bool skipWs) {
    // Skip leading whitespace only for space-form (caller emitted ' ').
    if (skipWs) {
        const size_t wsLen = LeadingWhitespaceLen(s.substr(valueStart));
        if (wsLen > 0) {
            valueStart += wsLen;
        }
    }

    // Empty value edge case: --flag= or --flag at EOL
    if (valueStart >= s.size() || s[valueStart] == '\0' || s[valueStart] == '"' ||
        s[valueStart] == '\'') {
        // Still redact even empty/opening-quote for safety
    }

    out.append("***");

    // Skip past the actual value in the source so we resume after it
    return FindValueEnd(s, valueStart);
}

/** @brief Redacts a POSIX long sensitive flag at @p pos if one matches. */
bool TryRedactLongFlag(std::string_view commandLine, size_t &pos, std::string &out) {
    for (const auto &pattern : kSensitivePatterns) {
        if (pattern.longFlag.empty())
            continue;

        const size_t valueStart = MatchLongFlag(commandLine, pos, pattern.longFlag);
        if (valueStart == pos)
            continue;

        out.append(pattern.longFlag);
        const bool isEqualsForm =
            (valueStart < commandLine.size() && commandLine[valueStart - 1] == '=');
        if (isEqualsForm)
            out.push_back('=');
        else if (valueStart < commandLine.size())
            out.push_back(' ');

        pos = RedactValue(commandLine, valueStart, out, /*skipWs=*/!isEqualsForm);
        return true;
    }
    return false;
}

/** @brief Redacts a Windows-style sensitive flag at @p pos if one matches. */
bool TryRedactWinFlag(std::string_view commandLine, size_t &pos, std::string &out) {
    for (const auto &pattern : kSensitivePatterns) {
        if (pattern.winFlag.empty())
            continue;

        const size_t valueStart = MatchWinFlag(commandLine, pos, pattern.winFlag);
        if (valueStart == pos)
            continue;

        out.append(pattern.winFlag);
        const bool isColonForm =
            (valueStart > 0 && valueStart <= commandLine.size() &&
             commandLine[valueStart - 1] == ':');
        if (isColonForm)
            out.push_back(':');
        else if (valueStart < commandLine.size())
            out.push_back(' ');

        pos = RedactValue(commandLine, valueStart, out, /*skipWs=*/!isColonForm);
        return true;
    }
    return false;
}

} // namespace

/** @copydoc RedactCommandLine */
std::string RedactCommandLine(std::string_view commandLine) {
    std::string out;
    out.reserve(commandLine.size() + 16);  // slight over-allocation for "***"

    size_t pos = 0;
    while (pos < commandLine.size()) {
        if (commandLine[pos] != '-' && commandLine[pos] != '/') {
            out.push_back(commandLine[pos]);
            ++pos;
            continue;
        }

        if (commandLine[pos] == '-' && TryRedactLongFlag(commandLine, pos, out))
            continue;
        if (commandLine[pos] == '/' && TryRedactWinFlag(commandLine, pos, out))
            continue;

        out.push_back(commandLine[pos]);
        ++pos;
    }

    return out;
}

/** @copydoc RedactForDisplay */
std::string RedactForDisplay(std::string_view input) {
    // Layer 1: command-line flag patterns (ArgRedactor)
    std::string result = RedactCommandLine(input);

    // Layer 2: prefix-based credential patterns (CredentialRedactor)
    result = Security::CredentialRedactor::Global().Redact(result);

    return result;
}

namespace {

/** @brief Ordered list of fast-scan substrings that indicate potential secrets.
 *
 *  A conservative superset of patterns covered by RedactCommandLine and
 *  CredentialRedactor.  If the input contains any of these, we allocate
 *  and run full redaction. */
constexpr std::string_view kSensitiveSubstrings[] = {
    "--password", "--token", "--secret", "--api-key", "--key",
    "--passphrase", "--certificate-password", "--archive-password",
    "--signing-password", "--pfx-password", "--notarization-password",
    "--access-key", "--private-key",
    "/p ", "/f ",                       // Windows signtool (space-separated value)
    "/p:", "/f:",                       // Windows signtool (colon-separated value)
    "password:", "password=",           // CredentialRedactor header patterns
    "token:", "token=",
    "api_key:", "api_key=",
    "secret:", "secret=",
    "access_token:", "access_token=",
    "Authorization:", "X-API-Key:",
};

} // namespace

/** @copydoc ContainsSensitivePrefix */
bool ContainsSensitivePrefix(std::string_view input) {
    for (const auto &needle : kSensitiveSubstrings) {
        if (input.find(needle) != std::string_view::npos)
            return true;
    }
    return false;
}

} // namespace Horo
