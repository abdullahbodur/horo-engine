#include "Horo/Runtime/Input.h"

#include <array>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>

namespace
{
using namespace Horo::Input;

struct CaptureOwner final : IInputCaptureOwner
{
    void OnInputCaptureCancelled(const CaptureCancellationReason value) noexcept override { cancelled = value; }
    std::optional<CaptureCancellationReason> cancelled;
};

InputBinding KeyBinding(const Key key, const float scale = 1.0F, const std::uint8_t component = 0)
{
    return InputBinding{.kind = BindingControlKind::Key, .key = key, .scale = scale, .component = component};
}

void SnapshotTransitionsAreFrameLocal()
{
    RawInputCollector collector;
    collector.BeginFrame(1);
    collector.SetKey(Key::W, true);
    const RawInputSnapshot &first = collector.Commit();
    assert(first.State(Key::W).down && first.State(Key::W).pressed && !first.State(Key::W).released);
    collector.BeginFrame(2);
    const RawInputSnapshot &held = collector.Commit();
    assert(held.State(Key::W).down && !held.State(Key::W).pressed && !held.State(Key::W).released);
    collector.BeginFrame(3);
    collector.SetKey(Key::W, false);
    const RawInputSnapshot &released = collector.Commit();
    assert(!released.State(Key::W).down && released.State(Key::W).released);
}

void InputServiceCommitsTextAndImeThroughTheSameSnapshot()
{
    InputService input;
    input.BeginFrame(7);
    input.Collector().AppendText("a");
    input.Collector().SetTextComposition("ö", 0, 1);
    const RawInputSnapshot &snapshot = input.CommitFrame();
    assert(snapshot.frame == 7 && snapshot.text == "a");
    assert(snapshot.composition.active && snapshot.composition.text == "ö");
    assert(&input.Router().Snapshot() == &snapshot);
}

void ContextPriorityAndCaptureAreDeterministic()
{
    RawInputCollector collector;
    collector.BeginFrame(1);
    collector.SetPointerButton(PointerButton::Primary, true);
    const RawInputSnapshot &snapshot = collector.Commit();
    InputRouter router;
    router.BeginFrame(snapshot);
    auto workspace = router.PushContext(InputContextId{"workspace"}, InputContextKind::EditorWorkspace);
    CaptureOwner owner;
    auto captured = router.CapturePointer(workspace, PointerButton::Primary, owner);
    assert(captured.HasValue() && router.HasCapture());
    auto modal = router.PushContext(InputContextId{"modal"}, InputContextKind::ModalRoot);
    assert(owner.cancelled == CaptureCancellationReason::ModalOpened);
    assert(!router.IsContextActive(workspace) && router.IsContextActive(modal) && !router.HasCapture());
    modal.Reset();
    assert(router.IsContextActive(workspace));
    auto firstWidget = router.PushContext(InputContextId{"widget.first"}, InputContextKind::FocusedGuiWidget);
    auto secondWidget = router.PushContext(InputContextId{"widget.second"}, InputContextKind::FocusedGuiWidget);
    assert(!router.IsContextActive(firstWidget) && router.IsContextActive(secondWidget));
    secondWidget.Reset();
    assert(router.IsContextActive(firstWidget));

    owner.cancelled.reset();
    auto escapeCapture = router.CapturePointer(firstWidget, PointerButton::Primary, owner);
    assert(escapeCapture.HasValue());
    collector.BeginFrame(2);
    collector.SetKey(Key::Escape, true);
    router.BeginFrame(collector.Commit());
    assert(owner.cancelled == CaptureCancellationReason::Escape && !router.HasCapture());

    owner.cancelled.reset();
    auto deviceCapture = router.CapturePointer(firstWidget, PointerButton::Primary, owner);
    assert(deviceCapture.HasValue());
    collector.BeginFrame(3);
    collector.SetWindowState({.focused = true, .pointerInside = true, .pointerDeviceAvailable = false});
    router.BeginFrame(collector.Commit());
    assert(owner.cancelled == CaptureCancellationReason::DeviceDisconnected && !router.HasCapture());

    owner.cancelled.reset();
    collector.BeginFrame(4);
    collector.SetWindowState({.focused = true, .pointerInside = true, .pointerDeviceAvailable = true});
    router.BeginFrame(collector.Commit());
    auto focusCapture = router.CapturePointer(firstWidget, PointerButton::Primary, owner);
    assert(focusCapture.HasValue());
    collector.BeginFrame(5);
    collector.SetWindowState({.focused = false, .pointerInside = true, .pointerDeviceAvailable = true});
    router.BeginFrame(collector.Commit());
    assert(owner.cancelled == CaptureCancellationReason::FocusLost && !router.HasCapture());

    collector.BeginFrame(6);
    collector.SetWindowState({.focused = true, .pointerInside = true, .pointerDeviceAvailable = true});
    router.BeginFrame(collector.Commit());
    for (const CaptureCancellationReason reason : {CaptureCancellationReason::Explicit,
                                                    CaptureCancellationReason::OwnerDestroyed})
    {
        owner.cancelled.reset();
        auto reasonCapture = router.CapturePointer(firstWidget, PointerButton::Primary, owner);
        assert(reasonCapture.HasValue());
        router.CancelCapture(reason);
        assert(owner.cancelled == reason && !router.HasCapture());
    }
    owner.cancelled.reset();
    auto releaseCapture = router.CapturePointer(firstWidget, PointerButton::Primary, owner);
    assert(releaseCapture.HasValue());
    PointerCaptureToken releaseToken = std::move(releaseCapture).Value();
    releaseToken.Release();
    assert(!router.HasCapture() && !owner.cancelled.has_value());

    auto context = router.PushContext(InputContextId{"temporary"}, InputContextKind::ModalChild);
    owner.cancelled.reset();
    auto contextCapture = router.CapturePointer(context, PointerButton::Primary, owner);
    assert(contextCapture.HasValue());
    context.Reset();
    assert(owner.cancelled == CaptureCancellationReason::ContextRemoved && !router.HasCapture());
}

void ActionsAndFixedTickEdgesResolveOnce()
{
    RawInputCollector collector;
    collector.BeginFrame(10);
    collector.SetKey(Key::W, true);
    collector.SetKey(Key::Space, true);
    const RawInputSnapshot &snapshot = collector.Commit();
    InputRouter router;
    const InputContextId gameplayId{"gameplay"};
    assert(router.SetActionMap({
        ActionDescriptor{ActionId{"move"}, ActionValueType::Axis2D, gameplayId, true,
                         {KeyBinding(Key::W, 1.0F, 1)}},
        ActionDescriptor{ActionId{"look"}, ActionValueType::Axis2D, gameplayId, false, {}},
        ActionDescriptor{ActionId{"jump"}, ActionValueType::Digital, gameplayId, true, {KeyBinding(Key::Space)}},
        ActionDescriptor{ActionId{"interact"}, ActionValueType::Digital, gameplayId, false, {}},
    }).HasValue());
    router.BeginFrame(snapshot);
    auto gameplay = router.PushContext(gameplayId, InputContextKind::Gameplay);
    GameplayInputFrameBuilder builder{ActionId{"move"}, ActionId{"look"}, ActionId{"jump"}, ActionId{"interact"}};
    const GameplayInputFrame first = builder.Consume(router, gameplay, 100);
    const GameplayInputFrame catchup = builder.Consume(router, gameplay, 101);
    assert(first.moveY == 1.0F && first.jumpPressed);
    assert(catchup.moveY == 1.0F && !catchup.jumpPressed);
}

void VirtualGamepadUsesSnapshotPathAndRejectsStaleIds()
{
    RawInputCollector collector;
    collector.BeginFrame(1);
    VirtualGamepad pad{collector};
    const GamepadDeviceId firstId = pad.Connect();
    assert(pad.Press(GamepadButton::South));
    const RawInputSnapshot &connected = collector.Commit();
    assert(connected.FindGamepad(firstId) != nullptr);
    assert(connected.FindGamepad(firstId)->buttons[static_cast<std::size_t>(GamepadButton::South)].pressed);
    collector.BeginFrame(2);
    pad.Disconnect();
    assert(collector.Commit().FindGamepad(firstId) == nullptr);
    collector.BeginFrame(3);
    const GamepadDeviceId secondId = pad.Connect();
    assert(firstId.slot == secondId.slot && firstId.sessionGeneration != secondId.sessionGeneration);
    assert(!collector.SetGamepadButton(firstId, GamepadButton::South, true));
}

void ProfilesRoundTripAndValidate()
{
    InputBindingProfile profile{.profileId = "desktop", .overrides = {
        BindingOverride{ActionId{"editor.save"}, {KeyBinding(Key::S)}}}};
    const auto serialized = SerializeBindingProfile(profile);
    assert(serialized.HasValue());
    const auto parsed = ParseBindingProfile(serialized.Value());
    assert(parsed.HasValue() && parsed.Value().profileId == "desktop");
    const std::array actions{ActionDescriptor{ActionId{"editor.save"}, ActionValueType::Digital,
                                               InputContextId{"workspace"}, true, {KeyBinding(Key::S)}}};
    assert(ValidateBindingProfile(actions, parsed.Value()).IsValid());
    assert(ParseBindingProfile("{broken").HasError());
    assert(ParseBindingProfile(
               R"({"schemaVersion":1,"schemaVersion":1,"profileId":"bad","overrides":[]})")
               .HasError());
}

void ProfilesWriteAtomicallyToNonAsciiPaths()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("horo-input-ü-" + std::to_string(suffix));
    const std::filesystem::path path = directory / "profil.json";
    const InputBindingProfile profile{
        .profileId = "user", .overrides = {BindingOverride{ActionId{"jump"}, {KeyBinding(Key::Space)}}}};
    assert(SaveBindingProfileAtomically(path, profile).HasValue());
    const Horo::Result<InputBindingProfile> loaded = LoadBindingProfile(path);
    assert(loaded.HasValue() && loaded.Value().profileId == "user");
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    assert(!error);
}

