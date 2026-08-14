# Extension Capability Roadmap

## Purpose

This document records the capability progression for Horo's extension, package,
SDK, gameplay-module, and renderer-component ecosystems. These stages describe
what the ecosystem can do; they are not GitHub release milestones and they do
not impose a single serial implementation order on engine workstreams.

GitHub product milestones answer which whole-product checkpoint requires a
ticket: Architecture Baseline, Engine Prototype, Developer Preview, Alpha,
Beta, 1.0, or Post-1.0. Project `Area` identifies the owning workstream,
`Roadmap Horizon` records planning proximity, and native `blocked by`
relationships define technical execution order.

## Capability Stages

### Trusted Local Extensions

A developer can author, validate, trust, enable, load, diagnose, and safely shut
down a local extension through the supported manifest and ABI contracts. The
host keeps package installation, trust, enablement, compatibility, loading, and
activation as separate states.

Primary initiatives: `ARC-001`, `EXT-001`, and the foundational part of
`SDK-001`.

### Headless Backend Modules

Trusted modules can contribute backend capabilities such as importers, cookers,
validators, pipeline steps, and toolchain providers. The same application
services work in editor, CLI, MCP, and headless compositions with host-owned
scheduling, cancellation, diagnostics, and shutdown.

Backend/library modules may also publish versioned typed services for approved
consumers. Imports are resolved by declared capability identity and version;
modules do not discover providers through globals or implicit native linking.

Primary initiative: `EXT-002`.

### GUI Extension Platform

Extensions can contribute supported editor surfaces through a restricted,
versioned UI boundary. Panels, commands, settings, inspectors, overlays, and
workspace state follow the editor design system, localization, capability, and
unload contracts.

The public GUI kit includes semantic theme tokens and standard component/form
primitives. Custom compositions remain possible through an explicit advanced
widget boundary, but they continue to use host-provided theme, DPI, input,
accessibility, and lifecycle contracts. A live theme change therefore updates
extension-owned presentation as well as built-in editor surfaces.

Primary initiative: `EXT-003`, supported by `SDK-001`.

### Script-Consumable Module APIs

Backend modules may optionally describe a language-neutral API that supported
scripting runtimes expose through validated bindings. Scripts receive scoped
capability handles and bounded value marshalling; they never receive native
pointers, host function tables, or ambient engine services. Provider updates,
disablement, reload, and shutdown revoke or refresh bindings at safe points.

Primary initiative: `EXT-005`, supported by `EXT-002`, `GAM-001`, and
`SDK-001`.

### Portable Packages And Marketplace

Packages have deterministic manifests, resolution, lockfiles, provenance,
restore, transactional lifecycle operations, and optional registry discovery.
Marketplace availability remains separate from local package correctness and is
not required for core project opening or build execution.

Primary initiatives: `PKG-001` and `EXT-004`.

### Runtime Ecosystems

Project-owned gameplay modules and independently distributed renderer
components use their own runtime-appropriate contracts. They reuse package,
trust, compatibility, diagnostics, and release infrastructure without being
forced through the generic editor-extension lifecycle or public ABI.

Primary initiatives: `GAM-001` and `RND-002`.

## Reference Module Templates

The public SDK maintains copyable examples under `examples/modules/` for the
minimum supported shapes:

- GUI-only module using semantic tokens and standard forms;
- backend/library-only module providing a typed service;
- script-consumable module with a language-neutral export descriptor;
- hybrid package combining backend, GUI, and script-facing modules without
  merging their lifecycle authorities.

Examples are executable compatibility fixtures as well as documentation. They
build and package in CI and demonstrate diagnostics, failure, cancellation, and
teardown in addition to the happy path.

## Planning Rule

The stages above may overlap. Editor, rendering, assets, gameplay, extensions,
packages, build, physics, audio, networking, platform, security,
observability, and release workstreams are expected to advance in parallel.
A stage name must never be used as a substitute for a product milestone or a
technical dependency.
