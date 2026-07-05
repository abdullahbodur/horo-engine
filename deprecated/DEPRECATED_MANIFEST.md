# Deprecated Legacy Source

This directory contains the legacy pre-restructure source tree moved out of the
repository root so the new desired project layout can be introduced gradually.

Purpose:

- reference old implementation while migrating modules into the new layout
- preserve C++ source, assets, scripts, tests, CMake modules, and vendor code
- avoid keeping duplicate active source trees at the root

Rules:

- do not develop new systems in this folder
- do not include this folder in CMake target discovery
- do not package this folder into releases
- migrate code out in reviewed module-sized units: public headers, private
  implementation, CMake wiring, tests, docs, and asset references together

Local-only/generated folders were not moved here:

- `.git/`
- generated build directories (`build/`, `cmake-build-debug/`)
- local virtual environments and caches (`venv/`, `.pytest_cache/`, `__pycache__/`)
- local IDE/session folders (`.idea/`, `.vscode/`, `.cursor/`, `.hermes/`, `.openclaw/`)
- transient output (`bw-output/`, `.DS_Store`)
