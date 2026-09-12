# World Streaming Cell Asset Request Migration

Cell loading now admits the candidate package and its hard-dependency packages as one
bounded `StreamingCellAssetRequest`. Construct the request from the same immutable
manifest, registry snapshot, and `StreamingCellOperationHandle` used for candidate
preparation. The returned move-only controller owns a cancellation root; dropping it or
calling `RequestCancel()` requests cancellation for every accepted provider child.

Poll `State()` without blocking. Consume `TakeResult()` only after a terminal state and
revalidate the returned operation fence before publication. `Ready` yields bytes in
canonical candidate-then-dependency order and preserves the submission-time registry
revision. Failure, cancellation, replacement, shutdown, or partial child admission never
publishes an aggregate batch.

Hosts must configure a positive `maximumRequests`. Missing registry entries, unresolved
manifest cells, stale generations, closed lifecycle, and over-capacity dependency trees
return typed errors; no provider fallback or ambient registry lookup is performed.
