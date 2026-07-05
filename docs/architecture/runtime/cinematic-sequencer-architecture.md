# Cinematic Sequencer Architecture

## Purpose

This document defines the cinematic sequencer (timeline) subsystem for Horo
Engine. It covers timeline editing, keyframe animation of scene objects and
properties, camera cuts, event tracks, audio tracks, cinematic playback, and
editor authoring.

## Sequencer Model

A cinematic sequence is a timeline that animates scene objects and triggers
events over time:

```cpp
struct SequenceAsset {
    SequenceId      id;
    std::string     name;
    Duration        duration;
    float           frameRate;          // timeline frame rate (e.g., 30 fps)
    std::vector<SequenceTrack> tracks;
    SequencePlaybackSettings playback;
};
```

Each track targets a specific object and property or fires events:

```cpp
struct SequenceTrack {
    TrackId         id;
    std::string     displayName;
    TrackType       type;
    StableObjectId  targetObject;       // resolved to runtime EntityId during binding
    ComponentId     targetComponent;    // or empty
    PropertyBindingId targetProperty;   // typed binding registry id
    std::vector<Keyframe> keyframes;
    TrackBlendSettings blend;
};
```

## Track Types

### Transform Track

Animates an object's position, rotation, and scale:

```cpp
struct TransformKeyframe {
    float         time;
    WorldTransform value;
    InterpolationType interpolation;   // Linear, Cubic, Constant
    Tangent       tangentIn;
    Tangent       tangentOut;
};
```

### Property Track

Animates any numeric or vector component property:

- Float properties (light intensity, field of view, material parameter)
- Vector properties (color, post-process weight)
- Bool properties (visibility, enabled state)

### Camera Cut Track

Switches the active camera at specific times:

```cpp
struct CameraCutKeyframe {
    float       time;
    StableObjectId cameraObject;
    float       blendDuration;      // cross-fade between cameras
};
```

### Event Track

Fires named events at specific times for gameplay scripts to handle:

```cpp
struct EventKeyframe {
    float           time;
    std::string     eventName;
    VariantMap      payload;
};
```

Events trigger gameplay module callbacks, audio cues, VFX spawns, and
dialogue triggers.

### Audio Track

Plays audio clips synchronized to the timeline:

```cpp
struct AudioTrackKeyframe {
    float       startTime;
    float       duration;
    AssetId     audioClip;
    float       volume;
    float       pitch;
    bool        loop;
};
```

### Sub-Sequence Track

Nests another sequence as a track, enabling modular cinematic composition:

```cpp
struct SubSequenceKeyframe {
    float       startTime;
    AssetId     sequenceAsset;
    float       playbackSpeed;
    bool        syncToParent;       // stretch to fit parent duration
};
```

## Playback

### Playback Controller

```cpp
class SequencePlayer {
public:
    void Play();
    void Pause();
    void Stop();
    void Seek(float time);
    float GetCurrentTime() const;
    SequencePlaybackState GetState() const;
    void SetPlaybackSpeed(float speed);

    // Events
    Signal<void(std::string_view eventName, const VariantMap& payload)> OnEvent;
    Signal<void()> OnFinished;
};
```

### Evaluation

Sequence evaluation runs each frame:

1. Advance time by `deltaTime * playbackSpeed`
2. For each active track, sample the interpolated value at current time
3. Apply sampled values to target properties
4. Fire events whose keyframe time was crossed this frame
5. Handle camera cuts (blend between outgoing and incoming camera)
6. At sequence end, apply `SequenceEndBehavior` (stop, loop, ping-pong)

Evaluation order respects track dependencies (parent-child transform
hierarchies are evaluated top-down).

### Blending

Sequences can blend into and out of gameplay:

```cpp
struct SequencePlaybackSettings {
    float           blendInDuration;
    float           blendOutDuration;
    PlaybackRestoreBehavior restore;   // RestoreToPreSequence or KeepFinalState
    bool            pauseGameplay;
    bool            hideHUD;
};
```

## Editor Authoring

### Timeline View

The cinematic editor provides a timeline-based editing surface:

- Multi-track timeline with zoom and scroll
- Keyframe creation, deletion, and drag manipulation
- Property recording (record object manipulation as keyframes)
- Track solo/mute for focused editing
- Playback with scrubbing in the editor viewport
- Curve editor for fine-tuning keyframe interpolation

### Integration

Sequences are editor-authorable assets stored in the project's asset tree.
They reference scene objects by entity ID. The cinematic editor opens as a
separate editor tab (or modal for focused editing).

## Runtime Integration

Cinematics can be triggered from:

- Gameplay scripts (`SequencePlayer::Play`)
- Scene loading (auto-play intro cinematic)
- Gameplay events (dialogue, boss fight intro, level transition)
- Authorized editor/runtime control adapters (MCP or remote tooling must pass capability checks)

## Feature Tiers

| Feature              | `es3`     | `dx11` / `dx12_vulkan` | `high_end` |
| -------------------- | ---------- | ------------- | ------------ |
| Timeline tracks      | 16         | 128           | 512          |
| Simultaneous players | 1          | 4             | 16           |
| Property recording   | No         | Yes           | Yes          |
| Sub-sequences        | 1 level    | 4 levels      | 8 levels     |
| Curve editor         | Linear only| Full curves   | Full curves  |
| Camera cut blending  | Hard cut   | Linear blend  | Smooth blend |

## Related Documents

- [Cinematic Sequencer UI Reference](./cinematic-sequencer.html)

- [Scene Runtime](./scene-runtime.md): scene object model and property paths
- [Animation Architecture](./animation-architecture.md): skeletal animation integration
- [Audio Architecture](./audio-architecture.md): audio track playback
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md): event handling in behaviors
- [Editor Document Model](../editor/editor-document-model.md): sequence asset editing
- [VFX And Particles Architecture](./vfx-and-particles-architecture.md): VFX spawn events
