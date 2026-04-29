# README Showcase Redesign — Design Spec

**Date:** 2026-04-29  
**Scope:** Upgrade README from developer reference to open-source showcase, attract contributors and adopters.  
**Audience:** Broader open-source community — contributors, curious developers, potential adopters.

---

## Goals

- Make project health immediately visible via a badges row
- Communicate scope and maturity at a glance (feature status table)
- Provide a visual landing spot for future screenshots
- Surface contribution entry points with a proper CONTRIBUTING.md
- Fix missing repo metadata (description, topics, LICENSE)

## Out of Scope

- Animated GIFs or editor recordings (blocked on screenshot capture session)
- Full API reference docs
- Wiki or GitHub Pages setup

---

## Section 1: Badges Row

**Location:** Directly under the `# Horo Engine` title, before the tagline paragraph.

**Badges (in order):**

| Badge | Service | Config |
|---|---|---|
| CI | GitHub Actions `ci.yml` on `main` | `https://github.com/abdullahbodur/horo-engine/actions/workflows/ci.yml/badge.svg` |
| Quality Gate | SonarCloud | project key `docktail_horo-engine`, org `docktail` |
| Coverage | SonarCloud | metric `coverage` |
| Bugs | SonarCloud | metric `bugs` |
| Code Smells | SonarCloud | metric `code_smells` |
| Language | shields.io static | `C++20`, blue, cplusplus logo |
| License | shields.io | `MIT`, links to `LICENSE` |
| Platforms | shields.io static | `Linux \| macOS \| Windows`, lightgrey |
| Release | shields.io GitHub release | latest tag, links to releases page |

All badges link to their respective dashboards or release pages.

## Section 2: Hero Image Placeholder

**Location:** New `## Screenshots` section placed immediately after the badges row.

**Content:**
```markdown
## Screenshots

<!-- Drop editor/renderer screenshots here once captured -->
<!-- Example: ![Editor overview](docs/screenshots/editor.png) -->
```

**Directory:** Create `docs/screenshots/` with a `.gitkeep` so the path is ready when screenshots are taken.

## Section 3: Feature Status Table

**Location:** Replace the existing `## High-Level Features` bullet list.

The table signals maturity and active development areas without requiring readers to dig into open PRs or code.

| Module | Status | Notes |
|---|---|---|
| Core (ECS, scene, serialization) | ✅ Production | |
| OpenGL renderer | ✅ Production | |
| Asset import pipeline | ✅ Production | |
| Unit test suite (Catch2) | ✅ Production | |
| UI automation tests | ✅ Production | Launcher + editor flows |
| MCP server (editor AI tooling) | ✅ Production | HTTP endpoint, opt-in |
| Vulkan renderer | 🔧 In progress | Not parity-complete yet |
| Backend resource factory API | 🔧 In progress | Active PRs |
| Physics | 🔧 In progress | Module scaffolded |
| GI / reflections | 📋 Planned | Architecture defined |

## Section 4: Architecture Diagram

**Location:** New `## Architecture` section, placed after the feature table and before Quick Start.

A compact ASCII diagram showing module ownership and data flow:

```
┌─────────────────────────────────────────────┐
│                  Launcher                   │
│          (editor shell + main loop)         │
└──────────────────┬──────────────────────────┘
                   │
          ┌────────▼────────┐
          │     Editor      │◄──── MCP server
          │  (scene / UI)   │
          └────────┬────────┘
                   │
          ┌────────▼────────┐
          │      Scene      │
          │  (ECS runtime)  │
          └──┬──────────┬───┘
             │          │
    ┌────────▼───┐  ┌───▼────────┐
    │  Renderer  │  │   Core     │
    │ (GL/Vulkan)│  │(math/input │
    └────────────┘  │ /physics)  │
                    └────────────┘
```

## Section 5: CONTRIBUTING.md

**File:** `.github/CONTRIBUTING.md` (GitHub auto-links this from the PR/issue UI)

**Contents:**
- Prerequisites (CMake, compiler, Ninja)
- Getting started (clone, build, test)
- Git hooks setup (`pre-commit install`)
- Branch naming convention (`feat/`, `fix/`, `docs/`, `chore/`, `test/`)
- Commit message convention (Conventional Commits, imperative mood)
- PR checklist: tests pass, format-check clean, one coherent intent per PR
- Code style notes (pointer to AGENTS.md)
- Where to ask questions (GitHub Discussions or Issues)

**README update:** Replace the inline 5-bullet Contributing section with a two-line summary linking to `CONTRIBUTING.md`.

## Section 6: GitHub Repo Metadata

Updated via GitHub API (not a file change):

- **Description:** `Open-source C++20 game engine — clarity, embeddability, fast iteration`
- **Topics:** `game-engine`, `cpp`, `cpp20`, `cmake`, `opengl`, `vulkan`, `ecs`, `game-development`, `cross-platform`, `editor`
- **License:** Add `LICENSE` file (MIT) to repo root; GitHub auto-detects it

## Section 7: MIT LICENSE File

Standard MIT license text with `Copyright (c) 2026 Abdullah Bodur`.

---

## Delivery

All changes delivered as a single PR targeting `main`:
- `README.md` updated
- `LICENSE` added
- `.github/CONTRIBUTING.md` added
- `docs/screenshots/.gitkeep` added
- `docs/superpowers/specs/` (this spec)
- GitHub repo metadata updated via API at implementation time
