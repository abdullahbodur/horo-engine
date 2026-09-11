# Sequence Player State Migration

The cinematic runtime now exposes its typed playback state contract through
`Horo/Cinematic/SequencePlayer.h`. Runtime composition code that previously carried
ad hoc `playing`, `paused`, `stopped`, time and floating-speed fields should link
`HoroEngine::CinematicRuntime` and retain a `SequencePlayerHandle` plus immutable
`SequencePlayerSnapshot` observations.

## Required Migration

1. Have the session-owned cinematic registry issue non-zero runtime-session and player
   identities and call `SequencePlayer::Create` after preparation succeeds.
2. Route Play, Pause, Stop, Seek and SetPlaybackSpeed commands through the registry at
   its owning boundary. Do not expose a mutable `SequencePlayer` reference to scene,
   gameplay, UI or tool callers.
3. Translate `SequencePlayerTransition::signal` once at the observation boundary.
   Repeated no-change commands carry `None` and must not duplicate notifications.
4. On stop, close future admission and drain already admitted occurrences before
   `FinishStop`. On cancellation, scene/session loss or shutdown, use Close and retire
   pending work before `FinishClose`.
5. Capture `SequencePlayerOperationFence` for deferred preparation/provider work and
   validate it before commit. A stale handle or revision is discarded; it never
   targets a replacement player.
6. Express rate as a bounded `SequencePlaybackRate`. A zero numerator freezes clock
   advancement without publishing Paused. Do not infer pause from speed.
7. Seek by direct random-access sampling at the returned target position. Reset the
   event cursor without replaying crossed events; explicit crossed-event dispatch is
   a separate later runtime capability.

`Stopped` and `Failed` are terminal. Restart creates a new player generation rather
than reviving retained state. No compatibility shim should keep legacy booleans as a
second source of truth.
