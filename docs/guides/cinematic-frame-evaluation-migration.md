# Cinematic Per-Frame Evaluation Migration

Runtime hosts should build one `SequenceFrameEvaluationPlan` during player
preparation and retain its borrowed callback contexts until the player is retired.
The plan owns only bounded backend-neutral descriptors; concrete scene, camera and
event owners remain injected adapters.

At a fixed or admitted service boundary:

1. Snapshot eligible players and call `OrderSequenceFramePlayers` using caller-owned
   storage.
2. Create each `SequenceFrameCursor` from the current `SequencePlayerSnapshot`.
   Use `EmitCurrentBoundary` for initial play and `SuppressCurrentBoundary` after a
   seek or external discontinuity.
3. Supply fixed-capacity value, occurrence and camera-cut spans to `Evaluate`.
4. Dispatch only the typed occurrences and camera requests produced after the
   successful call. Do not replay keys after a failed call.

Recreate a cursor whenever the player control revision changes. A stale cursor is a
typed error and must never be patched in place. Pause, stop, cancellation, scene
replacement and shutdown close admission at the owning service boundary. Frame-hot
code must not resize scratch buffers, discover services, resolve strings, block, or
retain backend-native handles.
