/** @file ReleaseNotes.cpp
 *  @brief Release notes generation from conventional commit history.
 */
#include "core/pipeline/ReleaseNotes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <string>

namespace Horo::Build {
namespace {

/** @brief Section header strings for each kind, in render order. */
constexpr std::array kSectionHeaders = {
    "## 🚀 Features",
    "## 🐛 Bug Fixes",
    "## ⚡ Performance",
    "## 📚 Documentation",
    "## ⚠️ Breaking Changes",
    "## 📦 Miscellaneous",
};

/** @brief Returns the render-order index for a kind (0 = Features, 5 = Misc). */
constexpr std::size_t KindToSectionIndex(ReleaseNoteKind kind) {
    using enum ReleaseNoteKind;
    switch (kind) {
    case Feat:     return 0;
    case Fix:      return 1;
    case Perf:     return 2;
    case Docs:     return 3;
    case Breaking: return 4;
    default:       return 5;
    }
}

/** @brief Trims leading whitespace from a string view. */
std::string_view TrimLeft(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    return s;
}

/** @brief Trims trailing whitespace from a string view. */
std::string_view TrimRight(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

/** @brief Trims leading and trailing whitespace. */
std::string_view Trim(std::string_view s) {
    return TrimLeft(TrimRight(s));
}

/** @brief Checks if a footer line is a BREAKING CHANGE marker. */
bool IsBreakingChangeFooter(std::string_view line) {
    const std::string_view trimmed = Trim(line);

    // Case-insensitive prefix check for "BREAKING CHANGE:" or "BREAKING-CHANGE:"
    if (trimmed.size() < 16)
        return false;

    // Compare "BREAKING" prefix case-insensitively
    constexpr std::string_view kPrefix = "BREAKING";
    for (std::size_t i = 0; i < kPrefix.size(); ++i) {
        const auto a = static_cast<char>(std::toupper(
            static_cast<unsigned char>(trimmed[i])));
        const auto b = static_cast<char>(std::toupper(
            static_cast<unsigned char>(kPrefix[i])));
        if (a != b)
            return false;
    }

    // After "BREAKING", expect " CHANGE:" or "-CHANGE:"
    const std::string_view suffix = trimmed.substr(kPrefix.size());
    return suffix.starts_with(" CHANGE:") || suffix.starts_with("-CHANGE:");
}

/** @brief Checks if a full commit message contains a breaking change footer. */
bool HasBreakingChangeFooter(std::string_view message) {
    // Split by newlines, look for footer after the first blank line.
    // A footer is any line after the first empty line in the message body.
    bool foundBlank = false;
    std::size_t pos = 0;
    while (pos < message.size()) {
        const std::size_t nlPos = message.find('\n', pos);
        const std::string_view line = (nlPos == std::string_view::npos)
                                          ? message.substr(pos)
                                          : message.substr(pos, nlPos - pos);

        if (foundBlank && IsBreakingChangeFooter(line))
            return true;

        if (Trim(line).empty())
            foundBlank = true;

        if (nlPos == std::string_view::npos)
            break;
        pos = nlPos + 1;
    }
    return false;
}

/** @brief Classifies a conventional commit type string into a ReleaseNoteKind. */
ReleaseNoteKind ClassifyType(std::string_view type) {
    using enum ReleaseNoteKind;
    if (type == "feat")
        return Feat;
    if (type == "fix")
        return Fix;
    if (type == "perf")
        return Perf;
    if (type == "docs")
        return Docs;
    return Unknown;
}

/** @brief Parses one hash-and-message block from formatted git log output. */
std::optional<ReleaseNoteEntry> ParseGitLogBlock(std::string_view block) {
    if (block.empty())
        return std::nullopt;

    const std::size_t hashEnd = block.find('\n');
    if (hashEnd == std::string_view::npos)
        return std::nullopt;

    const std::string_view fullHash = block.substr(0, hashEnd);
    const std::string_view shortHash =
        fullHash.size() >= 8 ? fullHash.substr(0, 8) : fullHash;
    const std::string_view message = Trim(block.substr(hashEnd + 1));
    if (message.empty())
        return std::nullopt;
    return ParseCommit(message, shortHash);
}

} // namespace

/** @copydoc GetReleaseNoteKindLabel */
const char *GetReleaseNoteKindLabel(ReleaseNoteKind kind) {
    using enum ReleaseNoteKind;
    switch (kind) {
    case Feat:     return "Features";
    case Fix:      return "Bug Fixes";
    case Perf:     return "Performance";
    case Docs:     return "Documentation";
    case Breaking: return "Breaking Changes";
    case Unknown:  return "Miscellaneous";
    }
    return "Miscellaneous";
}

/** @copydoc ParseCommit */
ReleaseNoteEntry ParseCommit(std::string_view message, std::string_view hash) {
    ReleaseNoteEntry entry;
    entry.hash = hash;

    if (message.empty())
        return entry;

    // Extract the subject line (first line of the message)
    const std::size_t subjectEnd = message.find('\n');
    const std::string_view subject =
        (subjectEnd == std::string_view::npos) ? message
                                                : message.substr(0, subjectEnd);

    // Subject format: <type>[optional(scope)][!]: <description>
    //
    // Find the first ':' that separates the type/scope prefix from the
    // description.  We walk forward character by character to handle
    // scopes that may contain colons (though uncommon).
    std::size_t colonPos = std::string_view::npos;
    bool isBreaking = false;
    {
        std::size_t parenDepth = 0;
        for (std::size_t i = 0; i < subject.size(); ++i) {
            const char c = subject[i];
            if (c == '(')
                ++parenDepth;
            else if (c == ')')
                parenDepth = parenDepth > 0 ? parenDepth - 1 : 0;
            else if (c == '!' && parenDepth == 0 && i + 1 < subject.size() &&
                     subject[i + 1] == ':') {
                // `!:` pattern — breaking change marker
                isBreaking = true;
            } else if (c == ':' && parenDepth == 0) {
                colonPos = i;
                break;
            }
        }
    }

    if (colonPos == std::string_view::npos) {
        // No conventional commit prefix found — treat entire subject as
        // summary with Unknown kind.
        entry.kind = ReleaseNoteKind::Unknown;
        entry.summary = Trim(subject);
        return entry;
    }

    const std::string_view prefix = subject.substr(0, colonPos);

    // Parse type: everything before '(' or '!' or end of prefix
    std::size_t typeEnd = 0;
    while (typeEnd < prefix.size() && prefix[typeEnd] != '(' &&
           prefix[typeEnd] != '!') {
        ++typeEnd;
    }
    const std::string_view type = Trim(prefix.substr(0, typeEnd));

    // Parse scope: between '(' and ')'
    std::string scope;
    if (const std::size_t openParen = prefix.find('(');
        openParen != std::string_view::npos) {
        const std::size_t closeParen = prefix.find(')', openParen);
        if (closeParen != std::string_view::npos)
            scope = prefix.substr(openParen + 1, closeParen - openParen - 1);
    }

    // Parse summary: everything after ": "
    std::string_view summary = subject.substr(colonPos + 1);
    summary = Trim(summary);

    // Check for breaking change footer in the full message body
    const bool hasBreakingFooter = HasBreakingChangeFooter(message);

    // Classify
    const ReleaseNoteKind baseKind = ClassifyType(type);

    if (isBreaking || hasBreakingFooter) {
        entry.kind = ReleaseNoteKind::Breaking;
    } else {
        entry.kind = baseKind;
    }

    entry.scope = std::move(scope);
    entry.summary = summary;

    return entry;
}

/** @copydoc ParseGitLog */
std::vector<ReleaseNoteEntry> ParseGitLog(std::string_view logOutput) {
    std::vector<ReleaseNoteEntry> entries;

    if (logOutput.empty())
        return entries;

    // The git log format is:
    //   <full-hash>
    //   <subject>
    //   <body lines...>
    //   ---
    //
    // We split on the "---\n" delimiter.
    const std::string_view kDelimiter = "---\n";

    std::size_t start = 0;
    while (start < logOutput.size()) {
        // Find the end of this commit block
        const std::size_t delimPos = logOutput.find(kDelimiter, start);
        if (const std::string_view block =
                delimPos == std::string_view::npos
                    ? logOutput.substr(start)
                    : logOutput.substr(start, delimPos - start);
            const auto entry = ParseGitLogBlock(block))
            entries.push_back(*entry);

        if (delimPos == std::string_view::npos)
            break;
        start = delimPos + kDelimiter.size();
    }

    return entries;
}

/** @copydoc RenderMarkdown */
std::string RenderMarkdown(const std::vector<ReleaseNoteEntry> &entries,
                           std::string_view versionFrom,
                           std::string_view versionTo) {
    // Group entries by section index, preserving order within each group
    struct Section {
        const char *header;
        std::vector<const ReleaseNoteEntry *> entries;
    };
    std::array<Section, kSectionHeaders.size()> sections{};
    std::transform(kSectionHeaders.begin(), kSectionHeaders.end(),
                   sections.begin(), [](const char *header) {
                       return Section{header, {}};
                   });

    for (const auto &entry : entries)
        sections[KindToSectionIndex(entry.kind)].entries.push_back(&entry);

    // Build markdown
    std::string md;

    // Title
    md += std::format("# Release Notes — {} → {}\n\n",
                      versionFrom, versionTo);

    // Summary line
    md += std::format("*{} commit{} included in this release*\n\n",
                      entries.size(), entries.size() == 1 ? "" : "s");

    bool firstSection = true;
    for (const Section &section : sections) {
        if (section.entries.empty())
            continue;

        if (!firstSection)
            md += '\n';
        firstSection = false;

        md += section.header;
        md += '\n';

        for (const ReleaseNoteEntry *entry : section.entries) {
            md += "- ";
            if (!entry->scope.empty())
                md += std::format("**{}:** ", entry->scope);
            md += entry->summary;
            md += std::format(" (`{}`)", entry->hash);
            md += '\n';
        }
    }

    return md;
}

/** @copydoc Generate */
ReleaseNotes Generate(std::string_view gitLog,
                      std::string_view versionFrom,
                      std::string_view versionTo) {
    ReleaseNotes notes;
    notes.versionFrom = versionFrom;
    notes.versionTo = versionTo;
    notes.entries = ParseGitLog(gitLog);
    notes.markdown = RenderMarkdown(notes.entries, versionFrom, versionTo);
    return notes;
}

} // namespace Horo::Build
