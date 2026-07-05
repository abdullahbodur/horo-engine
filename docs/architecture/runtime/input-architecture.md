# Input Architecture

## Purpose

This document defines platform input collection, frame snapshots, action
mapping, editor and gameplay routing, focus, capture, modal exclusivity,
rebinding, and deterministic simulation consumption.

## Core Decisions

- Platform events are normalized into immutable input snapshots.
- Raw device state and semantic actions are separate layers.
- Input routing follows explicit interaction scopes and focus ownership.
- Editor modals block workspace and gameplay command handling before handlers
  execute.
- Fixed simulation consumes tick-assigned input commands, not mutable live
  device state.
- Bindings are typed configuration with conflict validation.
- Input is not distributed through the general process data bus.
- Gamepad input is a first-class runtime input device, not an optional addon.
- Optional packages may improve gamepad mappings, glyphs, vendor metadata, and
  advanced device features, but common controllers must work without installing
  an external package.

## Layer Model

```text
OS / Window Events
       |
       v
Platform Input Collector
       |
       v
RawInputSnapshot
       |
       v
Input Router + Action Maps
       |
       +-- GUI text/navigation input
       +-- Editor commands and viewport controls
       +-- Gameplay InputFrame
```

The platform collector owns device state. Consumers receive immutable snapshots
for one frame.

## Raw Snapshot

```cpp
struct RawInputSnapshot {
    FrameNumber frame;
    KeyboardState keyboard;
    PointerState pointer;
    std::vector<GamepadState> gamepads;
    TextInput text;
    ModifierState modifiers;
    WindowInputState window;
};
```

It includes held state and transitions:

- down
- pressed this frame
- released this frame
- repeated text/navigation events where applicable

Scroll, pointer delta, and text input are frame-local values.

## Event Collection

Native callbacks update a platform-owned collector or bounded event queue. After
the host polls events, the collector commits one snapshot.

Callbacks do not:

- mutate scene or editor state
- invoke application use cases
- publish one data-bus event per key or pointer movement
- call renderer or GUI business logic

## Action Mapping

```cpp
struct ActionDescriptor {
    ActionId id;
    ActionValueType valueType;
    InputContextId context;
    std::vector<InputBinding> defaultBindings;
};
```

Actions may be digital, one-dimensional, two-dimensional, or text-like where
explicitly supported.

Examples:

```text
editor.save
editor.undo
viewport.orbit
viewport.move
gameplay.move
gameplay.jump
```

Bindings may combine keys, buttons, axes, modifiers, and chords. They use typed
device controls rather than arbitrary display strings.

## Input Contexts

Contexts form an ordered routing stack:

```text
Native Dialog
Modal Child
Modal Root
Focused GUI Widget
Editor Tool Capture
Editor Workspace
Gameplay
Global Non-Mutating Commands
```

The first eligible context may consume an input transition. Held state is still
available only where policy allows it.

Contexts are pushed and removed through RAII tokens. A destroyed tab, modal,
tool, or play session cannot leave an active context behind.

## Focus And Capture

Focus selects the keyboard and accessibility target. Capture grants a surface
continued pointer delivery during an active gesture.

Capture:

- has one owner
- records the initiating button/pointer
- ends on release, cancellation, focus loss, modal opening, or owner destruction
- prevents competing viewport and GUI gestures

Opening a root modal cancels workspace capture, gizmo drag, drag/drop, and
incomplete editor gestures before activating modal scope.

## Modal Exclusivity

While `EditorInteractionScopeKind::Modal` is active:

- only the top modal and its widget popups receive input
- editor shortcuts do not execute
- viewport navigation and picking are suppressed
- gameplay input is neutralized unless the modal explicitly represents an
  in-game overlay with a separate contract
- background model and job updates continue without interactive input

A dim layer alone is not an input barrier. Central routing enforces exclusivity.

## Text Input

Text input uses native text/composition events and is separate from physical key
bindings. Active text widgets consume editing commands before global shortcuts.

IME composition is owned by the focused text surface and is cancelled or
committed according to platform policy when focus scope changes.

## Gameplay Input Frames

The runtime transforms action state into a simulation input frame:

```cpp
struct GameplayInputFrame {
    SimulationTick tick;
    Vec2 move;
    Vec2 look;
    bool jumpPressed;
    bool interactPressed;
};
```

One fixed tick consumes one assigned input frame. When several simulation ticks
run during one presentation frame, edge-triggered actions are consumed according
to the action's declared policy and do not fire accidentally on every catch-up
tick.

This representation supports recording, replay, tests, and future networking
without replaying OS events.

## Rebinding And Persistence

Binding overrides are user configuration by default. A game project may define
its own action schema and packaged defaults.

Rebinding validates:

- reserved operating system shortcuts
- duplicate or conflicting bindings in active contexts
- unsupported device controls
- required actions left unbound
- modifier and chord ambiguity

Display labels are generated by the platform/UI adapter and are not persistent
binding identity.

## Device Lifecycle

Device connection and disconnection update the next snapshot. Gameplay assigns
devices through explicit player/device policy.

Loss of an active device produces neutral state and a typed notification. It
does not leave keys, buttons, or axes logically held.

## Gamepad Support

