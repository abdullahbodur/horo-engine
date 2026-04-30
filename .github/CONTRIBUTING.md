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
