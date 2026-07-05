# P6.4: SBOM Generation — Technical Plan

**JIRA:** HORO-32  
**Status:** Spec / planning  
**Version:** 0.0.1 (matches engine version)

---

## 1. Objective

Generate an SPDX 2.3 JSON Software Bill of Materials (SBOM) for each Horo Engine
release. The SBOM enumerates every third-party dependency — vendored source,
FetchContent-downloaded libraries, and optional feature-gated dependencies — with
precise versions, licenses, and origin URLs.

The SBOM is emitted as a build artifact attached to release drafts, consumed by
downstream supply-chain scanners, and referenced in the engine's own compliance
documentation.

---

## 2. Dependency Inventory

The engine currently carries **15 distinct third-party components** across three
categories. This inventory was extracted from `CMakeLists.txt`,
`vendor/CMakeLists.txt`, and `vendor/README.md` on 2026-05-25.

### 2.1 Committed vendor sources (5)

| Component | Version | License | Origin | Notes |
|-----------|---------|---------|--------|-------|
| glad | (generated) | MIT / Khronos | `vendor/glad/` | OpenGL 4.1 Core loader, pre-generated. No upstream tag; pin generation date. |
| glm | (header snapshot) | MIT / Happy Bunny | `vendor/glm/glm.hpp` | Header-only math. No version macro; pin commit hash via `git subtree` or manual record. |
| stb_image | (header snapshot) | MIT / Public Domain | `vendor/stb/` | Header-only image decode (`stb_image.h`, `stb_dxt.h`). No version macro; track commit hash. |
| ufbx | v0.21.3 | MIT / Public Domain | `vendor/ufbx/` | FBX parser amalgamation. Version recorded in `vendor/README.md` and inline comment. |
| bc7enc | (committed snapshot) | MIT / Public Domain | `vendor/bc7enc/` | BC7 texture encoder. No version macro; pin commit hash. |

**Challenge:** glad, glm, stb, and bc7enc have no embedded version identifier.
The SBOM generator must either:
- Read a manually maintained version manifest (`vendor/VERSIONS.json`), or
- Extract commit metadata from `git subtree` or `.gitattributes`, or
- Require a manual version bump in vendor docs when snapshots are updated.

*Recommendation (this plan):* Introduce `vendor/VERSIONS.json` as the single source of
truth, with a pre-commit hook or CI check that validates it against the actual
snapshot contents.

### 2.2 FetchContent dependencies (8 active + 2 optional)

| Component | Version | License | Origin | Gated By |
|-----------|---------|---------|--------|----------|
| glfw | 3.4 | zlib/libpng | `https://github.com/glfw/glfw.git` | Always |
| imgui | v1.91.6 | MIT | `https://github.com/ocornut/imgui.git` | Always |
| nlohmann_json | v3.11.3 | MIT | `https://github.com/nlohmann/json.git` | Always |
| tinygltf | v2.9.3 | MIT | `https://github.com/syoyo/tinygltf.git` | Always |
| xxhash | v0.8.3 | BSD-2-Clause | `https://github.com/Cyan4973/xxHash.git` | Always |
| lz4 | v1.10.0 | BSD-2-Clause | `https://github.com/lz4/lz4.git` | Always |
| glslang | 15.1.0 | BSD-3-Clause / Khronos | `https://github.com/KhronosGroup/glslang.git` | `HORO_ENGINE_ENABLE_SHADER_COOKER=ON` |
| Vulkan-Headers | vulkan-sdk-1.4.341.0 | Apache-2.0 / Khronos | `https://github.com/KhronosGroup/Vulkan-Headers.git` | `HORO_ENGINE_ENABLE_VULKAN=ON` |
| volk | 1.4.304 | MIT | `https://github.com/zeux/volk.git` | `HORO_ENGINE_ENABLE_VULKAN=ON` |
| imgui_test_engine | v1.91.6 | Dear ImGui Test Engine License | `https://github.com/ocornut/imgui_test_engine.git` | Tests only (not shipped) |

**Challenge:** FetchContent versions are embedded as `GIT_TAG` literals in
`vendor/CMakeLists.txt`. The SBOM generator can parse these directly via regex,
but it must also account for CMake cache overrides (`FETCHCONTENT_SOURCE_DIR_*`).

### 2.3 System dependencies (platform-specific)

| Dependency | Platforms | Notes |
|------------|-----------|-------|
| OpenGL | All | System-provided; not in SBOM package list (document in SBOM metadata) |
| X11 / Wayland | Linux | Transitive via GLFW; not tracked |
| Cocoa / AppKit | macOS | System framework |
| Win32 / COM | Windows | System API |

System dependencies are **not enumerated as SPDX packages**. They are mentioned
in the SBOM's `documentDescribes` commentary so compliance scanners understand
the platform baseline.

---

## 3. SPDX Output Format

### 3.1 Schema

