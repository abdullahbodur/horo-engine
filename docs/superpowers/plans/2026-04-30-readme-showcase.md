# README Showcase Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the Horo Engine README from a plain developer reference to an open-source showcase with badges, feature status table, architecture diagram, screenshots placeholder, CONTRIBUTING.md, MIT license, and updated GitHub repo metadata.

**Architecture:** All file changes land on the existing `docs/readme-showcase` branch and are delivered as a single PR to `main`. GitHub repo metadata (description, topics) is updated via the GitHub API at the end. No source code is touched.

**Tech Stack:** Markdown, shields.io badges, SonarCloud badges, GitHub Actions badge, GitHub REST API (`gh` CLI)

---

### Task 1: Add MIT LICENSE file

**Files:**
- Create: `LICENSE`

- [ ] **Step 1: Create the LICENSE file**

Create `LICENSE` in the repo root with this exact content:

```
MIT License

Copyright (c) 2026 Abdullah Bodur

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 2: Verify the file exists**

```bash
head -3 LICENSE
```

Expected output:
```
MIT License

Copyright (c) 2026 Abdullah Bodur
```

- [ ] **Step 3: Commit**

```bash
git add LICENSE
git commit -m "chore: add MIT LICENSE"
```

---

### Task 2: Add badges row to README

**Files:**
- Modify: `README.md` (lines 1–2, insert badges after title line)

- [ ] **Step 1: Insert the badges block**

Replace the content between `# Horo Engine` and `Horo Engine is an open-source…` so the top of `README.md` looks exactly like this:

```markdown
# Horo Engine

[![CI](https://github.com/abdullahbodur/horo-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/abdullahbodur/horo-engine/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=docktail_horo-engine&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=docktail_horo-engine)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=docktail_horo-engine&metric=coverage)](https://sonarcloud.io/summary/new_code?id=docktail_horo-engine)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=docktail_horo-engine&metric=bugs)](https://sonarcloud.io/summary/new_code?id=docktail_horo-engine)
[![Code Smells](https://sonarcloud.io/api/project_badges/measure?project=docktail_horo-engine&metric=code_smells)](https://sonarcloud.io/summary/new_code?id=docktail_horo-engine)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat&logo=cplusplus)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey)
[![Release](https://img.shields.io/github/v/release/abdullahbodur/horo-engine)](https://github.com/abdullahbodur/horo-engine/releases)

Horo Engine is an open-source C++20 game engine focused on **clarity, embeddability, and fast iteration**.
```

- [ ] **Step 2: Verify badge lines are present**

```bash
head -15 README.md
```

Expected: title on line 1, blank line 2, 9 badge lines 3–11, blank line 12, tagline on line 13.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): add badges row"
```

---

### Task 3: Add screenshots placeholder section

**Files:**
- Modify: `README.md` (insert section after badges/tagline block)
- Create: `docs/screenshots/.gitkeep`

- [ ] **Step 1: Create the screenshots directory**

```bash
mkdir -p docs/screenshots
touch docs/screenshots/.gitkeep
```

- [ ] **Step 2: Insert the Screenshots section into README**

Add the following block immediately after the tagline paragraph (after `...and fast iteration.`) and before `## Use Cases`:

```markdown

## Screenshots

<!-- Drop editor/renderer screenshots here once captured. -->
<!-- Example: ![Editor overview](docs/screenshots/editor.png) -->

```

- [ ] **Step 3: Verify the section appears in the right place**

```bash
grep -n "Screenshots\|Use Cases" README.md
```

Expected: `Screenshots` line number is lower (earlier) than `Use Cases` line number.

- [ ] **Step 4: Commit**

```bash
git add README.md docs/screenshots/.gitkeep
git commit -m "docs(readme): add screenshots placeholder section"
```

---

### Task 4: Replace High-Level Features with feature status table

**Files:**
- Modify: `README.md` — replace `## High-Level Features` and `## Current Renderer Status` sections

- [ ] **Step 1: Replace both sections**

