/** @file CredentialRedactor.h
 *  @brief Utility for stripping credentials from strings (log sanitisation).
 *
 *  The engine logs command lines, error messages, and debug output that may
 *  contain secrets (passwords, API keys, access tokens).  CredentialRedactor
 *  provides a registry of patterns that, when matched, replace the value with
 *  a fixed redaction marker.
 *
 *  Patterns are simple prefix→value mappings.  When the redactor encounters
 *  a known prefix (e.g. "--password=") it scans forward until a delimiter
 *  (whitespace, quote, end-of-string) and replaces the value portion with
 *  "[REDACTED]".
 *
 *  Thread safety: the global registry is NOT synchronised.  Register patterns
 *  during startup before any logging thread is spawned.  Redact() itself is
 *  re-entrant (it only reads the registry).
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Horo::Security {

/** @brief Describes one redaction pattern.
 *
 *  When the prefix is found in the input, the characters following it are
 *  consumed until a delimiter (whitespace, single/double quote, NUL, or
 *  end-of-string).  The consumed span is replaced with "[REDACTED]". */
struct RedactionRule {
    /** @brief Prefix that marks the start of a secret value.
     *  Case-sensitive.  Typically ends with '=' or ':'. */
    std::string_view prefix;

    /** @brief Human-readable label logged in place of the redacted value. */
    std::string_view label;
};

/** @brief Registry of credential-redaction rules.
 *
 *  Add rules during initialisation; the instance is read-only at runtime.
 *  Construct one per subsystem that needs redaction, or share a global
 *  instance via CredentialRedactor::Global(). */
class CredentialRedactor {
public:
    /** @brief Adds a redaction rule.
     *  @param prefix  E.g. "--password=", "token:", "api_key=".
     *  @param label   E.g. "password", "token", "api_key" (used in the
     *                 replacement marker).
     *  @param spaceDelimited  If false, value scan stops only at EOL/EOS/quote,
     *                         not at space/tab.  Use for HTTP auth headers. */
    void AddRule(std::string_view prefix, std::string_view label,
                 bool spaceDelimited = true) {
        m_rules.push_back(
            {std::string(prefix), std::string(label), spaceDelimited});
    }

    /** @brief Applies all registered rules to @p input and returns a
     *         new string with secrets replaced by "[<label> REDACTED]".
     *
     *  This function allocates — it is intended for log-path sanitisation,
     *  not for hot paths.  If the input contains no secrets it returns an
     *  exact copy. */
    std::string Redact(std::string_view input) const;

    /** @brief Returns the global singleton instance.
     *
     *  The global redactor ships with a set of common patterns:
     *  --password, --token, --api-key, --secret, etc.
     *  Callers may add project-specific rules during startup. */
    static CredentialRedactor& Global();

    CredentialRedactor();

private:
    struct Rule {
        std::string prefix;
        std::string label;
        bool spaceDelimited = true; /**< If false, value scan stops only at EOL/EOS/quote
                                         (used for HTTP auth headers like Authorization,
                                         where the credential spans multiple space-separated tokens). */
    };

    static size_t SkipPrefixWhitespace(std::string_view input, size_t pos);
    static bool IsValueDelimiter(char c, bool spaceDelimited);
    static size_t FindValueEnd(std::string_view input, size_t pos,
                               bool spaceDelimited);
    static void AppendRedactionMarker(std::string &result, const Rule &rule,
                                      bool hadWhitespace);
    bool TryRedactAt(std::string_view input, size_t &pos,
                     std::string &result) const;

    std::vector<Rule> m_rules;
};

// ── Inline implementation ─────────────────────────────────────────────────

inline CredentialRedactor::CredentialRedactor() {
    // Common patterns — extend via Global().AddRule() at startup.
    AddRule("--password=",       "password");
    AddRule("--pwd=",            "password");
    AddRule("--token=",          "token");
    AddRule("--api-key=",        "api_key");
    AddRule("--api-key:",        "api_key");
    AddRule("--secret=",         "secret");
    AddRule("--access-token=",   "access_token");
    AddRule("password:",         "password");
    AddRule("password=",         "password");
    AddRule("token:",            "token");
    AddRule("token=",            "token");
    AddRule("api_key=",          "api_key");
    AddRule("api_key:",          "api_key");
    AddRule("secret:",           "secret");
    AddRule("secret=",           "secret");
    AddRule("access_token:",     "access_token");
    AddRule("access_token=",     "access_token");
    AddRule("Authorization:",    "auth_header", /*spaceDelimited*/ false);
    AddRule("X-API-Key:",        "api_key_header", /*spaceDelimited*/ false);
}

inline size_t CredentialRedactor::SkipPrefixWhitespace(std::string_view input,
                                                       size_t pos) {
    while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t'))
        ++pos;
    return pos;
}

inline bool CredentialRedactor::IsValueDelimiter(char c, bool spaceDelimited) {
    if (c == '\n' || c == '\r' || c == '\0' || c == '"' || c == '\'')
        return true;
    return spaceDelimited && (c == ' ' || c == '\t');
}

inline size_t CredentialRedactor::FindValueEnd(std::string_view input, size_t pos,
                                               bool spaceDelimited) {
    while (pos < input.size() && !IsValueDelimiter(input[pos], spaceDelimited))
        ++pos;
    return pos;
}

inline void CredentialRedactor::AppendRedactionMarker(std::string &result,
                                                      const Rule &rule,
                                                      bool hadWhitespace) {
    if (hadWhitespace)
        result.push_back(' ');
    result.append("[");
    result.append(rule.label);
    result.append("_REDACTED]");
}

inline bool CredentialRedactor::TryRedactAt(std::string_view input, size_t &pos,
                                            std::string &result) const {
    for (const auto& rule : m_rules) {
        if (input.substr(pos, rule.prefix.size()) != rule.prefix)
            continue;

        result.append(rule.prefix);
        pos += rule.prefix.size();

        const size_t valueStart = SkipPrefixWhitespace(input, pos);
        const bool hadWhitespace = (valueStart > pos);
        size_t valueEnd = FindValueEnd(input, valueStart, rule.spaceDelimited);
        AppendRedactionMarker(result, rule, hadWhitespace);

        if (valueEnd < input.size() &&
            (input[valueEnd] == '"' || input[valueEnd] == '\'')) {
            result.push_back(input[valueEnd]);
            ++valueEnd;
        }

        pos = valueEnd;
        return true;
    }
    return false;
}

inline std::string CredentialRedactor::Redact(std::string_view input) const {
    std::string result;
    result.reserve(input.size()); // optimistic: no redactions needed

    size_t pos = 0;
    while (pos < input.size()) {
        if (TryRedactAt(input, pos, result))
            continue;

        result.push_back(input[pos]);
        ++pos;
    }

    return result;
}

inline CredentialRedactor& CredentialRedactor::Global() {
    static CredentialRedactor instance;
    return instance;
}

} // namespace Horo::Security
