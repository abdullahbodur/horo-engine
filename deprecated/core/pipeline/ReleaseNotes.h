/** @file ReleaseNotes.h
 *  @brief Release notes generation from conventional commit history.
 *
 *  Parses git log output (conventional commit format) and renders grouped
 *  markdown suitable for editor UI display, file output, or publish flows.
 *
 *  ## Determinism
 *  The parser is deterministic: given the same git log input, it produces
 *  the same ordered entries and identical markdown output.  Commit order
 *  within each group follows the original git log order (most recent first).
 *
 *  ## Grouping
 *  - Feat (feat): New features
 *  - Fix (fix): Bug fixes
 *  - Perf (perf): Performance improvements
 *  - Docs (docs): Documentation changes
 *  - Breaking (feat!/fix! or BREAKING CHANGE footer): Breaking changes
 *  - Misc: Everything else (chore, refactor, test, ci, build, style, unknown)
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Horo::Build {

/** @brief Conventional commit type classification for release notes. */
enum class ReleaseNoteKind {
    Feat,     /**< New feature (feat:). */
    Fix,      /**< Bug fix (fix:). */
    Perf,     /**< Performance improvement (perf:). */
    Docs,     /**< Documentation change (docs:). */
    Breaking, /**< Breaking change (feat! / fix! / BREAKING CHANGE footer). */
    Unknown,  /**< Unrecognised commit type — routed to Misc. */
};

/** @brief A single parsed conventional commit entry. */
struct ReleaseNoteEntry {
    ReleaseNoteKind kind = ReleaseNoteKind::Unknown;
    std::string scope;    /**< Optional scope (e.g. "editor", "renderer"). */
    std::string summary;  /**< Commit subject line after type/scope prefix. */
    std::string hash;     /**< Short commit hash for display (8 chars). */
};

/** @brief Complete release notes document with metadata and entries. */
struct ReleaseNotes {
    std::string versionFrom;             /**< Previous release tag. */
    std::string versionTo;              /**< Current release tag or HEAD. */
    std::vector<ReleaseNoteEntry> entries; /**< Parsed entries in git log order. */
    std::string markdown;               /**< Rendered markdown output. */
};

/** @brief Returns the display label for a release note kind.
 *
 *  @param kind The kind to label.
 *  @return Null-terminated string (e.g. "Features", "Bug Fixes"). */
const char *GetReleaseNoteKindLabel(ReleaseNoteKind kind);

/** @brief Parses a single conventional commit message.
 *
 *  Recognises the format:
 *  ```
 *  <type>[optional(scope)][!]: <description>
 *  ```
 *  Breaking changes are detected via:
 *  - A `!` immediately before the `:` separator.
 *  - A `BREAKING CHANGE:` or `BREAKING-CHANGE:` footer line.
 *
 *  When a commit is both a known type AND breaking, its kind is set to
 *  Breaking so it appears under the breaking section rather than its
 *  original type section.
 *
 *  @param message The complete commit message (subject line + body + footer).
 *  @param hash    Short commit hash for display (e.g. "a1b2c3d4").
 *  @return A parsed entry with kind, scope, summary, and hash populated. */
ReleaseNoteEntry ParseCommit(std::string_view message, std::string_view hash);

/** @brief Parses raw git log output into release note entries.
 *
 *  Expects input formatted with:
 *  ```
 *  git log --pretty=format:"%H%n%s%n%b---" <previous_tag>..HEAD
 *  ```
 *  Each commit is delimited by a line containing only "---".
 *
 *  Entries are returned in git log order (most recent first).
 *
 *  @param logOutput Raw stdout from a git log command.
 *  @return Ordered list of parsed entries. */
std::vector<ReleaseNoteEntry> ParseGitLog(std::string_view logOutput);

/** @brief Renders release notes as markdown from parsed entries.
 *
 *  Groups entries by kind in fixed order: Features → Bug Fixes →
 *  Performance → Documentation → Breaking Changes → Miscellaneous.
 *  Unknown and unrecognised commit types are routed to Misc.
 *
 *  @param entries     Parsed commit entries in git log order.
 *  @param versionFrom Previous tag (e.g. "v0.2.0").
 *  @param versionTo   Current tag (e.g. "v0.3.0" or "HEAD").
 *  @return Markdown string suitable for UI display or file output. */
std::string RenderMarkdown(const std::vector<ReleaseNoteEntry> &entries,
                           std::string_view versionFrom,
                           std::string_view versionTo);

/** @brief Full pipeline: parse git log and render markdown in one call.
 *
 *  Equivalent to `RenderMarkdown(ParseGitLog(log), from, to)` but returns
 *  the complete ReleaseNotes struct with both entries and rendered markdown.
 *
 *  @param gitLog      Raw git log output.
 *  @param versionFrom Previous release tag.
 *  @param versionTo   Current release tag or "HEAD".
 *  @return Complete release notes with entries and markdown fields populated. */
ReleaseNotes Generate(std::string_view gitLog,
                      std::string_view versionFrom,
                      std::string_view versionTo);

} // namespace Horo::Build
