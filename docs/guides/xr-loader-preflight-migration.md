# XR Loader Preflight Migration

## Purpose

XR startup code must no longer collapse installation, loader discovery, active-runtime
selection and system support into one `available` flag. Include
`Horo/XR/XRLoaderPreflight.h` and carry the typed result across GUI, CLI and packaged
preflight boundaries.

## Composition

The application creates one `XRLoaderPreflightRequest` from its already verified
backend/install record, product profile and exact loader-source policy. The private XR
adapter performs only the platform/native probe and returns bounded
`XRLoaderProbeEvidence` for the same attempt. Pass both to
`CreateXRLoaderPreflightSnapshot`; do not place paths, runtime names, native handles or
environment strings in either public value.

Shipping requests use `SystemDefault`. A developer override is valid only when the
application deliberately selects `Development`, selects
`ApprovedDeveloperOverride`, and records explicit approval. There is no bundled/system
or runtime fallback sequence.

## Failure And Lifetime

Branch on the registered error identity, not message text. Loader absence,
incompatibility/open failure, runtime absence/rejection, unsupported/temporary system,
override rejection, cancellation and stale attempts remain distinct. A failure
publishes no successful snapshot or partial runtime generation.

Before instance/session activation, call `ValidateXRLoaderPreflight` with the current
attempt, backend, install record and profile. Replacement or shutdown invalidates old
evidence even when display names or native runtime choices appear unchanged.
