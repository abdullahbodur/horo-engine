/** @file test_release_notes.cpp
 *  @brief Unit tests for ReleaseNotes — parsing conventional commits from
 *         fixture git log output and rendering deterministic markdown.
 *
 *  Covers:
 *  - Single commit parsing (feat, fix, perf, docs, breaking, unknown)
 *  - Breaking change detection (! and BREAKING CHANGE footer)
 *  - Scope extraction
 *  - Git log block parsing
 *  - Markdown rendering with grouping
 *  - Determinism (same input → same output)
 *  - Unknown types routed to Misc section
 */
#include <catch2/catch_test_macros.hpp>

#include "core/pipeline/ReleaseNotes.h"

#include <string>
#include <string_view>
#include <vector>

using namespace Horo::Build;

// ==========================================================================
//  Helper: build a git log fixture string
// ==========================================================================

namespace {

/** @brief Builds a git log fixture string from a list of (hash, message) pairs.
 *
 *  Format mirrors `git log --pretty=format:"%H%n%s%n%b---"`. */
std::string MakeGitLog(
    const std::vector<std::pair<std::string, std::string>> &commits) {
    std::string result;
    for (const auto &[hash, message] : commits) {
        result += hash + "\n" + message + "\n---\n";
    }
    return result;
}

} // namespace

// ==========================================================================
//  Section 1: ParseCommit — single commit message parsing
// ==========================================================================

TEST_CASE("ParseCommit: feat with no scope", "[parse][feat]") {
    const auto entry = ParseCommit("feat: add bloom post-process", "abc12345");
    REQUIRE(entry.kind == ReleaseNoteKind::Feat);
    REQUIRE(entry.scope.empty());
    REQUIRE(entry.summary == "add bloom post-process");
    REQUIRE(entry.hash == "abc12345");
}

TEST_CASE("ParseCommit: feat with scope", "[parse][feat][scope]") {
    const auto entry =
        ParseCommit("feat(renderer): implement PBR shading", "def67890");
    REQUIRE(entry.kind == ReleaseNoteKind::Feat);
    REQUIRE(entry.scope == "renderer");
    REQUIRE(entry.summary == "implement PBR shading");
    REQUIRE(entry.hash == "def67890");
}

TEST_CASE("ParseCommit: fix", "[parse][fix]") {
    const auto entry = ParseCommit("fix(core): prevent double-free in allocator",
                                   "11112222");
    REQUIRE(entry.kind == ReleaseNoteKind::Fix);
    REQUIRE(entry.scope == "core");
    REQUIRE(entry.summary == "prevent double-free in allocator");
}

TEST_CASE("ParseCommit: perf", "[parse][perf]") {
    const auto entry =
        ParseCommit("perf(physics): optimize broad-phase collision", "33334444");
    REQUIRE(entry.kind == ReleaseNoteKind::Perf);
    REQUIRE(entry.scope == "physics");
    REQUIRE(entry.summary == "optimize broad-phase collision");
}

TEST_CASE("ParseCommit: docs", "[parse][docs]") {
    const auto entry =
        ParseCommit("docs: update API reference for v0.3", "55556666");
    REQUIRE(entry.kind == ReleaseNoteKind::Docs);
    REQUIRE(entry.scope.empty());
    REQUIRE(entry.summary == "update API reference for v0.3");
}

TEST_CASE("ParseCommit: unknown type goes to Unknown", "[parse][unknown]") {
    const auto entry = ParseCommit("chore: bump dependencies", "77778888");
    REQUIRE(entry.kind == ReleaseNoteKind::Unknown);
    REQUIRE(entry.scope.empty());
    REQUIRE(entry.summary == "bump dependencies");
}

TEST_CASE("ParseCommit: refactor goes to Unknown", "[parse][unknown]") {
    const auto entry =
        ParseCommit("refactor(scene): extract entity pool", "99990000");
    REQUIRE(entry.kind == ReleaseNoteKind::Unknown);
    REQUIRE(entry.scope == "scene");
    REQUIRE(entry.summary == "extract entity pool");
}

TEST_CASE("ParseCommit: breaking via exclamation mark", "[parse][breaking]") {
    const auto entry = ParseCommit("feat!: drop legacy GL 3.3 support",
                                   "aaaa1111");
    REQUIRE(entry.kind == ReleaseNoteKind::Breaking);
    REQUIRE(entry.scope.empty());
    REQUIRE(entry.summary == "drop legacy GL 3.3 support");
}

TEST_CASE("ParseCommit: breaking with scope and exclamation",
          "[parse][breaking][scope]") {
    const auto entry =
        ParseCommit("feat(api)!: change engine init signature", "bbbb2222");
    REQUIRE(entry.kind == ReleaseNoteKind::Breaking);
    REQUIRE(entry.scope == "api");
    REQUIRE(entry.summary == "change engine init signature");
}

