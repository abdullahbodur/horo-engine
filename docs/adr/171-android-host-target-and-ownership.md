# ADR-171: Android Host, Target and Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Android process entry, target/API/ABI baseline, platform and renderer ownership, lifecycle and packaging boundaries
- **Issue**: [PLT-001.1](https://github.com/abdullahbodur/horo-engine/issues/2257)
- **Jira**: [HORO-2196](https://horo-engine.atlassian.net/browse/HORO-2196)
- **Parent**: [PLT-001](https://github.com/abdullahbodur/horo-engine/issues/2256)
- **Related**: [ADR-004](004-cli-core-gui-boundary.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-031](031-vulkan-loader-platform-and-version-baseline.md), [ADR-033](033-presentation-and-display-ownership.md), [ADR-158](158-openxr-loader-backend-packaging-and-host-composition.md)
- **Normative documents**: [Android Platform Host](../architecture/foundation/android-platform-host.md), [Platform Abstraction](../architecture/foundation/platform-abstraction.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Release Architecture](../architecture/release/release.md)

## Context

Android work cannot start safely from an unspecified "mobile target." The process entry,
API and ABI floors, graphics baseline, replaceable native-window lifetime, renderer
boundary, optional XR composition, build topology and package evidence all affect which
module owns state and when it may be destroyed. Leaving those decisions to individual
features would create competing activity hosts, native-handle leakage into portable APIs,
and packages whose support claims cannot be reproduced.

Android currently recommends GameActivity for new C/C++ games and describes it as the
replacement for NativeActivity. The NDK builds and packages ABIs independently. Android's
native Vulkan guidance recommends Vulkan 1.1 on 64-bit Android 10 devices and defines an
Android Baseline profile. Google Play target-level policy advances independently of engine
architecture. These external policies are inputs to a pinned product profile, not ambient
runtime discovery.

## Decision

### 1. One application host owns Android composition

The Android product executable owns a private `AndroidHost` composition root. Its single
interactive entry integration is pinned AndroidX GameActivity. NativeActivity and a custom
second activity/native loop are not supported alternatives for the initial target.

```text
Android product application
  -> private AndroidHost (GameActivity glue, process and lifecycle serialization)
  -> HoroEngine::PlatformAndroid (private Android platform services)
  -> portable Horo application/runtime
  -> HoroEngine::RenderVulkanAndroid or RenderNull for admitted tests
  -> optional HoroEngine::XROpenXR
```

Descriptors remain inert. The product host selects and wires concrete adapters before
activation; Runtime, Renderer, XR and feature modules neither discover an Activity nor
instantiate Android services.

### 2. The initial target tuple is explicit

| Dimension | Initial decision |
|---|---|
| Minimum Android API | API 29 (Android 10) |
| General Google Play compile/target level | API 36 at this decision date; release policy may advance it without lowering the engine floor |
| Production CPU ABI | `arm64-v8a` |
| Development/emulator ABI | `x86_64`, not distribution-qualified until a product profile says otherwise |
| Unsupported ABIs | `armeabi-v7a` and `x86` |
| Interactive graphics | Vulkan 1.1 plus the Android Baseline profile's required operations and effective capabilities |
| Optional graphics tier | Vulkan 1.3 only after separate device/profile qualification |
| Headless/test graphics | RenderNull through an explicit no-presentation profile |
| Initially unsupported graphics | OpenGL ES and desktop OpenGL |

Every release profile freezes its actual minimum, compile and target API levels, ABI,
renderer, device class and required features. A store requirement may force a newer target
or compile level, but does not silently change `minSdk`, engine capability semantics or
another distribution profile. Android XR and non-Play distributions declare separate
tuples and evidence.

### 3. Android Vulkan is a sibling backend specialization

The Android Vulkan target is a sibling of desktop Vulkan behind Horo-owned contracts; it
does not inherit a desktop host or expose `Vk*`, `ANativeWindow*` or JNI types publicly.
ADR-031 qualifies only its declared desktop tuple and does not qualify Android by name.

PlatformAndroid owns the retained `ANativeWindow` reference and its generation. The
Android Vulkan backend borrows the admitted generation while it owns the Vulkan instance,
device, surface, swapchain, queues, synchronization and deferred retirement. Surface loss
invalidates presentation for that generation, not the process or logical runtime. Renderer
support is the intersection of implemented operations, reported facts and product policy;
a Vulkan version string alone never admits a profile.

### 4. Lifecycle is serialized and generation safe

GameActivity callbacks enqueue typed observations. `AndroidHost` serializes them on the
application owner thread and drives explicit process, Activity, window and presentation
generations. Duplicate, reordered and late callbacks are valid inputs and must be idempotent
or rejected with a typed reason.

Backgrounding is suspension, not shutdown. Window loss stops new presentation against the
old generation while GPU references retire. Activity replacement may create new platform
and permission generations without destroying durable application state. Final process
shutdown cancels owned work and tears down XR, Renderer, Runtime and Platform in reverse
dependency order.

### 5. Native resources have one owner

| Resource or responsibility | Owner | Deliberate non-owner |
|---|---|---|
| GameActivity integration and callback queue | AndroidHost | Runtime and features |
| Activity, JavaVM/JNI attachment policy | AndroidHost with PlatformAndroid operations | Renderer and XR feature code |
| `ANativeWindow` retained reference and generation | PlatformAndroid | Renderer only borrows an admitted generation |
| Vulkan objects and presentation retirement | RenderVulkanAndroid | PlatformAndroid |
| Input, permissions, sensors, storage and Android services | PlatformAndroid | Portable Runtime |
| OpenXR loader/session/action objects | XROpenXR | PlatformAndroid and Renderer |
| Product profile, adapter selection and shutdown order | Android product host | Descriptors and libraries |

Cross-owner exchange uses typed Horo values, capability snapshots, opaque stable IDs and
bounded operations. No public header contains Android, JNI, GameActivity, Vulkan or OpenXR
native declarations.

### 6. XR reuses the host without merging owners

Standalone Android XR is an optional product composition over the same AndroidHost and
PlatformAndroid contracts. The host passes admitted application/activity initialization
inputs through the private capability defined by ADR-158. XROpenXR retains ownership of
loader, instance, session, spaces and actions; PlatformAndroid does not call OpenXR, and
the Vulkan backend does not own XR lifecycle. Android and XR target tuples qualify
independently and must both pass for a standalone package.

### 7. Toolchain and packages are pinned per ABI

The repository records compatible SDK, NDK, CMake, JDK, Android Gradle Plugin and
GameActivity dependency versions in target presets/package inputs. CMake configures one
ABI per build directory. A multi-ABI package composes independently built, verified
artifacts and records each ABI's native libraries and hashes.

Release preflight rejects missing or drifting tools, unsupported API/ABI pairs, absent
native dependencies, undeclared permissions/features, unavailable signing inputs, and
renderer/profile mismatch. Machine SDK paths and credentials never enter source or
published provenance.

### 8. Migration and qualification fail closed

Prototype native loops migrate behind AndroidHost; direct JNI/platform calls migrate to
PlatformAndroid; Android graphics code migrates into the private Android Vulkan target;
and ad hoc Gradle/CMake values migrate to versioned target profiles. There is no temporary
second source of ownership truth.

Host fakes validate lifecycle permutations and failure paths. Release qualification also
requires reproducible physical-device evidence for the exact API/ABI/renderer/profile
tuple, including cold start, suspend/resume, Activity and surface recreation, input and
permission neutralization, memory/thermal pressure, device loss, packaging and shutdown.
An emulator, RenderNull run or desktop Vulkan result cannot qualify an Android release.

## Consequences

- Android implementation tickets have a stable entry model, target floor and ownership
  boundary instead of choosing them locally.
- API 29 and `arm64-v8a` intentionally exclude older devices and 32-bit products; broader
  support requires a new admitted and qualified product profile.
- Android Vulkan, GameActivity integration and packaging require explicit private targets
  and physical-device evidence before support may be claimed.
- Store target-level changes remain release-policy updates and do not rewrite runtime
  ownership or capability contracts.

## Rejected Alternatives

### Use NativeActivity or allow multiple entry models

Rejected because competing event loops and lifecycle owners make callback ordering,
dependency upgrades and test expectations product-specific.

### Reuse the desktop Vulkan target unchanged

Rejected because Android surface, loader, lifecycle, packaging and qualification concerns
are different even when portable renderer contracts and some implementation utilities are
shared.

### Support every NDK ABI and graphics API initially

Rejected because build success is not lifecycle, driver or package qualification and would
multiply the initial evidence matrix without a product requirement.

### Let runtime code probe policy from the device or Gradle environment

Rejected because ambient probing makes support non-reproducible and moves composition,
distribution and fallback authority into feature code.

## External References

- [GameActivity introduction and setup](https://developer.android.com/games/agdk/game-activity/get-started)
- [Android NDK ABI management](https://developer.android.com/ndk/guides/abis)
- [Android native Vulkan support guidance](https://developer.android.com/games/develop/vulkan/native-engine-support)
- [Google Play target API requirements](https://developer.android.com/google/play/requirements/target-sdk)
