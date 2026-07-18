#pragma once

/**
 * @file RuntimeSceneDefinition.h
 * @brief Immutable validated handoff from authoring data to runtime scene construction.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"
#include "Horo/Runtime/Scene/PrimitiveMeshDescriptor.h"
#include "Horo/Runtime/Scene/SceneComponents.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Horo::Runtime
{
/** @brief Stable logical identity of one authored scene. */
struct SceneDefinitionId
{
    std::uint64_t value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }
    [[nodiscard]] constexpr auto operator<=>(const SceneDefinitionId &) const noexcept = default;
};

/** @brief Monotonic authored content revision carried through runtime activation. */
struct SceneDefinitionRevision
{
    std::uint64_t value{};
    [[nodiscard]] constexpr auto operator<=>(const SceneDefinitionRevision &) const noexcept = default;
};

/** @brief Stable authored object identity, independent from process-local entity handles. */
struct SceneObjectId
{
    std::uint64_t value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }
    [[nodiscard]] constexpr auto operator<=>(const SceneObjectId &) const noexcept = default;
};

/** @brief Typed core component payload owned by a definition or runtime scene. */
struct RuntimeComponentSet
{
    std::optional<CameraComponent> camera;
    std::optional<LightComponent> light;
    std::optional<TriggerVolumeComponent> triggerVolume;
    std::optional<AudioSourceComponent> audioSource;
    [[nodiscard]] constexpr auto operator<=>(const RuntimeComponentSet &) const noexcept = default;
};

/** @brief Complete immutable definition of one authored runtime entity. */
struct RuntimeEntityDefinition
{
    SceneObjectId object;
    std::optional<SceneObjectId> parent;
    Math::Transform localTransform;
    std::optional<PrimitiveMeshDescriptor> primitiveMesh;
    RuntimeComponentSet components;
};

/** @brief Validated immutable runtime-scene construction input. */
class RuntimeSceneDefinition final
{
  public:
    /** @brief Returns the current definition schema version. */
    [[nodiscard]] std::uint32_t SchemaVersion() const noexcept;
    /** @brief Returns the stable logical scene identity. */
    [[nodiscard]] SceneDefinitionId Id() const noexcept;
    /** @brief Returns the authored content revision represented by this definition. */
    [[nodiscard]] SceneDefinitionRevision Revision() const noexcept;
    /** @brief Returns every entity definition in stable authored order. */
    [[nodiscard]] std::span<const RuntimeEntityDefinition> Entities() const noexcept;

  private:
    friend class SceneDefinitionBuilder;
    RuntimeSceneDefinition(SceneDefinitionId id, SceneDefinitionRevision revision,
                           std::vector<RuntimeEntityDefinition> entities) noexcept;

    SceneDefinitionId id_;
    SceneDefinitionRevision revision_;
    std::vector<RuntimeEntityDefinition> entities_;
};

/** @brief Mutable load-time builder that validates before producing an immutable definition. */
class SceneDefinitionBuilder final
{
  public:
    /** @brief Creates a builder for one logical scene and authored revision. @param id Non-zero logical scene identity.
     * @param revision Authored content revision. */
    SceneDefinitionBuilder(SceneDefinitionId id, SceneDefinitionRevision revision) noexcept;
    /** @brief Appends one typed entity definition in stable authored order. @param entity Complete authored entity
     * payload. */
    void Add(RuntimeEntityDefinition entity);
    /** @brief Validates identity, hierarchy, numeric values, primitives, and components. @return Immutable definition
     * or the first typed validation error. */
    [[nodiscard]] Result<RuntimeSceneDefinition> Build() &&;

  private:
    SceneDefinitionId id_;
    SceneDefinitionRevision revision_;
    std::vector<RuntimeEntityDefinition> entities_;
};

/** @brief Validates one runtime entity payload independently of hierarchy membership. @param entity Entity payload to
 * validate. @return Success or a typed payload error. */
[[nodiscard]] Result<void> ValidateRuntimeEntityDefinition(const RuntimeEntityDefinition &entity);
} // namespace Horo::Runtime
