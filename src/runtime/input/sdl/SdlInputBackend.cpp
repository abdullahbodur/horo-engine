#include "SdlInputBackend.h"

#include <algorithm>
#include <unordered_map>

namespace Horo::Input {
    namespace {
        const ErrorDomainId SdlInputDomain{"horo.input.sdl"};
        const ErrorCodeDescriptor HapticsStaleDevice{
            SdlInputDomain, ErrorCode{"input.haptics.stale_device"}, ErrorSeverity::Error,
            "Gamepad is unavailable.", "Reconnect the gamepad before requesting haptics.", true, true
        };
        const ErrorCodeDescriptor HapticsUnsupported{
            SdlInputDomain, ErrorCode{"input.haptics.unsupported"}, ErrorSeverity::Error,
            "Gamepad haptics are unsupported.", "Use a device with haptics support.", false, true
        };
        const ErrorCodeDescriptor HapticsFailed{
            SdlInputDomain, ErrorCode{"input.haptics.failed"}, ErrorSeverity::Error,
            "Gamepad haptics could not start.", "Verify the device connection and retry.", true, true
        };

        Key MapKey(const SDL_Scancode key) noexcept {
            if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z)
                return static_cast<Key>(static_cast<int>(Key::A) + key - SDL_SCANCODE_A);
            if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9)
                return static_cast<Key>(static_cast<int>(Key::Digit1) + key - SDL_SCANCODE_1);
            switch (key) {
                case SDL_SCANCODE_0: return Key::Digit0;
                case SDL_SCANCODE_ESCAPE: return Key::Escape;
                case SDL_SCANCODE_RETURN: return Key::Enter;
                case SDL_SCANCODE_TAB: return Key::Tab;
                case SDL_SCANCODE_SPACE: return Key::Space;
                case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
                case SDL_SCANCODE_DELETE: return Key::Delete;
                case SDL_SCANCODE_LEFT: return Key::Left;
                case SDL_SCANCODE_RIGHT: return Key::Right;
                case SDL_SCANCODE_UP: return Key::Up;
                case SDL_SCANCODE_DOWN: return Key::Down;
                case SDL_SCANCODE_HOME: return Key::Home;
                case SDL_SCANCODE_END: return Key::End;
                case SDL_SCANCODE_PAGEUP: return Key::PageUp;
                case SDL_SCANCODE_PAGEDOWN: return Key::PageDown;
                case SDL_SCANCODE_F1: return Key::F1;
                case SDL_SCANCODE_F2: return Key::F2;
                case SDL_SCANCODE_F3: return Key::F3;
                case SDL_SCANCODE_F4: return Key::F4;
                case SDL_SCANCODE_F5: return Key::F5;
                case SDL_SCANCODE_F6: return Key::F6;
                case SDL_SCANCODE_F7: return Key::F7;
                case SDL_SCANCODE_F8: return Key::F8;
                case SDL_SCANCODE_F9: return Key::F9;
                case SDL_SCANCODE_F10: return Key::F10;
                case SDL_SCANCODE_F11: return Key::F11;
                case SDL_SCANCODE_F12: return Key::F12;
                default: return Key::Unknown;
            }
        }

        std::optional<PointerButton> MapPointerButton(const std::uint8_t button) noexcept {
            switch (button) {
                case SDL_BUTTON_LEFT: return PointerButton::Primary;
                case SDL_BUTTON_RIGHT: return PointerButton::Secondary;
                case SDL_BUTTON_MIDDLE: return PointerButton::Middle;
                case SDL_BUTTON_X1: return PointerButton::Auxiliary1;
                case SDL_BUTTON_X2: return PointerButton::Auxiliary2;
                default: return std::nullopt;
            }
        }

        std::optional<GamepadButton> MapGamepadButton(const std::uint8_t button) noexcept {
            switch (static_cast<SDL_GamepadButton>(button)) {
                case SDL_GAMEPAD_BUTTON_SOUTH: return GamepadButton::South;
                case SDL_GAMEPAD_BUTTON_EAST: return GamepadButton::East;
                case SDL_GAMEPAD_BUTTON_WEST: return GamepadButton::West;
                case SDL_GAMEPAD_BUTTON_NORTH: return GamepadButton::North;
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GamepadButton::LeftShoulder;
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GamepadButton::RightShoulder;
                case SDL_GAMEPAD_BUTTON_LEFT_STICK: return GamepadButton::LeftStick;
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return GamepadButton::RightStick;
                case SDL_GAMEPAD_BUTTON_START: return GamepadButton::Start;
                case SDL_GAMEPAD_BUTTON_BACK: return GamepadButton::Select;
                case SDL_GAMEPAD_BUTTON_DPAD_UP: return GamepadButton::DPadUp;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GamepadButton::DPadDown;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GamepadButton::DPadLeft;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return GamepadButton::DPadRight;
                default: return std::nullopt;
            }
        }

        std::optional<GamepadAxis> MapGamepadAxis(const std::uint8_t axis) noexcept {
            switch (static_cast<SDL_GamepadAxis>(axis)) {
                case SDL_GAMEPAD_AXIS_LEFTX: return GamepadAxis::LeftX;
                case SDL_GAMEPAD_AXIS_LEFTY: return GamepadAxis::LeftY;
                case SDL_GAMEPAD_AXIS_RIGHTX: return GamepadAxis::RightX;
                case SDL_GAMEPAD_AXIS_RIGHTY: return GamepadAxis::RightY;
                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return GamepadAxis::LeftTrigger;
                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return GamepadAxis::RightTrigger;
                default: return std::nullopt;
            }
        }

        Error SdlError(const ErrorCodeDescriptor &descriptor, const char *fallback) {
            const char *native = SDL_GetError();
            return MakeError(descriptor, native != nullptr && *native != '\0' ? native : fallback);
        }
    } // namespace

    struct SdlInputBackend::Impl {
        struct Device {
            GamepadDeviceId id;
            SDL_Gamepad *gamepad{nullptr};
            SDL_Joystick *joystick{nullptr};
        };

        RawInputCollector collector;
        std::unordered_map<SDL_JoystickID, Device> devices;
        WindowInputState windowState{};
    };

    SdlInputBackend::SdlInputBackend() : impl_(std::make_unique<Impl>()) {
    }

    SdlInputBackend::~SdlInputBackend() {
        for (auto &[nativeId, device]: impl_->devices) {
            static_cast<void>(nativeId);
            if (device.gamepad) SDL_CloseGamepad(device.gamepad);
            else if (device.joystick) SDL_CloseJoystick(device.joystick);
        }
    }

    void SdlInputBackend::BeginFrame(const FrameNumber frame) { impl_->collector.BeginFrame(frame); }

    void SdlInputBackend::ProcessEvent(const SDL_Event &event) {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (!event.key.repeat) impl_->collector.SetKey(MapKey(event.key.scancode), event.key.down);
                impl_->collector.SetModifiers(ModifierState{
                    .control = (event.key.mod & SDL_KMOD_CTRL) != 0,
                    .shift = (event.key.mod & SDL_KMOD_SHIFT) != 0,
                    .alt = (event.key.mod & SDL_KMOD_ALT) != 0,
                    .command = (event.key.mod & SDL_KMOD_GUI) != 0,
                });
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (event.text.text) impl_->collector.AppendText(event.text.text);
                break;
            case SDL_EVENT_TEXT_EDITING:
                impl_->collector.SetTextComposition(event.edit.text != nullptr ? event.edit.text : "", event.edit.start,
                                                    event.edit.length);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                impl_->collector.SetPointerPosition(event.motion.x, event.motion.y);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (const auto button = MapPointerButton(event.button.button))
                    impl_->collector.SetPointerButton(*button, event.button.down);
                impl_->collector.SetPointerPosition(event.button.x, event.button.y);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                impl_->collector.AddPointerWheel(
                    event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.x : event.wheel.x,
                    event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y);
                break;
            case SDL_EVENT_MOUSE_ADDED:
                impl_->windowState.pointerDeviceAvailable = true;
                impl_->collector.SetWindowState(impl_->windowState);
                break;
            case SDL_EVENT_MOUSE_REMOVED:
                // The public snapshot intentionally does not expose SDL mouse IDs. A
                // removal conservatively invalidates the active pointer capture; a
                // subsequent add event restores pointer availability.
                impl_->windowState.pointerDeviceAvailable = false;
                impl_->collector.SetWindowState(impl_->windowState);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                impl_->windowState.focused = true;
                impl_->collector.SetWindowState(impl_->windowState);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                impl_->collector.Neutralize();
                impl_->windowState.focused = false;
                impl_->collector.SetWindowState(impl_->windowState);
                break;
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
                impl_->windowState.pointerInside = true;
                impl_->collector.SetWindowState(impl_->windowState);
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                impl_->windowState.pointerInside = false;
                impl_->collector.SetWindowState(impl_->windowState);
                break;
            case SDL_EVENT_GAMEPAD_ADDED: {
                if (impl_->devices.contains(event.gdevice.which)) break;
                SDL_Gamepad *gamepad = SDL_OpenGamepad(event.gdevice.which);
                if (!gamepad) break;
                const char *name = SDL_GetGamepadName(gamepad);
                const GamepadDeviceId id = impl_->collector.ConnectGamepad(name ? name : "SDL Gamepad", true);
                impl_->devices.emplace(event.gdevice.which, Impl::Device{id, gamepad, nullptr});
                break;
            }
            case SDL_EVENT_GAMEPAD_REMOVED: {
                const auto found = impl_->devices.find(event.gdevice.which);
                if (found == impl_->devices.end()) break;
                (void) impl_->collector.DisconnectGamepad(found->second.id);
                if (found->second.gamepad) SDL_CloseGamepad(found->second.gamepad);
                impl_->devices.erase(found);
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                const auto found = impl_->devices.find(event.gbutton.which);
                const auto button = MapGamepadButton(event.gbutton.button);
                if (found != impl_->devices.end() && button)
                    (void) impl_->collector.SetGamepadButton(found->second.id, *button, event.gbutton.down);
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                const auto found = impl_->devices.find(event.gaxis.which);
                const auto axis = MapGamepadAxis(event.gaxis.axis);
                if (found != impl_->devices.end() && axis) {
                    const float normalized = event.gaxis.value < 0
                                                 ? static_cast<float>(event.gaxis.value) / 32768.0F
                                                 : static_cast<float>(event.gaxis.value) / 32767.0F;
                    (void) impl_->collector.SetGamepadAxis(found->second.id, *axis, normalized);
                }
                break;
            }
            case SDL_EVENT_JOYSTICK_ADDED: {
                if (impl_->devices.contains(event.jdevice.which) || SDL_IsGamepad(event.jdevice.which)) break;
                SDL_Joystick *joystick = SDL_OpenJoystick(event.jdevice.which);
                if (!joystick) break;
                const char *name = SDL_GetJoystickName(joystick);
                const std::size_t buttons = static_cast<std::size_t>(std::clamp(
                    SDL_GetNumJoystickButtons(joystick), 0, 256));
                const std::size_t axes = static_cast<std::size_t>(std::clamp(SDL_GetNumJoystickAxes(joystick), 0, 256));
                const GamepadDeviceId id =
                        impl_->collector.ConnectGamepad(name ? name : "SDL Joystick", false, buttons, axes);
                impl_->devices.emplace(event.jdevice.which, Impl::Device{id, nullptr, joystick});
                break;
            }
            case SDL_EVENT_JOYSTICK_REMOVED: {
                const auto found = impl_->devices.find(event.jdevice.which);
                if (found == impl_->devices.end()) break;
                (void) impl_->collector.DisconnectGamepad(found->second.id);
                if (found->second.gamepad) SDL_CloseGamepad(found->second.gamepad);
                else if (found->second.joystick) SDL_CloseJoystick(found->second.joystick);
                impl_->devices.erase(found);
                break;
            }
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP: {
                if (const auto found = impl_->devices.find(event.jbutton.which);
                    found != impl_->devices.end() && found->second.joystick)
                    (void) impl_->collector.SetRawGamepadButton(found->second.id, event.jbutton.button,
                                                                event.jbutton.down);
                break;
            }
            case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
                if (const auto found = impl_->devices.find(event.jaxis.which);
                    found != impl_->devices.end() && found->second.joystick) {
                    const float normalized = event.jaxis.value < 0
                                                 ? static_cast<float>(event.jaxis.value) / 32768.0F
                                                 : static_cast<float>(event.jaxis.value) / 32767.0F;
                    (void) impl_->collector.SetRawGamepadAxis(found->second.id, event.jaxis.axis, normalized);
                }
                break;
            }
            default: break;
        }
    }

    const RawInputSnapshot &SdlInputBackend::Commit() { return impl_->collector.Commit(); }
    RawInputCollector &SdlInputBackend::Collector() noexcept { return impl_->collector; }

    Result<void> SdlInputBackend::PlayRumble(const GamepadDeviceId id, const RumbleEffect effect) {
        const auto found = std::ranges::find_if(impl_->devices, [id](const auto &entry) {
            return entry.second.id == id;
        });
        if (found == impl_->devices.end())
            return Result<void>::Failure(SdlError(HapticsStaleDevice, "Gamepad is unavailable."));
        if (!found->second.gamepad)
            return Result<void>::Failure(SdlError(HapticsUnsupported, "Gamepad haptics are unsupported."));
        const auto amplitude = [](const float value) {
            return static_cast<std::uint16_t>(std::clamp(value, 0.0F, 1.0F) * 65535.0F);
        };
        if (!SDL_RumbleGamepad(found->second.gamepad, amplitude(effect.lowFrequency), amplitude(effect.highFrequency),
                               effect.durationMilliseconds))
            return Result<void>::Failure(SdlError(HapticsFailed, "Unable to start gamepad rumble."));
        return Result<void>::Success();
    }

    Result<void> SdlInputBackend::Stop(const GamepadDeviceId id) { return PlayRumble(id, {}); }
} // namespace Horo::Input