TEST_CASE("ParseCommit: breaking via BREAKING CHANGE footer",
          "[parse][breaking][footer]") {
    const auto entry = ParseCommit(
        "feat: rename Window to HoroWindow\n\n"
        "BREAKING CHANGE: Window class renamed to HoroWindow.\n"
        "Migration guide: replace all `Window` references.",
        "cccc3333");
    REQUIRE(entry.kind == ReleaseNoteKind::Breaking);
    REQUIRE(entry.scope.empty());
    REQUIRE(entry.summary == "rename Window to HoroWindow");
}

TEST_CASE("ParseCommit: breaking via BREAKING-CHANGE footer with hyphen",
          "[parse][breaking][footer]") {
    const auto entry = ParseCommit(
        "feat: remove deprecated input API\n\n"
        "BREAKING-CHANGE: InputManager deprecated methods removed.",
        "dddd4444");
    REQUIRE(entry.kind == ReleaseNoteKind::Breaking);
    REQUIRE(entry.summary == "remove deprecated input API");
}

TEST_CASE("ParseCommit: non-conventional format goes to Unknown",
          "[parse][unknown]") {
    const auto entry = ParseCommit("random commit with no prefix", "eeee5555");
    REQUIRE(entry.kind == ReleaseNoteKind::Unknown);
    REQUIRE(entry.summary == "random commit with no prefix");
}

TEST_CASE("ParseCommit: empty message returns Unknown with empty summary",
          "[parse][edge]") {
    const auto entry = ParseCommit("", "ffff6666");
    REQUIRE(entry.kind == ReleaseNoteKind::Unknown);
    REQUIRE(entry.summary.empty());
    REQUIRE(entry.hash == "ffff6666");
}

// ==========================================================================
//  Section 2: ParseGitLog — multi-commit log parsing
// ==========================================================================

TEST_CASE("ParseGitLog: empty input returns empty vector", "[parse][log]") {
    const auto entries = ParseGitLog("");
    REQUIRE(entries.empty());
}

TEST_CASE("ParseGitLog: single commit", "[parse][log]") {
    const auto log = MakeGitLog(
        {{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "feat: initial release"}});
    const auto entries = ParseGitLog(log);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].kind == ReleaseNoteKind::Feat);
    REQUIRE(entries[0].summary == "initial release");
    REQUIRE(entries[0].hash.size() == 8);
}

TEST_CASE("ParseGitLog: multiple commits in order", "[parse][log]") {
    const auto log = MakeGitLog({
        {"aaa00000000000000000000000000000000000001",
         "feat: add new feature"},
        {"aaa00000000000000000000000000000000000002",
         "fix: bug fix"},
        {"aaa00000000000000000000000000000000000003",
         "docs: update readme"},
    });
    const auto entries = ParseGitLog(log);
    REQUIRE(entries.size() == 3);
    REQUIRE(entries[0].summary == "add new feature");
    REQUIRE(entries[1].summary == "bug fix");
    REQUIRE(entries[2].summary == "update readme");
}

TEST_CASE("ParseGitLog: ignores commits with empty message body",
          "[parse][log][edge]") {
    // Hash line but no subject — should be skipped
    const auto log = MakeGitLog({
        {"aaa00000000000000000000000000000000000001", "\n"},
        {"aaa00000000000000000000000000000000000002",
         "feat: visible commit"},
    });
    const auto entries = ParseGitLog(log);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].summary == "visible commit");
}

TEST_CASE("ParseGitLog: mixed commit types", "[parse][log]") {
    const auto log = MakeGitLog({
        {"aaa01", "feat(editor): new gizmo tool"},
        {"aaa02", "fix(renderer): NaN in PBR shader"},
        {"aaa03", "perf(scene): reduce component lookups"},
        {"aaa04", "docs: add migration guide"},
        {"aaa05", "feat(api)!: rename public headers"},
        {"aaa06", "chore: update CI scripts"},
        {"aaa07", "test: add missing coverage"},
    });
    const auto entries = ParseGitLog(log);
    REQUIRE(entries.size() == 7);

    REQUIRE(entries[0].kind == ReleaseNoteKind::Feat);
    REQUIRE(entries[1].kind == ReleaseNoteKind::Fix);
    REQUIRE(entries[2].kind == ReleaseNoteKind::Perf);
    REQUIRE(entries[3].kind == ReleaseNoteKind::Docs);
    REQUIRE(entries[4].kind == ReleaseNoteKind::Breaking);
    REQUIRE(entries[5].kind == ReleaseNoteKind::Unknown);
    REQUIRE(entries[6].kind == ReleaseNoteKind::Unknown);
}

// ==========================================================================
//  Section 3: RenderMarkdown — grouped markdown output
// ==========================================================================

