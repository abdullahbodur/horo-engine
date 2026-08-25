# Header Visibility And Ownership

## Purpose

This document defines the enforceable C++ header boundary for Horo's production
targets. It preserves stable `#include <Horo/...>` spelling without allowing a
consumer of one module to discover every header in the repository.

## Classifications

Every header has exactly one classification:

| Classification | Location | Visibility |
|---|---|---|
| SDK/public | `include/Horo/` | The owning target and consumers that link it. |
| Internal-shared | Owning module source tree | Only explicitly named internal targets; never installed or transitively exposed by a public target. |
| Target-private | Owning module source tree | The target implementation only. This is the default for `src/` headers. |

Public placement is a compatibility commitment, not merely a convenient include
path. Moving a source header into `include/Horo/` requires a stable owner, a narrow
contract, Doxygen documentation, migration notes, and consumer coverage.

## Build-Tree Contract

`cmake/HoroPublicHeaderOwnership.cmake` assigns each public header to one real
production target. `cmake/HoroTargetBoundaries.cmake` materializes a separate
include view under `build/target-includes/<target>/public` and places only the
owning target's registered headers in that view.

Production targets publish their own view with a build interface. Their declared
`PUBLIC` dependencies publish additional views transitively. Production usage
requirements must not contain the repository-wide `include/` or `src/` roots.
Implementations may read source headers privately, but that path is not inherited
by consumers.

Configure is the first enforcement gate:

- an unowned public header is rejected;
- duplicate ownership is rejected;
- a registered path that does not exist is rejected;
- broad source/public include roots are removed from target usage requirements.

With testing enabled, CMake generates one isolated translation unit for every
registered public header. Each generated consumer links only the owning target,
so missing public dependencies or private-header leaks fail during compilation.

## Change Procedure

When adding or moving a public header:

1. Identify the real target that owns the contract.
2. Register the header under that target in
   `cmake/HoroPublicHeaderOwnership.cmake`.
3. Declare every dependency needed by the header as a target `PUBLIC` dependency.
4. Keep backend, GUI, platform-native, and third-party implementation types out
   of the contract unless the owning architecture explicitly permits them.
5. Build the generated public-header consumer target and every affected real
   consumer.
6. Record caller migration when ownership or include spelling changes.

If another production target needs a header currently under `src/`, do not expose
the source root. Either promote a deliberately stable contract to `include/Horo/`
or create a narrow non-installed internal interface with explicit consumers.

## ARC-001.2 Migration Notes

The initial boundary migration keeps all existing `Horo/...` include spellings.
No caller source rewrite is required. The observable change is intentional:
linking an unrelated Horo target no longer makes every public header available,
and linking EditorModel, EditorServices, Gui, InputSdl, or viewport targets no
longer exports the repository `src/` tree.

Callers that previously compiled through accidental include fan-out must link the
actual owning target. White-box tests that need implementation details must use a
narrow test-private include path or an explicit internal interface rather than
depending on production transitivity.

Legacy editor white-box tests use the non-installed `HoroEditorTestInternals`
interface as an explicit migration boundary. It is test-only and may expose the
source root to its listed consumers while their historical `editor/...` include
spellings remain. New tests should prefer a narrower test-private include path;
do not link this interface from production or SDK examples.

`HoroGui` currently exposes Dear ImGui types in several established public
headers, so `HoroThirdParty::ImGui` remains a truthful public usage requirement.
It may become private only after those signatures migrate to Horo-owned types.
`ProjectAssetImportCommitter` remains target-private behind an out-of-line
`AssetImportModal` destructor; do not reintroduce its `src/` include in the public
modal header.
