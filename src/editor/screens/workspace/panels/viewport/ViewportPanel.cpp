#include "ViewportPanel.h"

#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/AssetSceneDrop.h"
#include "visualizers/light/LightMarkerLayer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] std::optional<AssetSceneDragPayload> ReadAssetPayload(const ImGuiPayload *payload) {
            if (payload == nullptr || !payload->IsDataType(AssetSceneDragPayloadType) || payload->Data == nullptr ||
                payload->DataSize != sizeof(AssetSceneDragPayload))
                return std::nullopt;
            AssetSceneDragPayload result;
            std::memcpy(&result, payload->Data, sizeof(result));
            if (result.assetId.back() != '\0' || result.assetType.back() != '\0')
                return std::nullopt;
            return result;
        }
    }  // namespace

    /** @copydoc ViewportPanel::OnAttach */
    void ViewportPanel::OnAttach(PanelContext &context) {
        viewportRenderer_ = context.viewportRenderer;
        if (context.inputRouter != nullptr && context.workspaceInputContext != nullptr)
            interaction_.Attach(*context.inputRouter, *context.workspaceInputContext);
    }

    /** @copydoc ViewportPanel::OnDetach */
    void ViewportPanel::OnDetach() {
        interaction_.Detach();
        viewportRenderer_ = nullptr;
    }

    /** @copydoc ViewportPanel::DrawIcon */
    void ViewportPanel::DrawIcon(ImDrawList *drawList, const ImVec2 &position, const ImVec2 &size, const ImU32 color) {
        const float originX = position.x + (size.x - 14.0F) * 0.5F;
        const float originY = position.y + (size.y - 14.0F) * 0.5F;
        drawList->AddRect(ImVec2(originX + 2.0F, originY + 3.0F), ImVec2(originX + 12.0F, originY + 11.0F), color, 1.0F, 0, 1.5F);
        drawList->AddCircle(ImVec2(originX + 7.0F, originY + 7.0F), 2.0F, color, 0, 1.5F);
    }

    /** @copydoc ViewportPanel::DrawPanel */
    void ViewportPanel::DrawPanel(const ImVec2 &position, const ImVec2 &size, const EditorWorkspaceViewModel &viewModel,
                                  EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        const std::array tabNames{context.localization.Get("editor", "workspace.panel.viewport").c_str()};
        Ui::DrawDockTabs(tabNames, 0, context.theme.fonts);

        constexpr float tabBarHeight = 28.0F;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::BeginChild("##Content", ImVec2(size.x, size.y - tabBarHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

        ImDrawList &drawList = *ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = size.x;
        const float height = size.y - tabBarHeight;
        const float centerX = origin.x + width * 0.5F;
        const float horizon = origin.y + height * 0.38F;
        const float ground = origin.y + height;

        RequestViewportExtent(width, height);
        if (viewportRenderer_ != nullptr) {
            viewportRenderer_->RequestGrid(EditorViewportGridOptions{
                .visible = context.settings.settings.gridOverlay,
            });
            EditorViewportLightVisualizerOptions lightVisualizer;
            if (viewModel.primarySelection.has_value()) {
                const auto selected =
                    std::ranges::find(viewModel.viewportLights, *viewModel.primarySelection, &ViewportLightPresentation::object);
                if (selected != viewModel.viewportLights.end())
                    lightVisualizer.selectedLight = selected->light;
            }
            viewportRenderer_->RequestLightVisualizer(lightVisualizer);
        }
        const EditorViewportTextureView textureView =
            viewportRenderer_ != nullptr ? viewportRenderer_->TextureView() : EditorViewportTextureView{};
        const bool hasRenderedViewport = viewportRenderer_ != nullptr && viewportRenderer_->IsReady() && textureView.IsValid();
        DrawViewportSurface(drawList, origin, width, height, centerX, horizon, ground, textureView, hasRenderedViewport);

        if (hasRenderedViewport && width > 0.0F && height > 0.0F) {
            const ImVec2 projectionMinimum{origin.x + 10.0F, origin.y + 8.0F};
            const ImVec2 projectionMaximum{
                projectionMinimum.x + 190.0F,
                projectionMinimum.y + ImGui::GetFontSize() + 14.0F,
            };
            const ImVec2 mouse = ImGui::GetMousePos();
            const bool pointerOverProjection = mouse.x >= projectionMinimum.x && mouse.x <= projectionMaximum.x &&
                                               mouse.y >= projectionMinimum.y && mouse.y <= projectionMaximum.y;
            bool surfaceHovered = false;
            if (!pointerOverProjection || interaction_.IsActive()) {
                ImGui::SetCursorScreenPos(origin);
                ImGui::InvisibleButton("##ViewportSurface", ImVec2(width, height),
                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                           ImGuiButtonFlags_MouseButtonMiddle);
                surfaceHovered = ImGui::IsItemHovered();
            }
            const Math::ClipDepthRange depthRange = viewportRenderer_->ClipDepthRange();
            bool assetDragActive = false;
            if ((!pointerOverProjection || interaction_.IsActive()) && ImGui::BeginDragDropTarget()) {
                const ImGuiPayload *accepted =
                    ImGui::AcceptDragDropPayload(AssetSceneDragPayloadType,
                                                 ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                const std::optional<AssetSceneDragPayload> payload = ReadAssetPayload(accepted);
                if (payload.has_value()) {
                    assetDragActive = true;
                    const AssetSceneDropPolicyResult policy = EvaluateAssetSceneDrop(*payload);
                    drawList.AddRect(origin, {origin.x + width, origin.y + height},
                                     Theme::U32(policy.canInstantiate ? Theme::Accent() : Theme::Err()), 3.0F, 0, 2.0F);
                    if (accepted->IsDelivery()) {
                        const ImVec2 dropMousePos = ImGui::GetMousePos();
                        command.command = EditorWorkspaceViewCommand::InstantiateAsset;
                        command.assetSceneDrop = AssetSceneDropRequest{
                            .assetId = payload->assetId.data(),
                            .assetType = payload->assetType.data(),
                            .parent = viewModel.primarySelection,
                            .target = AssetSceneDropTarget::Viewport,
                            .normalizedX = std::clamp((dropMousePos.x - origin.x) / width, 0.0F, 1.0F),
                            .normalizedY = std::clamp((dropMousePos.y - origin.y) / height, 0.0F, 1.0F),
                            .aspect = width / height,
                            .depthRange = depthRange,
                            .documentRevision = viewModel.documentRevision,
                        };
                    }
                }
                ImGui::EndDragDropTarget();
            }
            const std::optional<SceneObjectId> clickedLight =
                DrawViewportLightMarkers(drawList, origin, width, height, viewModel.viewportCamera, viewModel.viewportLights,
                                         viewModel.primarySelection, surfaceHovered && !interaction_.IsActive() && !assetDragActive,
                                         depthRange);
            if (clickedLight.has_value()) {
                command.command = EditorWorkspaceViewCommand::SelectObject;
                command.objectPayload = *clickedLight;
            } else if ((!pointerOverProjection || interaction_.IsActive()) && !assetDragActive) {
                interaction_.Draw(drawList, origin, width, height, surfaceHovered, viewModel, command, context, ImGui::GetIO().DeltaTime,
                                  depthRange);
            }
        }

        DrawProjectionControl(origin, viewModel, command, context);
        DrawObjectCount(origin, viewModel, context);
        if (!hasRenderedViewport)
            DrawMissingRendererMessage(centerX, origin.y, height, context);

        ImGui::EndChild();
        ImGui::PopStyleVar();
        static_cast<void>(position);
    }

    void ViewportPanel::RequestViewportExtent(const float width, const float height) const noexcept {
        if (viewportRenderer_ == nullptr)
            return;
        const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
        const bool renderable = width > 0.0F && height > 0.0F && framebufferScale.x > 0.0F && framebufferScale.y > 0.0F;
        viewportRenderer_->RequestExtent(
            renderable
                ? EditorViewportExtent{
                      .width = static_cast<std::uint32_t>(
                          std::max(1.0F, width * framebufferScale.x)),
                      .height = static_cast<std::uint32_t>(
                          std::max(1.0F, height * framebufferScale.y)),
                  }
                : EditorViewportExtent{});
    }

    void ViewportPanel::DrawViewportSurface(ImDrawList &drawList, const ImVec2 &origin, const float width, const float height,
                                            const float centerX, const float horizon, const float ground,
                                            const EditorViewportTextureView &textureView, const bool hasRenderedViewport) {
        if (hasRenderedViewport) {
            const auto texture = static_cast<ImTextureID>(textureView.textureId);
            drawList.AddImage(texture, origin, ImVec2(origin.x + width, origin.y + height), ImVec2(textureView.u0, textureView.v0),
                              ImVec2(textureView.u1, textureView.v1));
            return;
        }

        drawList.AddRectFilledMultiColor(origin, ImVec2(origin.x + width, origin.y + height),
                                         ImGui::GetColorU32(ImVec4(0.05F, 0.06F, 0.09F, 1.0F)),
                                         ImGui::GetColorU32(ImVec4(0.05F, 0.06F, 0.09F, 1.0F)),
                                         ImGui::GetColorU32(ImVec4(0.09F, 0.11F, 0.15F, 1.0F)),
                                         ImGui::GetColorU32(ImVec4(0.09F, 0.11F, 0.15F, 1.0F)));

        const ImU32 gridColor = ImGui::GetColorU32(ImVec4(0.16F, 0.20F, 0.27F, 1.0F));
        constexpr int lineCount = 14;
        for (int line = 0; line <= lineCount; ++line) {
            const float ratio = static_cast<float>(line) / lineCount;
            const float xOffset = (ratio - 0.5F) * width;
            drawList.AddLine(ImVec2(centerX + xOffset, ground), ImVec2(centerX, horizon), gridColor, 0.7F);

            const float yPosition = ground - ratio * (ground - horizon);
            const float halfWidth = width * (1.0F - ratio * 0.90F) * 0.5F;
            drawList.AddLine(ImVec2(centerX - halfWidth, yPosition), ImVec2(centerX + halfWidth, yPosition), gridColor, 0.7F);
        }
        drawList.AddRectFilledMultiColor(ImVec2(origin.x, horizon - 12.0F), ImVec2(origin.x + width, horizon + 22.0F),
                                         ImGui::GetColorU32(ImVec4(0.01F, 0.22F, 0.44F, 0.0F)),
                                         ImGui::GetColorU32(ImVec4(0.01F, 0.22F, 0.44F, 0.0F)),
                                         ImGui::GetColorU32(ImVec4(0.03F, 0.38F, 0.60F, 0.35F)),
                                         ImGui::GetColorU32(ImVec4(0.03F, 0.38F, 0.60F, 0.35F)));
    }

    void ViewportPanel::DrawProjectionControl(const ImVec2 &origin, const EditorWorkspaceViewModel &viewModel,
                                              EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0F, origin.y + 8.0F));
        const char *projectionItems[]{
            context.localization.Get("editor", "workspace.viewport.perspective_shaded").c_str(),
            context.localization.Get("editor", "workspace.viewport.orthographic_shaded").c_str(),
        };
        int projectionIndex = viewModel.viewportCamera.projection == Runtime::CameraProjection::Perspective ? 0 : 1;
        ImGui::PushItemWidth(190.0F);
        if (Ui::ComboControl("viewport_projection", &projectionIndex, projectionItems, 2, context.theme.fonts)) {
            command.command = EditorWorkspaceViewCommand::ChangeViewportProjection;
            command.viewportProjectionPayload =
                projectionIndex == 0 ? Runtime::CameraProjection::Perspective : Runtime::CameraProjection::Orthographic;
        }
        ImGui::PopItemWidth();
    }

    void ViewportPanel::DrawObjectCount(const ImVec2 &origin, const EditorWorkspaceViewModel &viewModel, const EditorGuiContext &context) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0F, origin.y + 42.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.32F, 0.38F, 0.48F, 1.0F));
        const std::size_t objectCountValue = viewModel.objects.size();
        const std::string objectCount =
            std::vformat(context.localization.Get("editor", "workspace.viewport.object_count"), std::make_format_args(objectCountValue));
        ImGui::TextUnformatted(objectCount.c_str());
        ImGui::PopStyleColor();
    }

    void ViewportPanel::DrawMissingRendererMessage(const float centerX, const float originY, const float height,
                                                   const EditorGuiContext &context) {
        const std::string &message = context.localization.Get("editor", "workspace.viewport.renderer_missing");
        const float messageWidth = ImGui::CalcTextSize(message.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(centerX - messageWidth * 0.5F, originY + height * 0.52F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.28F, 0.32F, 0.40F, 1.0F));
        ImGui::TextUnformatted(message.c_str());
        ImGui::PopStyleColor();
    }
}  // namespace Horo::Editor
