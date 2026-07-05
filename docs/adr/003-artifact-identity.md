# ADR-003: Artifact Identity, Checksum, and Manifest Model

- **Status**: Accepted
- **Date**: 2026-05-25
- **JIRA**: HORO-32
- **Supersedes**: None
- **Scope**: Release artifact naming, integrity verification, and archive manifest

## Context

A Horo Engine release consists of multiple artifacts produced by independent
CI jobs on different platforms. Each artifact must be:

1. **Uniquely identifiable**: A human can tell which platform, architecture,
   and version a given tarball/zip contains.
2. **Integrity-verifiable**: A consumer can confirm the artifact has not been
   corrupted or tampered with after it leaves CI.
3. **Self-describing at the archive level**: The `.horo` asset archive embedded
   in a release carries its own integrity hashes, enabling runtime verification
   without external metadata.

The engine has two layers of artifact identity: the *distribution artifact*
(the `.tar.gz` or `.zip` uploaded to GitHub Releases) and the *asset archive*
(the `assets.horo` file inside the distribution).

## Decision

### Layer 1: Distribution artifact naming

Each release binary follows the pattern:

```
HoroEngine-{os}-{arch}.{ext}
```

| OS | Architecture | Extension | Example |
|----|-------------|-----------|---------|
| `linux` | `x86_64` | `.tar.gz` | `HoroEngine-linux-x86_64.tar.gz` |
| `macos` | `arm64` | `.tar.gz` | `HoroEngine-macos-arm64.tar.gz` |
| `windows` | `x64` | `.zip` | `HoroEngine-windows-x64.zip` |

**Rationale**:
- Lowercase OS names match GitHub Actions runner conventions (`ubuntu-latest`,
  `macos-latest`, `windows-latest`).
- `x86_64` for Linux (matching `uname -m`), `arm64` for macOS (matching Apple
  Silicon convention), `x64` for Windows (matching MSVC target triple and
  common Windows shorthand).
- `.tar.gz` for Unix, `.zip` for Windows — matching the native archive format
  consumers expect on each platform.

Release artifacts are uploaded to the GitHub Release with `gh release upload
--clobber`, keyed by the release tag (e.g. `v0.0.1`).

### Layer 2: Asset archive (.horo) format and integrity

The `.horo` archive format (`core/archive/HoroFormat.h`) embeds its own
integrity verification at multiple levels:

#### Fixed header (32 bytes)
```
[H][O][R][O] | version (4B) | flags (4B) | toc_count (4B) | toc_offset (8B) | data_offset (8B)
```
The `magic` bytes (`HORO`) enable fast file-type detection. The `version`
field allows backward-incompatible format evolution.

#### Per-chunk CRC32
When `HoroArchiveFlags::HashCRC32` is set, each `TOCEntry` carries a 32-bit
CRC32 checksum of the uncompressed chunk data. This provides fast integrity
checking on extraction — consumers can verify CRC32 before decompression or
decryption, catching disk corruption or incomplete writes early.

#### Per-chunk SHA-256 (optional)
When `HoroArchiveFlags::HashSHA256` is set, a SHA-256 hash block follows the
TOC. Each entry corresponds to a chunk, providing cryptographic verification.
This is the preferred hash for release artifacts where tamper resistance
matters.

#### Key Check Value (KCV) for encrypted archives
When encryption is enabled, the `CryptoProvider` generates an 8-byte KCV
(the first 8 bytes of encrypting a 16-byte zero block with the configured
key). The KCV is stored in the archive header and allows consumers to verify
they have the correct decryption key before attempting to decrypt real data.

#### Archive versioning
The `kHoroVersion` constant (currently `1`) controls forward compatibility.
Readers must reject archives with a `version` greater than what they support.
Writers produce the latest version they support. This is a version-number
bump model, not a capability-negotiation model.

### Layer 3: Release output path convention

Editor and CLI releases produce game project artifacts in a predictable
directory structure:

```
{outputRoot}/{versionTag}_{os}_{arch}/
  {GameExecutable}
  assets.horo
  shaders/
    basic.frag
    basic.vert
```

