# Release Notes Flow — "What's New" in the Welcome Screen

## Overview

The "WHAT'S NEW" section of the HoroEditor Welcome Screen is populated at
**build time** from `CHANGELOG.md`. No file I/O occurs at runtime; the data is
compiled directly into the binary as `constexpr` C++ literals.

```
CHANGELOG.md                      CMake configure / incremental build
────────────────                  ──────────────────────────────────────
## [0.2.0] — 2026-07-10     →    scripts/parse_changelog.py
  ### Added                       │
  - New Project wizard ...        │  parse up to 2 ## [version] entries
                                  ↓
                            build/generated/GeneratedBuildInfo.h
                            ┌──────────────────────────────────────────┐
                            │ namespace Horo::Generated {              │
                            │   inline constexpr WhatsNewEntry         │
                            │     kWhatsNewEntries[2] = { ... };       │
                            │ }                                        │
                            └──────────────────────────────────────────┘
                                         ↓
                            WelcomeScreen.cpp — BuildViewModel()
                            WelcomeScreenGui.cpp — DrawNewsCard loop
```

---

## CHANGELOG.md Format Contract

The file lives at the repository root and follows
[Keep a Changelog 1.1](https://keepachangelog.com/en/1.1.0/).

```markdown
## [x.y.z] — YYYY-MM-DD ← required: version header

### Added ← optional: sub-section

- First bullet used as card body. ← first bullet becomes the card body
- Further bullets are ignored.

## [x.y.z-1] — YYYY-MM-DD

### Fixed

- Another entry.
```

**Parser rules (`scripts/parse_changelog.py`):**

| Rule             | Detail                                                                           |
| ---------------- | -------------------------------------------------------------------------------- |
| Max entries      | 2 (most recent first)                                                            |
| `[Unreleased]`   | Skipped — no shipped content                                                     |
| `tag` field      | Always `"Release Notes"`                                                         |
| `title` field    | `"vX.Y.Z — Month YYYY"` (date reformatted for display)                           |
| `body` field     | First `- bullet` under any `###` sub-section, max 120 chars (truncated with `…`) |
| No entries found | Fallback: `{"Release Notes", "No releases yet", ""}` × 2                         |

---

## Files Involved

| File                                              | Role                                                        |
| ------------------------------------------------- | ----------------------------------------------------------- |
| `CHANGELOG.md`                                    | Source of truth — edited by maintainers                     |
| `scripts/parse_changelog.py`                      | Parser — run by CMake at configure time                     |
| `cmake/GenerateBuildInfo.cmake`                   | CMake module — drives the script, sets `HORO_GENERATED_DIR` |
| `build/generated/GeneratedBuildInfo.h`            | **Generated** — `.gitignore`'d, never committed             |
| `include/Horo/Editor/WelcomeScreen.h`             | `WelcomeViewModel::whatsNew` array field                    |
| `src/editor/screens/WelcomeScreen.cpp`            | `BuildViewModel()` — copies data from generated header      |
| `src/editor/screens/welcome/WelcomeScreenGui.cpp` | Renders the two cards from `viewModel.whatsNew`             |

---

## Build Flow

### First-time configure

```bash
cmake -B build/skeleton -S .
# → GenerateBuildInfo.cmake runs parse_changelog.py
# → build/generated/GeneratedBuildInfo.h is written
cmake --build build/skeleton
```

### After editing CHANGELOG.md

```bash
cmake --build build/skeleton
# → add_custom_command detects CHANGELOG.md is newer than GeneratedBuildInfo.h
# → parse_changelog.py reruns automatically
# → only affected translation units recompile
```

No reconfigure (`cmake -B`) is needed after editing `CHANGELOG.md` — the
`add_custom_command` dependency in `GenerateBuildInfo.cmake` handles it.

---

## Adding a New Release Entry

1. Open `CHANGELOG.md`.
2. Add a new `## [x.y.z] — YYYY-MM-DD` section **at the top** (before previous entries).
3. Add at least one `### Added` / `### Changed` / `### Fixed` bullet.
4. Run `cmake --build build/skeleton --target HoroEditor`.
5. Launch `HoroEditor` — the Welcome Screen "WHAT'S NEW" section shows the two newest entries.

---

## Python Dependency

`parse_changelog.py` requires **Python 3** (no third-party packages).

CMake uses `find_package(Python3 QUIET COMPONENTS Interpreter)`. If Python 3 is
not found:

- A warning is printed during configure.
- A fallback `GeneratedBuildInfo.h` with `"No releases yet"` entries is written.
- The build proceeds normally.

CI environments that run `cmake --build` are expected to have Python 3 available.

---

## Security & Distribution Notes

- `GeneratedBuildInfo.h` is in `build/generated/` which is excluded from source
  control via `.gitignore`.
- The generated file contains only static strings parsed from the repository's
  own `CHANGELOG.md` — no network access, no external data.
- Packaged/distributed binaries embed the strings at compile time; the
  `CHANGELOG.md` source file is **not** bundled with the application.
