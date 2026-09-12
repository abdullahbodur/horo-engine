# World Streaming Cell Activation Migration

Cell activation now uses `StreamingCellActivationTransaction` as the only public
owner of prepared Scene and feature-provider receipts.

## Required changes

1. Give each Scene/provider participant a stable `StreamingRuntimeServiceId` and
   exact `StreamingRuntimeServiceRevision` from the active composition.
2. Return a unique `IStreamingCellActivationReceipt` only after the participant is
   fully prepared for no-fail publication. Keep backend-native resources private in
   the receipt implementation.
3. Build the complete `StreamingCellActivationRequirement` set and call `Prepare`
   with the exact Activating `StreamingCellOperation` and a positive receipt bound.
4. Invoke `Commit` only during `CommitDeferredLifecycleChanges`, after revalidating
   the current operation fence and authority lifecycle.

Do not invoke provider publication individually. Missing, duplicated, stale or
over-capacity receipt sets fail preparation and roll back every supplied receipt.
Dropping or moving over a prepared transaction preserves unique ownership; its
destructor rolls back unless publication completed.
