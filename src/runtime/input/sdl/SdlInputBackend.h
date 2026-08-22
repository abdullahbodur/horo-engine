#pragma once

#include "Horo/Runtime/Input.h"

#include <SDL3/SDL.h>
#include <memory>

namespace Horo::Input {
    /** @brief SDL3 platform collector and baseline haptics adapter. Native types remain private to this target. */
    class SdlInputBackend final : public IGamepadHaptics {
    public:
        SdlInputBackend();
        ~SdlInputBackend() override;
        SdlInputBackend(const SdlInputBackend &) = delete;
        SdlInputBackend &operator=(const SdlInputBackend &) = delete;

        void BeginFrame(FrameNumber frame);
        void ProcessEvent(const SDL_Event &event);
        [[nodiscard]] const RawInputSnapshot &Commit();
        [[nodiscard]] RawInputCollector &Collector() noexcept;

        [[nodiscard]] Result<void> PlayRumble(GamepadDeviceId id, RumbleEffect effect) override;
        [[nodiscard]] Result<void> Stop(GamepadDeviceId id) override;

        /** @brief Pimpl state. The type is public so file-local event helpers can take it; data stays encapsulated behind impl_. */
        struct Impl;

    private:
        std::unique_ptr<Impl> impl_;
    };
}  // namespace Horo::Input