Remove the existing `## High-Level Features` bullet list and `## Current Renderer Status` bullet list. Replace them with a single `## Features` section:

```markdown
## Features

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

```

- [ ] **Step 2: Verify old sections are gone and new table is present**

```bash
grep -n "High-Level Features\|Current Renderer Status\|## Features" README.md
```

Expected: only `## Features` is found. The other two should not appear.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): replace feature bullets with status table"
```

---

### Task 5: Add architecture diagram

**Files:**
- Modify: `README.md` — insert new `## Architecture` section

- [ ] **Step 1: Insert the Architecture section**

Add the following block after `## Features` (after the feature table) and before `## Quick Start`:

```markdown
## Architecture

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
    ┌────────▼───┐  ┌───▼────────────┐
    │  Renderer  │  │      Core      │
    │ (GL/Vulkan)│  │ math · input   │
    └────────────┘  │   physics      │
                    └────────────────┘
```

See [docs/architecture/README.md](./docs/architecture/README.md) for full module documentation.

```

- [ ] **Step 2: Verify the section is present**

```bash
grep -n "## Architecture\|## Quick Start" README.md
```

Expected: `## Architecture` appears before `## Quick Start`.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): add architecture diagram"
```

---

### Task 6: Create CONTRIBUTING.md

**Files:**
- Create: `.github/CONTRIBUTING.md`

- [ ] **Step 1: Create the file**

Create `.github/CONTRIBUTING.md` with this content:

```markdown
# Contributing to Horo Engine

Thank you for your interest in contributing. This document covers everything you need to get started.

## Prerequisites

- CMake 3.25+
- C++20 compiler: Clang 15+, GCC 13+, or MSVC 2022
- Ninja (Linux/macOS) or Visual Studio 2022 (Windows)
- Python 3 (for pre-commit hooks)

Linux packages:
```bash
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev
```

## Getting Started

```bash
git clone https://github.com/abdullahbodur/horo-engine
cd horo-engine
pre-commit install   # install git hooks
make                 # debug build
make test            # run unit tests
make format-check    # verify formatting
```

## Branch Naming

| Prefix | Use for |
|---|---|
| `feat/` | New features |
| `fix/` | Bug fixes |
| `docs/` | Documentation only |
| `chore/` | Build, CI, tooling |
| `test/` | Test-only changes |
| `refactor/` | Refactors with no behaviour change |

Example: `feat/vulkan-texture-sampler`

## Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/) in imperative mood:

```
feat(renderer): add texture sampler abstraction
fix(core): handle empty ProjectPath init
docs(mcp): document endpoint configuration
```

Rules:
- Subject line ≤ 72 characters
- One coherent intent per commit
- No `wip`, `misc`, `update`, or `fix stuff` subjects

## Pull Requests

Before opening a PR:

- [ ] `make test` passes
- [ ] `make format-check` passes (run `make format` to auto-fix)
- [ ] Each commit has a focused, descriptive message
- [ ] PR description explains *why*, not just *what*

PRs targeting `main` require CI to pass on all three platforms (Linux, macOS, Windows) and a review from a maintainer.

## Code Style

See [AGENTS.md](../AGENTS.md) for the full style guide and high-risk area notes.

Key points:
- Prefer explicit types, RAII, and narrow interfaces
- Use `const` aggressively
- No magic strings or loose flag combinations
- Comments explain invariants and ownership — not what the code does

## Questions

