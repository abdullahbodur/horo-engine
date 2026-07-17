#pragma once

/**
 * @file EditorViewportModel.h
 * @brief Editor-session authority for backend-neutral viewport camera navigation.
 */

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Foundation/Result.h"
#include "editor/document/SceneDocument.h"
#include "editor/project_model/EditorViewportCamera.h"

#include <optional>

namespace Horo::Editor
{
/** @brief Monotonic viewport-state revision within one editor session. */
struct ViewportRevision
{
    std::uint64_t value{0};

    [[nodiscard]] constexpr auto operator<=>(const ViewportRevision &) const noexcept = default;
};

/** @brief Reason the authoritative viewport state changed. */
enum class ViewportChangeKind : std::uint8_t
{
    CameraMoved,
    CameraProjectionChanged,
    CameraFocused,
    ScenePreviewChanged,
};

/** @brief Notification emitted after viewport state commits. */
struct ViewportChangedEvent
{
    static constexpr auto HoroEventTypeName = "ViewportChangedEvent";

    ViewportRevision revision;
    ViewportChangeKind kind{ViewportChangeKind::CameraMoved};
};

/** @brief One frame of camera-relative editor navigation intent. */
struct EditorViewportNavigationDelta
{
    float yawRadians{0.0F};   /**< Rotation around scene up. */
    float pitchRadians{0.0F}; /**< Rotation around current camera right. */
    float moveRight{0.0F};    /**< Camera-local right translation in scene units. */
    float moveUp{0.0F};       /**< Camera-local up translation in scene units. */
    float moveForward{0.0F};  /**< Camera-local forward translation in scene units. */
    float dollyScale{1.0F};   /**< Multiplicative target distance or orthographic-height scale. */
    bool orbit{false};        /**< Keep the target fixed and rotate the camera position around it. */
};

/** @brief Immutable editor viewport state snapshot. */
struct EditorViewportSnapshot
{
    ViewportRevision revision;
    EditorViewportCamera camera;
    std::optional<SceneObjectTransformPreview> transformPreview;
};

/** @brief Owns and validates editor-session camera/navigation state. */
class EditorViewportModel final
{
  public:
    /**
     * @brief Creates a viewport authority with the default editor camera.
     * @param events Borrowed editor-session notification bus that outlives this model.
     */
    explicit EditorViewportModel(EditorDataBus &events) noexcept;

    /** @brief Returns the current immutable viewport snapshot. */
    [[nodiscard]] const EditorViewportSnapshot &Current() const noexcept;

    /**
     * @brief Applies one camera-relative navigation delta atomically.
     * @param delta Finite input delta already scaled by the viewport input mapper.
     * @return Success, or a typed validation error without changing the camera.
     */
    [[nodiscard]] Result<void> Navigate(const EditorViewportNavigationDelta &delta);

    /** @brief Changes editor projection while preserving apparent scale at the orbit target. */
    [[nodiscard]] Result<void> SetProjection(Runtime::CameraProjection projection);

    /** @brief Frames validated world bounds with a deterministic margin. */
    [[nodiscard]] Result<void> Focus(const Math::Aabb &worldBounds, float aspect);

    /**
     * @brief Commits one transient transform override to authoritative viewport workspace state.
     * @param preview Stable object identity and finite local transform to preview.
     * @return Success, or a typed validation error without changing preview state.
     */
    [[nodiscard]] Result<void> SetTransformPreview(const SceneObjectTransformPreview &preview);

    /**
     * @brief Clears the active transient transform override.
     * @return True when preview state changed and a notification was published.
     */
    bool ClearTransformPreview();

  private:
    EditorDataBus *events_{nullptr};
    EditorViewportSnapshot current_{};
};
} // namespace Horo::Editor