Gamepad support is part of the runtime input contract. The engine owns the
canonical gamepad model, snapshot integration, action-map binding support,
rebinding validation, device lifecycle, player assignment, normalization,
disconnect behavior, haptics contract, and virtual test backend.

The baseline engine distribution must provide a usable common-controller path:

- canonical `GamepadState`, `GamepadDeviceId`, `GamepadButton`, and
  `GamepadAxis` types
- basic controller mapping fallback using platform game-controller APIs or an
  SDL/XInput-style canonical layout strategy
- unknown-controller fallback that exposes supported buttons and axes safely
- action binding support for buttons, triggers, sticks, and D-pad controls
- configurable deadzone and axis normalization policies
- disconnect neutralization
- player/device assignment
- virtual gamepad injection for tests and headless runs
- simple rumble/haptics interface where the platform supports it

Optional packages may extend gamepad support with larger controller mapping
databases, glyph packs, Steam Input integration, DualSense advanced features,
Nintendo layout metadata, adaptive triggers, lightbar, touchpad, or other
vendor-specific capabilities. These packages improve correctness, presentation,
and device-specific richness; they do not define whether gamepad input exists.

### Device Identity

Gamepad identity is not a stable integer index alone. Device handles include a
session generation so stale references cannot silently point at a different
controller after disconnect/reconnect:

```cpp
struct GamepadDeviceId {
    uint32_t slot;
    uint64_t sessionGeneration;
};
```

The slot is presentation-friendly. The session generation protects ownership and
lifetime-sensitive state.

### Canonical Controls

The engine exposes physical position-oriented canonical controls:

```cpp
enum class GamepadButton {
    South,
    East,
    West,
    North,
    LeftShoulder,
    RightShoulder,
    LeftStick,
    RightStick,
    Start,
    Select,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
};

enum class GamepadAxis {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
};
```

Gameplay and editor actions bind to canonical controls, not vendor display
labels. A project should bind `gameplay.jump` to `GamepadButton::South`; the UI
can then present Xbox `A`, PlayStation `Cross`, or Nintendo south-position text
through a glyph provider.

### Player Assignment

Player assignment is explicit and supports mixed device ownership:

```text
Player 1 -> Keyboard/Mouse
Player 2 -> Gamepad A
Player 3 -> Gamepad B
```

Future local co-op flows such as "press any button to join" are built on top of
device lifecycle and assignment primitives. They do not bypass the action-map or
snapshot model.

### Deadzone And Normalization

Deadzone policy is binding/context-specific, not a single global constant:

```text
leftStick.move      radial deadzone 0.18
rightStick.camera   radial deadzone 0.12
trigger.accelerate  threshold 0.05
menu.navigation     digital threshold 0.50
```

Normalization happens before action resolution so gameplay consumes stable
values independent of backend quirks. Raw values may be exposed only through
diagnostic or explicitly raw-input APIs.

### Glyph And Presentation Providers

The engine owns the `GlyphProvider` interface but not every icon pack:

```text
Action: gameplay.jump
Binding: GamepadButton::South
Glyph provider:
  Xbox        -> A
  PlayStation -> Cross
  Nintendo   -> south-position policy label
```

Glyph packs are packages. The absence of a glyph pack falls back to canonical
text labels and does not break input.

### Haptics Boundary

Core haptics is intentionally narrow:

```cpp
class IGamepadHaptics {
public:
    virtual Result<void> PlayRumble(GamepadDeviceId id, RumbleEffect effect) = 0;
    virtual Result<void> Stop(GamepadDeviceId id) = 0;
};
```

Advanced vendor features such as adaptive triggers, lightbar, touchpad gestures,
or HD rumble require explicit capability descriptors and may be delivered by
optional packages or platform integrations.

### Virtual Test Backend

Headless and deterministic tests use a virtual gamepad backend:

```cpp
VirtualGamepad pad;
pad.Connect();
pad.Press(GamepadButton::South);
pad.SetAxis(GamepadAxis::LeftX, 1.0f);
```

Virtual input commits through the same snapshot path as platform devices. Tests
must not mutate gameplay state by bypassing the input service.

## Data Bus Relationship

High-frequency input does not travel through `EngineDataBus` or
`EditorDataBus`. The authoritative input service is queried once per frame or
tick.

Low-frequency committed changes may publish notifications:

- bindings changed
- active device changed
- input capability changed

## Testing

Required tests cover:

- pressed/released frame semantics
- context priority and consumption
- modal blocking and focus restoration
- pointer capture cancellation
- text input versus shortcut routing
- fixed-tick edge consumption during catch-up
- deterministic recorded input replay
- binding conflict validation
- device disconnect neutralization
- headless injection without a native window
- canonical gamepad mapping fallback without external packages
- stale gamepad handle rejection after disconnect/reconnect
- mixed keyboard/gamepad player assignment
- binding-specific gamepad deadzone policies
- glyph-provider fallback when no glyph package is installed
- virtual gamepad injection through the snapshot path

## Related Documents

- [Input Mapping Editor UI Reference](./input-mapping-editor.html): action maps, bindings, device preview, and conflict detection panel.

- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Editor Modal Host](../editor/editor-modal-host.md)
- [GUI Screen Host](../editor/gui-screen-host.md)
- [Configuration System](../foundation/configuration-system.md)
- [Platform Abstraction](../foundation/platform-abstraction.md)