void ProfileConflictsReservationsAndLayeringAreTyped()
{
    const InputContextId workspace{"workspace"};
    const std::array actions{
        ActionDescriptor{ActionId{"first"}, ActionValueType::Digital, workspace, true, {KeyBinding(Key::A)}},
        ActionDescriptor{ActionId{"second"}, ActionValueType::Digital, workspace, false, {KeyBinding(Key::B)}}};
    InputBinding reserved = KeyBinding(Key::Q);
    reserved.requiredModifiers.command = true;
    const InputBindingProfile invalid{
        .profileId = "invalid",
        .overrides = {BindingOverride{ActionId{"first"}, {KeyBinding(Key::B)}},
                      BindingOverride{ActionId{"second"}, {reserved}}}};
    const BindingValidationReport report = ValidateBindingProfile(actions, invalid);
    assert(!report.IsValid());
    assert(std::ranges::any_of(report.diagnostics, [](const BindingDiagnostic &diagnostic) {
        return diagnostic.code == BindingDiagnosticCode::ReservedShortcut;
    }));

    const InputBindingProfile lower{.profileId = "project",
                                    .overrides = {BindingOverride{ActionId{"first"}, {KeyBinding(Key::C)}}}};
    const InputBindingProfile higher{.profileId = "user",
                                     .overrides = {BindingOverride{ActionId{"first"}, {KeyBinding(Key::D)}},
                                                   BindingOverride{ActionId{"second"}, {KeyBinding(Key::E)}}}};
    const auto merged = MergeBindingProfiles(lower, higher);
    assert(merged.HasValue() && merged.Value().profileId == "user" && merged.Value().overrides.size() == 2);
    assert(merged.Value().overrides.front().bindings.front().key == Key::D);
}

