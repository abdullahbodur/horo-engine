# Accessibility Architecture

## Purpose

This document defines the accessibility subsystem for Horo Engine. It covers
accessibility features for players (closed captions, colorblind modes, control
remapping, screen reader support, difficulty assist) and for developers
(accessibility testing tools, compliance validation, editor accessibility).

## Player Accessibility

### Closed Captions And Subtitles

```cpp
struct ClosedCaptionSettings {
    bool      enabled;
    float     fontSize;
    Color     textColor;
    Color     backgroundColor;
    float     backgroundOpacity;
    CaptionPosition position;        // bottom, top, follow speaker
    bool      showSpeakerName;
};
```

Captions are triggered by audio events that carry speaker identity, dialogue
text, and sound descriptions. The caption renderer displays text with
configurable appearance. Captions respect safe area margins.

### Colorblind Support

Colorblind modes are implemented as post-process color transforms:

```cpp
enum class ColorblindMode {
    None,
    Protanopia,      // red-blind
    Deuteranopia,    // green-blind
    Tritanopia,      // blue-blind
    CustomMatrix,
};
```

The mode is applied as a 3×3 color matrix in the post-process pipeline after
tonemapping. Games can query the active mode to provide alternative visual
cues (patterns, shapes, labels) in addition to color.

### Control Remapping

```cpp
struct AccessibilityControls {
    bool      allowFullRemapping;
    bool      enableToggleActions;     // toggle instead of hold
    bool      enableStickyKeys;        // sequential instead of simultaneous
    float     holdDuration;            // adjustable hold threshold
    float     repeatDelay;             // adjustable repeat rate
    bool      enableGyroAim;           // motion-controlled aiming
    float     gyroSensitivity;
};
```

The input system supports per-action remapping to any input device. Remapping
is exposed through the input-mapping editor and at runtime through the pause
menu. Remapping profiles can be saved per user.

### Screen Reader Support

A screen-reader interface exposes UI state to platform accessibility APIs:

```cpp
class IScreenReader {
public:
    virtual void Announce(std::string_view text, AnnouncePriority priority) = 0;
    virtual void SetFocus(std::string_view elementId) = 0;
    virtual void DescribeElement(std::string_view elementId,
                                  std::string_view label,
                                  std::string_view value,
                                  std::string_view role) = 0;
};
```

UI elements register their accessibility metadata (label, role, value, state)
with the screen-reader bridge. Focus changes and value updates are announced
automatically. The editor UI also exposes accessibility metadata for
developer tools.

### Difficulty Assists

Configurable gameplay assists that do not affect the core game balance:

```cpp
struct DifficultyAssists {
    bool    aimAssist;
    float   aimAssistStrength;
    bool    autoAimSnap;
    bool    reducedEnemyAggression;
    float   reactionTimeMultiplier;    // slows enemy reaction
    bool    skipQuickTimeEvents;
    bool    invincibilityAfterHit;     // brief invulnerability window
    float   damageMultiplier;          // reduces incoming damage
};
```

These are exposed as gameplay settings, not cheat codes. They are configured
in the accessibility menu and communicated to gameplay systems through the
`GameplayAccessibilityState` data bus topic.

### Visual Accessibility

```cpp
struct VisualAccessibilitySettings {
    float     uiScale;                 // global UI scale
    bool      highContrastMode;
    float     textContrast;            // increase text/background contrast
    bool      reduceMotion;            // disable screen shake, animations
    bool      disableFlashEffects;     // prevent seizure triggers
    float     cameraShakeReduction;
};
```

## Developer Accessibility

### Accessibility Validation

The editor provides accessibility testing tools:

- Colorblind preview (simulate each mode in the viewport)
- Contrast checker (W3C WCAG AA/AAA compliance for UI elements)
- Screen reader log (record all announced text for review)
- Control scheme validator (detect unmapped essential actions)

### Editor Accessibility

The Horo Editor itself is accessible:

- All editor functions are keyboard-accessible with documented shortcuts
- UI elements expose accessibility roles for platform screen readers
- High-contrast editor theme is available
- Editor text size is configurable
- Editor UI respects OS accessibility settings (reduced motion, display
  scaling)

## Compliance

Accessibility features target WCAG 2.2 Level AA compliance for the editor
and aim to meet platform accessibility requirements (CVAA for games in the US,
European Accessibility Act, console platform TRCs).

### Testing

- Automated contrast checks in CI for editor UI
- Screen reader testing as part of editor QA
- Colorblind simulation as part of rendering validation
- Control remapping fuzz testing

## Feature Tiers

| Feature              | All Tiers |
| -------------------- | --------- |
| Closed captions      | Yes       |
| Colorblind modes     | Yes       |
| Control remapping    | Yes       |
| Screen reader bridge | Yes       |
| Difficulty assists   | Yes       |
| UI scaling           | Yes       |
| Reduce motion        | Yes       |

Accessibility features are not tier-gated. They are available on all platforms
and rendering backends.

## Related Documents

- [Game UI And HUD](./game-ui-and-hud.md): UI element accessibility metadata
- [Input Architecture](./input-architecture.md): control remapping system
- [Post-Processing And Effects](./post-processing-and-effects-architecture.md): colorblind color matrices
- [Audio Architecture](./audio-architecture.md): caption event triggers
- [Editor Panel Host](../editor/editor-panel-host.md): accessibility settings panel
- [Localization](../editor/localization.md): caption and subtitle localization