SPDX 2.3 JSON, conforming to
[spdx/json-spec](https://github.com/spdx/spdx-spec/blob/development/v2.3.1/schemas/spdx-schema.json).

### 3.2 Document structure

```json
{
  "SPDXID": "SPDXRef-DOCUMENT",
  "spdxVersion": "SPDX-2.3",
  "creationInfo": {
    "created": "2026-05-25T12:00:00Z",
    "creators": [
      "Tool: HoroEngine-SBOM-Generator-0.0.1",
      "Organization: Horo Organization"
    ],
    "licenseListVersion": "3.23"
  },
  "name": "HoroEngine-v0.0.1",
  "dataLicense": "CC0-1.0",
  "documentNamespace": "https://github.com/abdullahbodur/horo-engine/sbom/HoroEngine-v0.0.1-<UUID>",
  "packages": [ /* ... */ ],
  "relationships": [ /* ... */ ]
}
```

### 3.3 Package entry (example)

```json
{
  "SPDXID": "SPDXRef-Package-glfw-3.4",
  "name": "glfw",
  "versionInfo": "3.4",
  "supplier": "Organization: GLFW Developers",
  "downloadLocation": "https://github.com/glfw/glfw.git@3.4",
  "filesAnalyzed": false,
  "licenseConcluded": "Zlib",
  "licenseDeclared": "Zlib",
  "copyrightText": "Copyright (c) 2002-2006 Marcus Geelnard, 2006-2019 Camilla Löwy",
  "externalRefs": [
    {
      "referenceCategory": "PACKAGE-MANAGER",
      "referenceType": "purl",
      "referenceLocator": "pkg:github/glfw/glfw@3.4"
    }
  ]
}
```

### 3.4 Relationships

Top-level:

```
SPDXRef-DOCUMENT DESCRIBES SPDXRef-Package-HoroEngine
SPDXRef-Package-HoroEngine DEPENDS_ON SPDXRef-Package-glfw-3.4
SPDXRef-Package-HoroEngine DEPENDS_ON SPDXRef-Package-imgui-1.91.6
...
```

For optional dependencies, relationships are conditional:

```json
{
  "spdxElementId": "SPDXRef-Package-HoroEngine",
  "relationshipType": "OPTIONAL_DEPENDENCY_OF",
  "relatedSpdxElement": "SPDXRef-Package-glslang-15.1.0",
  "comment": "Only when HORO_ENGINE_ENABLE_SHADER_COOKER=ON"
}
```

---

## 4. Generator Implementation

### 4.1 Approach: Python script + CI integration

A Python script at `scripts/generate-sbom.py` that:

1. Reads `vendor/CMakeLists.txt` to extract FetchContent declarations (GIT_TAG,
   GIT_REPOSITORY).
2. Reads `vendor/VERSIONS.json` (new) for committed vendor snapshots.
3. Reads `CMakeLists.txt` for the engine version (`HORO_ENGINE_VERSION`).
4. Resolves SPDX license identifiers via a built-in mapping table.
5. Emits `sbom.spdx.json` to the build artifact directory.
6. Accepts `--config` to include/exclude optional dependencies based on the
   active CMake preset flags.

### 4.2 New file: `vendor/VERSIONS.json`

Single source of truth for committed vendor snapshots:

```json
{
  "format_version": 1,
  "entries": [
    {
      "name": "glad",
      "version": "4.1-core-generated-2024-01",
      "license": "MIT",
      "origin": "https://glad.dav1d.de/",
      "notes": "OpenGL 4.1 Core loader. Regenerate via https://glad.dav1d.de/ with profile=core, api=gl=4.1."
    },
    {
      "name": "glm",
      "version": "0.9.9.8",
      "license": "MIT",
      "origin": "https://github.com/g-truc/glm",
      "pinned_commit": "<insert-actual-commit>",
      "notes": "Header-only math library. Snapshot from vendor/glm/."
    },
    {
      "name": "stb",
      "version": "2.29-stb_image-2.29-stb_dxt-1.0",
      "license": "MIT",
      "origin": "https://github.com/nothings/stb",
      "notes": "Header-only image libraries. stb_image.h v2.29, stb_dxt.h v1.0 (approx)."
    },
    {
      "name": "ufbx",
      "version": "0.21.3",
      "license": "MIT",
      "origin": "https://github.com/ufbx/ufbx",
      "pinned_commit": "83bc7cf"
    },
    {
      "name": "bc7enc",
      "version": "r1.0.5",
      "license": "MIT",
      "origin": "https://github.com/richgel999/bc7enc",
      "notes": "BC7 texture block encoder."
    }
  ]
}
```

### 4.3 License identifier mapping

The generator must translate human-readable license names to SPDX identifiers:

| Source | SPDX Identifier |
|--------|----------------|
| MIT | MIT |
| zlib/libpng | Zlib |
| BSD-2-Clause | BSD-2-Clause |
| BSD-3-Clause | BSD-3-Clause |
| Apache-2.0 | Apache-2.0 |
| Public Domain | LicenseRef-Horo-PublicDomain |
| Dear ImGui Test Engine License | LicenseRef-ImGuiTestEngine |

Non-standard licenses (Public Domain variants, Dear ImGui Test Engine License)
use `LicenseRef-` prefixed custom identifiers with a `comment` field explaining
the actual terms.

### 4.4 Namespace UUID

Each SBOM gets a deterministic UUID via UUIDv5 using the engine version and
dependency hash as the namespace seed. This ensures reproducible SBOMs for
identical builds.

---

## 5. CI Integration

### 5.1 Release pipeline (`release-binaries.yml`)

Add an SBOM generation step **after build, before artifact packaging**:

```yaml
- name: Generate SBOM
  run: python3 scripts/generate-sbom.py --output build/release/sbom.spdx.json

- name: Attach SBOM to release artifacts
  # sbom.spdx.json is included in the tarball/zip alongside HoroEditor binary
```

The SBOM accompanies the binary in the release tarball, making it available
to anyone who downloads the engine release.

### 5.2 CI validation (`ci.yml`)

Add a non-blocking check that:

1. Generates SBOM for a debug build.
2. Validates JSON schema.
3. Verifies all expected dependencies appear.
4. Fails if `vendor/VERSIONS.json` is stale (detected by checking for new
   FetchContent GIT_TAG values without corresponding VERSIONS entries).

### 5.3 Release draft attachment

Optionally upload SBOM as a separate release asset via `gh release upload`:

```yaml
- name: Upload SBOM to release
  env:
    GH_TOKEN: ${{ github.token }}
    RELEASE_TAG: ${{ github.event.release.tag_name || inputs.tag }}
  run: |
    gh release upload "$RELEASE_TAG" build/release/sbom.spdx.json --clobber
```

---

## 6. Limitations (documented in SBOM metadata)

### 6.1 Transitive dependencies

FetchContent dependencies may pull their own dependencies. For example:
- glslang fetches SPIRV-Tools, SPIRV-Headers
- tinygltf depends on nlohmann_json and stb (already tracked at top level)

The SBOM generator operates on the **top-level dependency list** only.
Transitive dependencies are noted in a document-level comment but not
individually enumerated. Rationale: full transitive resolution requires
a running CMake configure step and parsing the dependency graph from CMake's
internal state — outside scope for an offline script.

### 6.2 Build-time patch modifications

The engine applies runtime patches to some FetchContent sources (e.g., GLFW
`nsgl_context.m` NSOpenGLPFAAccelerated guard). These modifications are not
reflected in the SBOM version. The version reported is the upstream tag.

### 6.3 System package variance

On Linux, system packages (libwayland-dev, libx11-dev, etc.) are not enumerated.
The SBOM assumes a conforming platform baseline per the engine's documented
system requirements. Downstream consumers packaging for specific distributions
should supplement with distribution-specific SBOMs.

### 6.4 Test-only dependencies

`imgui_test_engine` is fetched at build time but is not a runtime dependency.
It is excluded from the release SBOM but could be included in a separate
"dev-SBOM" for development environment auditing.

---

## 7. Files Changed (implementation phase)

| File | Purpose |
|------|---------|
| `scripts/generate-sbom.py` | New: SBOM generation script |
| `vendor/VERSIONS.json` | New: committed vendor version manifest |
| `.github/workflows/release-binaries.yml` | Modify: add SBOM generation + upload step |
| `.github/workflows/ci.yml` | Modify: add SBOM validation check |
| `docs/sbom-generation-plan.md` | This document |
| `vendor/README.md` | Update: reference VERSIONS.json as version authority |

---

## 8. Verification Checklist

- [ ] `python3 scripts/generate-sbom.py` exits 0 and produces valid JSON.
- [ ] Output parses against `spdx-schema.json` (validate via `check-jsonschema` or `ajv`).
- [ ] Every "always-active" dependency appears in `packages[]`.
- [ ] `--config=release` includes glslang (when SHADER_COOKER=ON).
- [ ] `--config=release --no-shader-cooker` excludes glslang.
- [ ] `vendor/VERSIONS.json` entries match the actual committed files.
- [ ] Generated namespace UUID is deterministic for same inputs.
- [ ] SBOM attaches to release tarball in CI.
- [ ] `LicenseRef-` identifiers include explanatory comments.
- [ ] No PII, tokens, or secrets leak into document metadata.

---

## 9. Open Questions

1. **glm version**: The vendored glm snapshot has no version file. Should we
   pin a specific upstream tag (e.g., 0.9.9.8) or record the actual commit hash?
   *Leaning toward: record commit hash, derive human-readable version from the
   closest upstream tag.*

2. **SPDX namespace UUID format**: UUIDv5 (SHA-1 of engine-version + dep-hash)
   or random UUIDv4 per build? UUIDv5 is reproducible; UUIDv4 is simpler but
   non-deterministic. *Leaning toward UUIDv5 for audit trail consistency.*

3. **Should the SBOM include the engine itself as a package?** SPDX convention
   says yes (the document describes a primary package). The engine's own
   `HORO_ENGINE_VERSION` + MIT license would be the root package.

4. **vendor/VERSIONS.json maintenance**: Who updates it when vendored sources
   change? *Recommendation: the developer who updates the snapshot updates
   VERSIONS.json in the same commit. Pre-commit hook validates JSON schema.*
