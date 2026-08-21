#pragma once

/**
 * @file AssetSceneDrop.h
 * @brief Typed Content Browser asset drag references, drop policy, and viewport placement.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/Math/SceneMath.h"
#include "editor/document/EditorViewportSceneExtractor.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace Horo::Editor {
    inline constexpr char AssetSceneDragPayloadType[] = "HORO_ASSET_REFERENCE";

    /** @brief Bounded stable reference copied into the process-local ImGui drag payload. */
    struct AssetSceneDragPayload {
        std::array<char, 37> assetId{};
        std::array<char, 64> assetType{};
        std::array<char, 1024> absolutePath{};
        bool registered{};
    };

    /** @brief Destination semantics shared by viewport and hierarchy adapters. */
    enum class AssetSceneDropTarget : std::uint8_t {
        Viewport,
        HierarchyRoot,
        HierarchyChild,
    };

    /** @brief Controller command payload produced by one accepted asset drop. */
    struct AssetSceneDropRequest {
        std::string assetId;
        std::string assetType;
        std::optional<SceneObjectId> parent;
        AssetSceneDropTarget target{AssetSceneDropTarget::HierarchyRoot};
        float normalizedX{0.5F};
        float normalizedY{0.5F};
        float aspect{1.0F};
        Math::ClipDepthRange depthRange{Math::ClipDepthRange::NegativeOneToOne};
        DocumentRevision documentRevision{};
    };

    enum class AssetSceneDropRejection : std::uint8_t {
        None,
        InvalidPayload,
        Unregistered,
        UnsupportedType,
    };

    struct AssetSceneDropPolicyResult {
        bool canInstantiate{};
        AssetSceneDropRejection rejection{AssetSceneDropRejection::InvalidPayload};
    };

    /** @brief Creates a bounded payload from one Content Browser entry. */
    [[nodiscard]] AssetSceneDragPayload MakeAssetSceneDragPayload(std::string_view assetId, std::string_view assetType,
                                                                  std::string_view absolutePath, bool registered) noexcept;

    /** @brief Validates bounded payload termination, version, identity, and supported type. */
    [[nodiscard]] AssetSceneDropPolicyResult EvaluateAssetSceneDrop(const AssetSceneDragPayload &payload) noexcept;

    /** @brief Returns true for asset types that the current authored scene model can instantiate directly. */
    [[nodiscard]] bool CanInstantiateAssetType(std::string_view assetType) noexcept;

    struct AssetViewportPlacementRequest {
        const EditorViewportSceneSnapshot &scene;
        float normalizedX{};
        float normalizedY{};
        float aspect{1.0F};
        Math::ClipDepthRange depthRange{Math::ClipDepthRange::NegativeOneToOne};
        Math::Aabb localBounds{{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
        bool snapToGrid{};
        float gridStep{1.0F};
        float fallbackDistance{5.0F};
    };

    enum class AssetViewportPlacementKind : std::uint8_t {
        Surface,
        GroundPlane,
        CameraFront,
    };

    struct AssetViewportPlacement {
        Math::Vec3 worldPosition{};
        AssetViewportPlacementKind kind{AssetViewportPlacementKind::CameraFront};
    };

    /** @brief Resolves surface, XZ work-plane, and camera-front placement without mutating scene state. */
    [[nodiscard]] Result<AssetViewportPlacement> ResolveAssetViewportPlacement(const AssetViewportPlacementRequest &request);
}  // namespace Horo::Editor