The path is constructed by `ResolveJobOutputPath` in
`core/pipeline/ReleasePipeline.cpp`:

```cpp
std::format("{}/{}_{}_{}", outputRoot, versionTag, osLower, archLabel);
```

Example: `build/release/v0.0.1_macos_arm64/`

This convention ensures that:
- Multiple platform releases for the same version can coexist in the same
  output root without collision.
- The version tag and platform are visible in the path, not just in metadata.
- CI automation can glob for `v*_*_*` directories to find all release outputs.

### Layer 4: Build history manifest (non-cryptographic)

The `BuildHistoryEntry` struct and its JSON serialization
(`~/.horo/build_history.json`) provide a human-readable and
machine-parseable record of past builds:

```json
[
  {
    "version": "v0.0.1",
    "timestamp": "2026-05-25T14:30:00Z",
    "allSucceeded": true,
    "jobs": [
      {
        "os": "macOS",
        "arch": "arm64",
        "config": "Release",
        "status": "Success",
        "exitCode": 0,
        "outputPath": "/path/to/build/release/v0.0.1_macos_arm64",
        "error": "",
        "timestamp": "2026-05-25T14:35:00Z"
      }
    ]
  }
]
```

This is a convenience manifest for the editor UI (the Recent Runs card in the
Build Pipeline Modal), not a security boundary. It does not contain hashes or
signatures.

### Future: Distribution-level hashing

The current release pipeline does not yet generate SHA-256 checksums for the
distribution artifacts (`.tar.gz`, `.zip`).  This is a known gap.  When
implemented, checksums should be:

- Generated as a separate CI step after artifact upload.
- Stored alongside the artifact in the GitHub Release (e.g.
  `HoroEngine-macos-arm64.tar.gz.sha256`).
- Generated by the trusted CI runner, not by an external service.

This is deferred because GitHub already provides integrity for release
artifacts at the platform level (HTTPS + checksums on upload), and the `.horo`
internal hashing provides runtime verification. However, for distribution to
package managers or offline mirrors, external checksums will be required.

## Consequences

### Positive
- The `.horo` format is self-verifying: integrity checks do not require
  external files or network access.
- Release naming is predictable and machine-parseable.
- Build history provides editor UI convenience without leaking secrets.
- KCV enables fast password verification without decrypting real data.

### Negative
- No distribution-level checksums yet — consumers must trust GitHub's
  transport integrity.
- CRC32 is fast but not collision-resistant — it catches corruption, not
  deliberate tampering. SHA-256 (optional) provides the cryptographic
  guarantee but is not enabled by default.
- The `kHoroVersion` bump model means any backward-incompatible format change
  requires all readers to be updated simultaneously. There is no
  capability-negotiation handshake.

### Open questions
- Should SHA-256 be enabled by default for all release archives?
- Should distribution artifacts be signed (GPG / Sigstore / minisign) in
  addition to being hashed?
- Should the build history manifest be extended to include artifact hashes?

## Rejected Alternatives

### Platform-specific archive formats (`.dmg`, `.msi`, `.AppImage`)

**Rejected for now**: These would improve the end-user installation experience
but add significant per-platform packaging complexity and CI tooling
requirements. The current `.tar.gz`/`.zip` approach is universal and allows
the release pipeline to mature before adding platform-specific installers.

### Embed a JSON manifest inside the `.horo` archive

**Rejected because**: The TOC serves the same purpose (asset path → chunk
offset + hash mapping). Adding a separate JSON manifest would duplicate the
asset index and create consistency problems (manifest says one thing, TOC
says another). The TOC is the authoritative index.

### Use content-addressable naming (hash in filename)

**Rejected because**: While content-addressed naming (e.g.
`HoroEngine-<sha256>.tar.gz`) provides strong identity, it makes the release
page illegible to humans and complicates CI scripting (you don't know the
hash until the build completes). The current version-tagged naming is
sufficient for the project's scale.

### Omit per-chunk hashing and rely on a single archive-level hash

**Rejected because**: Per-chunk hashing enables partial verification —
consumers can verify individual assets without reading the entire archive.
This matters for large archives where a game might only need to verify a
single asset's integrity. The TOC structure supports this naturally.
