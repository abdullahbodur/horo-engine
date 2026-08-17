#include "LightMarkerLayer.h"

#include "Horo/Editor/EditorIcons.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "editor/renderer/EditorViewportScene.h"

namespace Horo::Editor {
    namespace {
        [[nodiscard]] std::optional<ImVec2> Project(const EditorViewportCamera &camera, const Math::Vec3 world, const ImVec2 origin,
                                                    const float width, const float height, const Math::ClipDepthRange depthRange) noexcept {
            if (width <= 0.0F || height <= 0.0F)
                return std::nullopt;
            const Result<Math::Mat4> viewProjection = BuildEditorViewportViewProjection(camera, width / height, depthRange);
            if (viewProjection.HasError())
                return std::nullopt;
            const Result<Math::Vec3> projected = Math::TryProject(viewProjection.Value(), world);
            if (const float minimumDepth = depthRange == Math::ClipDepthRange::NegativeOneToOne ? -1.0F : 0.0F;
                projected.HasError() || projected.Value().z < minimumDepth || projected.Value().z > 1.0F)
                return std::nullopt;
            return ImVec2{
                origin.x + (projected.Value().x * 0.5F + 0.5F) * width,
                origin.y + (0.5F - projected.Value().y * 0.5F) * height,
            };
        }

        [[nodiscard]] Ui::UiIcon IconFor(const Render::RenderLightKind kind) noexcept {
            switch (kind) {
                case Render::RenderLightKind::Directional:
                    return Ui::UiIcon::DirectionalLight;
                case Render::RenderLightKind::Point:
                    return Ui::UiIcon::PointLight;
                case Render::RenderLightKind::Spot:
                    return Ui::UiIcon::SpotLight;
            }
            return Ui::UiIcon::PointLight;
        }

        [[nodiscard]] float DistanceSquared(const ImVec2 left, const ImVec2 right) noexcept {
            const float x = left.x - right.x;
            const float y = left.y - right.y;
            return x * x + y * y;
        }
    }  // namespace

    std::optional<SceneObjectId> DrawViewportLightMarkers(ImDrawList &drawList, const ImVec2 origin, const float width, const float height,
                                                          const EditorViewportCamera &camera,
                                                          const std::span<const ViewportLightPresentation> lights,
                                                          const std::optional<SceneObjectId> primarySelection, const bool acceptInput,
                                                          const Math::ClipDepthRange depthRange) {
        constexpr float markerSize = 22.0F;
        constexpr float hitRadius = 14.0F;
        const ImVec2 pointer = ImGui::GetMousePos();
        std::optional<SceneObjectId> clicked;
        float closestHit = hitRadius * hitRadius;

        for (const ViewportLightPresentation &presentation : lights) {
            const std::optional<ImVec2> anchor = Project(camera, presentation.light.position, origin, width, height, depthRange);
            if (!anchor.has_value())
                continue;

            const bool selected = primarySelection == presentation.object;
            const ImVec2 center = *anchor;

            const ImU32 background = Theme::U32(selected ? Theme::AccentSoft() : Theme::Bg2());
            const ImU32 border = Theme::U32(selected ? Theme::Accent() : Theme::BorderStrong());
            drawList.AddCircleFilled(center, 13.0F, background, 24);
            drawList.AddCircle(center, 13.0F, border, 24, selected ? 2.0F : 1.2F);
            Ui::DrawEditorIcon(&drawList, IconFor(presentation.light.kind), {center.x - markerSize * 0.5F, center.y - markerSize * 0.5F},
                               {markerSize, markerSize}, Theme::U32(Theme::Text()));

            if (acceptInput && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (const float distance = DistanceSquared(pointer, center); distance <= closestHit) {
                    closestHit = distance;
                    clicked = presentation.object;
                }
            }
        }
        return clicked;
    }
}  // namespace Horo::Editor
