# World Streaming Cell Candidate Migration

Cell artifact readers must now prepare `StreamingCellCandidate` values before
provider decode or owner-thread publication.

1. Parse the fixed `HOROCELL` header and TOC into bounded caller-owned storage.
2. Capture the exact `StreamingCellOperationHandle`, load kind, `Preparing` phase,
   lifecycle and byte/count ceilings at job submission.
3. Call `PrepareStreamingCellCandidate` on a worker with the immutable
   `CookedWorldIndexManifest` publication and parsed header view.
4. Move the successful candidate into later decode/provider stages. Before commit,
   the owner must compare `candidate.Operation()` with the current operation and
   generation fence.

Preparation copies package identity, manifest integrity facts and canonical payload
rows. It does not retain header spans, perform I/O, decode data, call providers or
advance cell state. A replacement generation therefore cannot adopt an older
candidate, even when both target the same cell tuple.

Callers must not substitute a different manifest row, codec, provider requirement or
capacity after failure. Cancellation and shutdown reject new preparation explicitly;
already owned candidates remain ordinary values whose later publication still needs
current-fence validation.