TEST_CASE("RenderMarkdown: empty entries produces header-only output",
          "[render]") {
    const auto md = RenderMarkdown({}, "v0.1.0", "v0.2.0");
    REQUIRE(md.starts_with("# Release Notes — v0.1.0 → v0.2.0"));
    REQUIRE(md.find("*0 commits included") != std::string::npos);
}

TEST_CASE("RenderMarkdown: single feat entry", "[render]") {
    ReleaseNoteEntry entry;
    entry.kind = ReleaseNoteKind::Feat;
    entry.summary = "add bloom post-process";
    entry.hash = "abc12345";

    const auto md = RenderMarkdown({entry}, "v0.1.0", "v0.2.0");
    REQUIRE(md.find("## 🚀 Features") != std::string::npos);
    REQUIRE(md.find("- add bloom post-process (`abc12345`)") !=
            std::string::npos);
}

TEST_CASE("RenderMarkdown: groups multiple entries by kind", "[render]") {
    std::vector<ReleaseNoteEntry> entries;

    {
        ReleaseNoteEntry e;
        e.kind = ReleaseNoteKind::Feat;
        e.summary = "add PBR shading";
        e.scope = "renderer";
        e.hash = "aaa";
        entries.push_back(e);
    }
    {
        ReleaseNoteEntry e;
        e.kind = ReleaseNoteKind::Fix;
        e.summary = "fix NaN in shader";
        e.scope = "renderer";
        e.hash = "bbb";
        entries.push_back(e);
    }
    {
        ReleaseNoteEntry e;
        e.kind = ReleaseNoteKind::Feat;
        e.summary = "add gizmo tool";
        e.scope = "editor";
        e.hash = "ccc";
        entries.push_back(e);
    }

    const auto md = RenderMarkdown(entries, "v0.1.0", "v0.2.0");

    // Features section should appear before Bug Fixes
    const auto featPos = md.find("## 🚀 Features");
    const auto fixPos = md.find("## 🐛 Bug Fixes");
    REQUIRE(featPos != std::string::npos);
    REQUIRE(fixPos != std::string::npos);
    REQUIRE(featPos < fixPos);

    // Both feat entries under Features section
    REQUIRE(md.find("- **renderer:** add PBR shading (`aaa`)") !=
            std::string::npos);
    REQUIRE(md.find("- **editor:** add gizmo tool (`ccc`)") !=
            std::string::npos);

    // Fix entry under Bug Fixes
    REQUIRE(md.find("- **renderer:** fix NaN in shader (`bbb`)") !=
            std::string::npos);
}

TEST_CASE("RenderMarkdown: unknown types go to Misc section", "[render]") {
    ReleaseNoteEntry entry;
    entry.kind = ReleaseNoteKind::Unknown;
    entry.summary = "bump dependencies";
    entry.hash = "ddd";

    const auto md = RenderMarkdown({entry}, "v0.1.0", "v0.2.0");
    REQUIRE(md.find("## 📦 Miscellaneous") != std::string::npos);
    REQUIRE(md.find("- bump dependencies (`ddd`)") != std::string::npos);
}

TEST_CASE("RenderMarkdown: all sections in correct order", "[render][order]") {
    // Create one entry per section
    std::vector<ReleaseNoteEntry> entries;
    const ReleaseNoteKind kinds[] = {
        ReleaseNoteKind::Feat,     ReleaseNoteKind::Fix,
        ReleaseNoteKind::Perf,     ReleaseNoteKind::Docs,
        ReleaseNoteKind::Breaking, ReleaseNoteKind::Unknown,
    };
    int idx = 0;
    for (auto kind : kinds) {
        ReleaseNoteEntry e;
        e.kind = kind;
        e.summary = "item " + std::to_string(idx);
        e.hash = "hash" + std::to_string(idx);
        entries.push_back(e);
        ++idx;
    }

    const auto md = RenderMarkdown(entries, "v0.1.0", "v0.2.0");

    // Verify section order
    const auto featPos = md.find("## 🚀 Features");
    const auto fixPos = md.find("## 🐛 Bug Fixes");
    const auto perfPos = md.find("## ⚡ Performance");
    const auto docsPos = md.find("## 📚 Documentation");
    const auto breakPos = md.find("## ⚠️ Breaking Changes");
    const auto miscPos = md.find("## 📦 Miscellaneous");

    REQUIRE(featPos < fixPos);
    REQUIRE(fixPos < perfPos);
    REQUIRE(perfPos < docsPos);
    REQUIRE(docsPos < breakPos);
    REQUIRE(breakPos < miscPos);
}

TEST_CASE("RenderMarkdown: skips empty sections", "[render]") {
    ReleaseNoteEntry entry;
    entry.kind = ReleaseNoteKind::Perf;
    entry.summary = "lone perf improvement";
    entry.hash = "eee";

    const auto md = RenderMarkdown({entry}, "v0.1.0", "v0.2.0");
    REQUIRE(md.find("## 🚀 Features") == std::string::npos);
    REQUIRE(md.find("## 🐛 Bug Fixes") == std::string::npos);
    REQUIRE(md.find("## ⚡ Performance") != std::string::npos);
}