Open a [GitHub Discussion](https://github.com/abdullahbodur/horo-engine/discussions) or a [GitHub Issue](https://github.com/abdullahbodur/horo-engine/issues) if you are unsure where to start.
```

- [ ] **Step 2: Verify the file exists and has expected sections**

```bash
grep "^## " .github/CONTRIBUTING.md
```

Expected output:
```
## Prerequisites
## Getting Started
## Branch Naming
## Commit Messages
## Pull Requests
## Code Style
## Questions
```

- [ ] **Step 3: Commit**

```bash
git add .github/CONTRIBUTING.md
git commit -m "docs: add CONTRIBUTING.md"
```

---

### Task 7: Update Contributing section in README

**Files:**
- Modify: `README.md` — replace inline Contributing bullet list

- [ ] **Step 1: Replace the Contributing section**

Find and replace the entire `## Contributing` section (the numbered list) with:

```markdown
## Contributing

See [CONTRIBUTING.md](.github/CONTRIBUTING.md) for setup, branch naming, commit conventions, and the PR checklist.
```

- [ ] **Step 2: Verify the old numbered list is gone**

```bash
grep -n "Fork and create\|pre-commit install\|CONTRIBUTING" README.md
```

Expected: only the `CONTRIBUTING.md` link line appears. `Fork and create` and `pre-commit install` should not be present.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): replace inline contributing steps with CONTRIBUTING.md link"
```

---

### Task 8: Update the License section in README

**Files:**
- Modify: `README.md` — update the License section

- [ ] **Step 1: Replace the License section**

Find and replace the `## License` section with:

```markdown
## License

[MIT](LICENSE) — source code in this repository. See `vendor/` subdirectories for third-party licenses.
```

- [ ] **Step 2: Verify**

```bash
grep -A2 "^## License" README.md
```

Expected:
```
## License

[MIT](LICENSE) — source code in this repository. See `vendor/` subdirectories for third-party licenses.
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): link to LICENSE file in License section"
```

---

### Task 9: Update GitHub repo metadata via API

No files modified — GitHub API calls only.

- [ ] **Step 1: Set repo description and topics**

```bash
gh api --hostname github.com --method PATCH repos/abdullahbodur/horo-engine \
  -f description="Open-source C++20 game engine — clarity, embeddability, fast iteration"

gh api --hostname github.com --method PUT repos/abdullahbodur/horo-engine/topics \
  -f "names[]=game-engine" \
  -f "names[]=cpp" \
  -f "names[]=cpp20" \
  -f "names[]=cmake" \
  -f "names[]=opengl" \
  -f "names[]=vulkan" \
  -f "names[]=ecs" \
  -f "names[]=game-development" \
  -f "names[]=cross-platform" \
  -f "names[]=editor"
```

- [ ] **Step 2: Verify description and topics are set**

```bash
gh api --hostname github.com repos/abdullahbodur/horo-engine \
  --jq '{description: .description, topics: .topics}'
```

Expected:
```json
{
  "description": "Open-source C++20 game engine — clarity, embeddability, fast iteration",
  "topics": ["cmake","cpp","cpp20","cross-platform","ecs","editor","game-development","game-engine","opengl","vulkan"]
}
```

---

### Task 10: Open PR

- [ ] **Step 1: Verify you are on the right branch**

```bash
git branch --show-current
```

Expected: `docs/readme-showcase`

- [ ] **Step 2: Push branch**

```bash
git push origin docs/readme-showcase
```

- [ ] **Step 3: Open PR**

```bash
gh pr create \
  --hostname github.com \
  --repo abdullahbodur/horo-engine \
  --base main \
  --head docs/readme-showcase \
  --title "docs: README showcase — badges, status table, architecture, CONTRIBUTING, LICENSE" \
  --body "## What

Upgrades README from a plain developer reference to an open-source showcase.

## Changes

- **Badges row**: CI, SonarCloud quality gate/coverage/bugs/code smells, C++20, MIT, platforms, latest release
- **Screenshots section**: placeholder ready for when screenshots are captured
- **Feature status table**: replaces High-Level Features bullets with a ✅/🔧/📋 module table
- **Architecture diagram**: ASCII module dependency overview
- **CONTRIBUTING.md**: prerequisites, branch naming, commit convention, PR checklist, code style notes
- **LICENSE**: MIT
- **GitHub repo metadata**: description and topics updated via API

## Notes

No source code changed. CI should pass (docs + non-compiled files only)."
```

- [ ] **Step 4: Verify PR was created**

```bash
gh pr view --hostname github.com --repo abdullahbodur/horo-engine docs/readme-showcase
```

Expected: PR URL and title are shown.
