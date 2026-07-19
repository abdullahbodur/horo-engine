#pragma once

/**
 * @file PrimitiveCatalog.h
 * @brief Stable typed identities and metadata for Horo's built-in scene primitives.
 */

#include "Horo/Runtime/Scene/SceneComponents.h"

#include <optional>
#include <span>
#include <string_view>

namespace Horo::Runtime
{
/** @brief Stable serialized identity of a built-in scene primitive. */
struct PrimitiveId
{
    std::string_view value;

    /** @brief Reports whether the identity contains a non-empty stable token. */
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return !value.empty();
    }

    [[nodiscard]] constexpr auto operator<=>(const PrimitiveId &) const noexcept = default;
};

/** @brief High-level category of a built-in scene primitive. */
enum class PrimitiveCategory : std::uint8_t
{
    Mesh,
    Collider,
    SceneObject,
};

/** @brief Stable creation-menu group for a built-in primitive. */
enum class PrimitiveCreationGroup : std::uint8_t
{
    Root,
    Objects3D,
    Cameras,
    Lights,
    Volumes,
    Audio,
    NotCreatable,
};

/** @brief Typed logical kind for core scene-object primitives. */
enum class SceneObjectPrimitiveType : std::uint8_t
{
    Empty,
    Camera,
    DirectionalLight,
    PointLight,
    SpotLight,
    TriggerVolume,
    AudioSource,
};

/** @brief Procedural mesh generators guaranteed by the core runtime. */
enum class PrimitiveMeshType : std::uint8_t
{
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    Plane,
    Quad,
};

/** @brief Stable metadata for one built-in primitive. */
struct PrimitiveDescriptor
{
    PrimitiveId id;
    PrimitiveCategory category{PrimitiveCategory::Mesh};
    PrimitiveCreationGroup creationGroup{PrimitiveCreationGroup::NotCreatable};
    std::string_view displayName;
    std::string_view defaultObjectName;
    std::string_view iconToken;
    bool isRenderable{false};
    bool isPhysicsSolidByDefault{false};
    std::optional<PrimitiveMeshType> meshType;
    std::optional<ColliderShapeType> defaultCollider;
    std::optional<SceneObjectPrimitiveType> sceneObjectType;
};

/** @brief Read-only access to the authoritative built-in primitive registry. */
class PrimitiveCatalog final
{
  public:
    /** @brief Returns every core primitive in stable catalog order. */
    [[nodiscard]] static std::span<const PrimitiveDescriptor> All() noexcept;

    /** @brief Looks up a primitive by its stable serialized identifier. */
    [[nodiscard]] static const PrimitiveDescriptor *Find(std::string_view id) noexcept;

    /** @brief Looks up the descriptor for a procedural mesh type. */
    [[nodiscard]] static const PrimitiveDescriptor *Find(PrimitiveMeshType meshType) noexcept;
};
} // namespace Horo::Runtime