void ActionTransitionsAreConsumedAndGamepadAxesHaveEdges()
{
    RawInputCollector collector;
    collector.BeginFrame(1);
    collector.SetKey(Key::A, true);
    const GamepadDeviceId gamepad = collector.ConnectGamepad("pad");
    assert(collector.SetGamepadAxis(gamepad, GamepadAxis::LeftX, 0.8F));
    InputRouter router;
    const InputContextId contextId{"gameplay"};
    InputBinding axis{.kind = BindingControlKind::GamepadAxis,
                      .gamepadAxis = GamepadAxis::LeftX,
                      .deadzoneKind = DeadzoneKind::Axial,
                      .deadzone = 0.1F};
    assert(router.SetActionMap({ActionDescriptor{ActionId{"key"}, ActionValueType::Digital, contextId, false,
                                          {KeyBinding(Key::A)}},
                         ActionDescriptor{ActionId{"axis"}, ActionValueType::Axis1D, contextId, false, {axis}}})
               .HasValue());
    router.BeginFrame(collector.Commit());
    auto context = router.PushContext(contextId, InputContextKind::Gameplay);
    assert(router.ReadAction(context, ActionId{"key"}).pressed);
    assert(!router.ReadAction(context, ActionId{"key"}).pressed);
    assert(router.ReadAction(context, ActionId{"axis"}).pressed);

    collector.BeginFrame(2);
    router.BeginFrame(collector.Commit());
    assert(router.ReadAction(context, ActionId{"axis"}).down);
    assert(!router.ReadAction(context, ActionId{"axis"}).pressed);
    collector.BeginFrame(3);
    assert(collector.SetGamepadAxis(gamepad, GamepadAxis::LeftX, 0.0F));
    router.BeginFrame(collector.Commit());
    assert(router.ReadAction(context, ActionId{"axis"}).released);
}