TEST_CASE("RenderMarkdown: correct pluralization in summary line",
          "[render]") {
    ReleaseNoteEntry entry;
    entry.kind = ReleaseNoteKind::Feat;
    entry.summary = "single commit";
    entry.hash = "fff";

    const auto md1 = RenderMarkdown({entry}, "v0.1.0", "v0.2.0");
    REQUIRE(md1.find("*1 commit included") != std::string::npos);

    const auto md0 = RenderMarkdown({}, "v0.1.0", "v0.2.0");
    REQUIRE(md0.find("*0 commits included") != std::string::npos);

    std::vector<ReleaseNoteEntry> two = {entry, entry};
    const auto md2 = RenderMarkdown(two, "v0.1.0", "v0.2.0");
    REQUIRE(md2.find("*2 commits included") != std::string::npos);
}

// ==========================================================================
//  Section 4: Generate — full pipeline integration
// ==========================================================================

TEST_CASE("Generate: full pipeline returns entries and markdown", "[generate]") {
    const auto log = MakeGitLog({
        {"aaa01", "feat(editor): add gizmo tool"},
        {"aaa02", "fix(core): prevent crash"},
    });

    const auto notes = Generate(log, "v0.1.0", "v0.2.0");

    REQUIRE(notes.entries.size() == 2);
    REQUIRE(notes.versionFrom == "v0.1.0");
    REQUIRE(notes.versionTo == "v0.2.0");
    REQUIRE_FALSE(notes.markdown.empty());
    REQUIRE(notes.markdown.find("## 🚀 Features") != std::string::npos);
    REQUIRE(notes.markdown.find("## 🐛 Bug Fixes") != std::string::npos);
}

// ==========================================================================
//  Section 5: Determinism
// ==========================================================================

TEST_CASE("Determinism: same input produces identical output",
          "[determinism]") {
    const auto log = MakeGitLog({
        {"aaa01", "feat: feature A"},
        {"aaa02", "fix: bug B"},
        {"aaa03", "perf: speed C"},
        {"aaa04", "docs: doc D"},
        {"aaa05", "chore: chore E"},
    });

    const auto notes1 = Generate(log, "v0.1.0", "v0.2.0");
    const auto notes2 = Generate(log, "v0.1.0", "v0.2.0");

    REQUIRE(notes1.entries.size() == notes2.entries.size());
    REQUIRE(notes1.markdown == notes2.markdown);

    for (std::size_t i = 0; i < notes1.entries.size(); ++i) {
        REQUIRE(notes1.entries[i].kind == notes2.entries[i].kind);
        REQUIRE(notes1.entries[i].scope == notes2.entries[i].scope);
        REQUIRE(notes1.entries[i].summary == notes2.entries[i].summary);
        REQUIRE(notes1.entries[i].hash == notes2.entries[i].hash);
    }
}

TEST_CASE("Determinism: entry order preserved within groups", "[determinism]") {
    // Two feat entries — should appear in log order, not alphabetically
    const auto log = MakeGitLog({
        {"aaa02", "feat: second feature added"},
        {"aaa01", "feat: first feature added"},
    });

    const auto notes = Generate(log, "v0.1.0", "v0.2.0");

    const auto pos1 = notes.markdown.find("first feature added");
    const auto pos2 = notes.markdown.find("second feature added");
    REQUIRE(pos1 != std::string::npos);
    REQUIRE(pos2 != std::string::npos);
    // Most recent first: second then first in git log order
    REQUIRE(pos2 < pos1);
}

// ==========================================================================
//  Section 6: GetReleaseNoteKindLabel
// ==========================================================================

TEST_CASE("GetReleaseNoteKindLabel: returns correct labels", "[labels]") {
    REQUIRE(std::string(GetReleaseNoteKindLabel(ReleaseNoteKind::Feat)) ==
            "Features");
    REQUIRE(std::string(GetReleaseNoteKindLabel(ReleaseNoteKind::Fix)) ==
            "Bug Fixes");
    REQUIRE(std::string(GetReleaseNoteKindLabel(ReleaseNoteKind::Perf)) ==
            "Performance");
    REQUIRE(std::string(GetReleaseNoteKindLabel(ReleaseNoteKind::Docs)) ==
            "Documentation");
    REQUIRE(std::string(GetReleaseNoteKindLabel(ReleaseNoteKind::Breaking)) ==
            "Breaking Changes");
    REQUIRE(std::string(GetReleaseNoteKindLabel(ReleaseNoteKind::Unknown)) ==
            "Miscellaneous");
}
