#pragma once

/**
 * @file SceneComponents.h
 * @brief Backend-neutral value types for core authored scene-object components.
 */

#include "Horo/Math/SceneMath.h"

#include <cstdint>

namespace Horo::Runtime
{
/** @brief Collider shapes guaranteed by the core physics-facing primitive contract. */
enum class ColliderShapeType : std::uint8_t
{
    Box,
    Sphere,
    Capsule,
    StaticPlane,
};

/** @brief Camera projection authored on a core camera object. */
enum class CameraProjection : std::uint8_t
{
    Perspective,
    Orthographic,
};

/** @brief Backend-neutral authored camera values. */
struct CameraComponent
{
    CameraProjection projection{CameraProjection::Perspective};
    float verticalFieldOfViewRadians{1.0471976F};
    float orthographicHeight{10.0F};
    float nearPlane{0.1F};
    float farPlane{1000.0F};

    [[nodiscard]] constexpr auto operator<=>(const CameraComponent &) const noexcept = default;
};

/** @brief Core light kinds available without a package. */
enum class LightKind : std::uint8_t
{
    Directional,
    Point,
    Spot,
};

/** @brief Backend-neutral authored light values. */
struct LightComponent
{
    LightKind kind{LightKind::Directional};
    Math::Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity{1.0F};
    float range{10.0F};
    float innerConeRadians{0.3490659F};
    float outerConeRadians{0.7853982F};

    [[nodiscard]] constexpr auto operator<=>(const LightComponent &) const noexcept = default;
};

/** @brief Authored overlap volume awaiting runtime physics conversion. */
struct TriggerVolumeComponent
{
    ColliderShapeType shape{ColliderShapeType::Box};

    [[nodiscard]] constexpr auto operator<=>(const TriggerVolumeComponent &) const noexcept = default;
};

/** @brief Source kind recorded before an audio asset or middleware event is assigned. */
enum class AudioSourceKind : std::uint8_t
{
    NativeClip,
    MiddlewareEvent,
};

/** @brief Backend-neutral authored audio-emitter defaults. */
struct AudioSourceComponent
{
    AudioSourceKind kind{AudioSourceKind::NativeClip};
    float gain{1.0F};
    bool spatial{true};

    [[nodiscard]] constexpr auto operator<=>(const AudioSourceComponent &) const noexcept = default;
};
} // namespace Horo::Runtime