void NeutralizeReleasesHeldControlsAndPrunesAssignments()
{
    RawInputCollector collector;
    collector.BeginFrame(1);
    collector.SetKey(Key::W, true);
    const GamepadDeviceId gamepad = collector.ConnectGamepad("pad");
    InputRouter router;
    router.BeginFrame(collector.Commit());
    assert(router.AssignGamepad(0, gamepad));
    collector.BeginFrame(2);
    collector.Neutralize();
    router.BeginFrame(collector.Commit());
    assert(router.Snapshot().State(Key::W).released);
    collector.BeginFrame(3);
    assert(collector.DisconnectGamepad(gamepad));
    router.BeginFrame(collector.Commit());
    assert(!router.PlayerForGamepad(gamepad).has_value());
}

void RecordingReplaysExactly()
{
    GameplayInputRecording recording;
    recording.Record(GameplayInputFrame{.tick = 1, .moveX = 0.5F, .jumpPressed = true});
    recording.Record(GameplayInputFrame{.tick = 2, .lookY = -0.25F});
    assert(recording.Next() == recording.Frames()[0]);
    assert(recording.Next() == recording.Frames()[1]);
    assert(!recording.Next().has_value());
    recording.ResetReplay();
    assert(recording.Next() == recording.Frames()[0]);
}
} // namespace

int main()
{
    SnapshotTransitionsAreFrameLocal();
    InputServiceCommitsTextAndImeThroughTheSameSnapshot();
    ContextPriorityAndCaptureAreDeterministic();
    ActionsAndFixedTickEdgesResolveOnce();
    VirtualGamepadUsesSnapshotPathAndRejectsStaleIds();
    ProfilesRoundTripAndValidate();
    ProfilesWriteAtomicallyToNonAsciiPaths();
    ProfileConflictsReservationsAndLayeringAreTyped();
    ActionTransitionsAreConsumedAndGamepadAxesHaveEdges();
    NeutralizeReleasesHeldControlsAndPrunesAssignments();
    RecordingReplaysExactly();
    return 0;
}
