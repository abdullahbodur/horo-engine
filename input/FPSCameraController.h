#pragma once
#include "math/Vec3.h"
#include "renderer/Camera.h"

namespace Horo {
    // Stateless first-person camera controller.
    // Owns yaw/pitch/position state; reads Input::GetMouseDelta() internally on
    // Update().
    class FPSCameraController {
    public:
        // Call once per frame while game input is active.
        // Reads Input::GetMouseDelta() and integrates yaw/pitch.
        void Update(float dt);

        void SetPosition(const Vec3 &pos);

        void SetYaw(float yaw);

        void SetPitch(float pitch);

        void SetMouseSensitivity(float v) { m_mouseSensitivity = v; }
        void SetPitchMin(float v) { m_pitchMin = v; }
        void SetPitchMax(float v) { m_pitchMax = v; }

        // Writes camera.position and camera.target based on current state.
        void ApplyToCamera(Camera &camera) const;

        float GetYaw() const { return m_yaw; }
        float GetPitch() const { return m_pitch; }

    private:
        float m_mouseSensitivity = 0.15f;
        float m_pitchMin = -70.0f;
        float m_pitchMax = 70.0f;
        float m_yaw = 0.0f;
        float m_pitch = 0.0f;
        Vec3 m_position{0.0f, 0.0f, 0.0f};
    };
} // namespace Horo
