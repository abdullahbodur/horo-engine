/** @copydoc EditorIcons.h */

#include "Horo/Editor/EditorIcons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <ranges>

namespace Horo::Editor::Ui {
    namespace {
        struct IconDrawContext {
            ImDrawList &drawList;
            ImVec2 position;
            ImVec2 size;
            ImU32 color;

            [[nodiscard]] ImVec2 Center() const noexcept {
                return {position.x + size.x * 0.5F, position.y + size.y * 0.5F};
            }
        };

        using IconRenderer = void (*)(const IconDrawContext &);

        struct IconDescriptor {
            std::string_view token;
            IconRenderer renderer;
        };

        void DrawNothing(const IconDrawContext &) {}

        void DrawGenericIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            const ImVec2 center = context.Center();
            context.drawList.AddRect({x + 2.0F, y + 3.0F}, {x + w - 3.0F, y + h - 2.0F}, context.color, 1.0F, 0, 1.3F);
            context.drawList.AddLine({x + 2.0F, y + 3.0F}, {center.x, y}, context.color, 1.1F);
            context.drawList.AddLine({x + w - 3.0F, y + 3.0F}, {center.x, y}, context.color, 1.1F);
        }

        void DrawCreateIcon(const IconDrawContext &context) {
            const ImVec2 center = context.Center();
            context.drawList.AddLine({center.x, context.position.y + 2.0F}, {center.x, context.position.y + context.size.y - 2.0F},
                                     context.color, 1.5F);
            context.drawList.AddLine({context.position.x + 2.0F, center.y}, {context.position.x + context.size.x - 2.0F, center.y},
                                     context.color, 1.5F);
        }

        void DrawRenameIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            context.drawList.AddLine({x + 3.0F, y + h - 3.0F}, {x + w - 3.0F, y + 3.0F}, context.color, 2.0F);
            context.drawList.AddTriangleFilled({x + 1.0F, y + h - 1.0F}, {x + 5.0F, y + h - 3.0F}, {x + 3.0F, y + h - 5.0F}, context.color);
        }

        void DrawDuplicateIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            context.drawList.AddRect({x + 1.0F, y + 1.0F}, {x + w - 5.0F, y + h - 5.0F}, context.color, 1.0F, 0, 1.2F);
            context.drawList.AddRect({x + 5.0F, y + 5.0F}, {x + w - 1.0F, y + h - 1.0F}, context.color, 1.0F, 0, 1.2F);
        }

        void DrawDeleteIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            context.drawList.AddRect({x + 4.0F, y + 5.0F}, {x + w - 4.0F, y + h - 1.0F}, context.color, 1.0F, 0, 1.3F);
            context.drawList.AddLine({x + 2.0F, y + 4.0F}, {x + w - 2.0F, y + 4.0F}, context.color, 1.3F);
            context.drawList.AddLine({x + 6.0F, y + 1.0F}, {x + w - 6.0F, y + 1.0F}, context.color, 1.3F);
        }

        void DrawVisibilityIcon(const IconDrawContext &context, const bool crossedOut) {
            const ImVec2 center = context.Center();
            const float glyphSize = std::min(context.size.x, context.size.y);
            const float glyphLeft = center.x - glyphSize * 0.44F;
            const float glyphRight = center.x + glyphSize * 0.44F;
            const float glyphTop = center.y - glyphSize * 0.40F;
            const float glyphBottom = center.y + glyphSize * 0.40F;
            const float stroke = std::max(1.0F, glyphSize * 0.085F);
            context.drawList.AddBezierCubic({glyphLeft, center.y}, {center.x - glyphSize * 0.24F, glyphTop},
                                            {center.x + glyphSize * 0.24F, glyphTop}, {glyphRight, center.y}, context.color, stroke);
            context.drawList.AddBezierCubic({glyphRight, center.y}, {center.x + glyphSize * 0.24F, glyphBottom},
                                            {center.x - glyphSize * 0.24F, glyphBottom}, {glyphLeft, center.y}, context.color, stroke);
            context.drawList.AddCircleFilled(center, glyphSize * 0.12F, context.color, 12);
            if (crossedOut)
                context.drawList.AddLine({center.x - glyphSize * 0.34F, center.y - glyphSize * 0.34F},
                                         {center.x + glyphSize * 0.34F, center.y + glyphSize * 0.34F}, context.color,
                                         std::max(1.25F, glyphSize * 0.11F));
        }

        void DrawVisibilityOnIcon(const IconDrawContext &context) {
            DrawVisibilityIcon(context, false);
        }

        void DrawVisibilityOffIcon(const IconDrawContext &context) {
            DrawVisibilityIcon(context, true);
        }

        void DrawLockIcon(const IconDrawContext &context) {
            const ImVec2 center = context.Center();
            const float glyphSize = std::min(context.size.x, context.size.y);
            const float stroke = std::max(1.0F, glyphSize * 0.085F);
            const float halfWidth = glyphSize * 0.35F;
            const float bodyTop = center.y - glyphSize * 0.02F;
            const float bodyBottom = center.y + glyphSize * 0.38F;
            context.drawList.AddRect({center.x - halfWidth, bodyTop}, {center.x + halfWidth, bodyBottom}, context.color, glyphSize * 0.06F,
                                     0, stroke);
            context.drawList.PathClear();
            context.drawList.PathLineTo({center.x - glyphSize * 0.25F, bodyTop});
            context.drawList.PathBezierCubicCurveTo({center.x - glyphSize * 0.25F, center.y - glyphSize * 0.43F},
                                                    {center.x + glyphSize * 0.25F, center.y - glyphSize * 0.43F},
                                                    {center.x + glyphSize * 0.25F, bodyTop}, 10);
            context.drawList.PathStroke(context.color, 0, stroke);
        }

        void DrawHierarchyIcon(const IconDrawContext &context, const bool mesh) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            const ImVec2 center = context.Center();
            const ImVec2 top{center.x, y + 1.0F};
            const ImVec2 left{x + 2.0F, y + h * 0.32F};
            const ImVec2 right{x + w - 2.0F, y + h * 0.32F};
            const ImVec2 leftBottom{x + 2.0F, y + h * 0.72F};
            const ImVec2 bottom{center.x, y + h - 1.0F};
            const ImVec2 rightBottom{x + w - 2.0F, y + h * 0.72F};
            const std::array outline{top, right, rightBottom, bottom, leftBottom, left};
            context.drawList.AddPolyline(outline.data(), outline.size(), context.color, ImDrawFlags_Closed, 1.35F);
            context.drawList.AddLine(left, center, context.color, 1.15F);
            context.drawList.AddLine(right, center, context.color, 1.15F);
            context.drawList.AddLine(center, bottom, context.color, 1.15F);
            if (mesh) {
                context.drawList.AddLine(top, center, context.color, 1.15F);
                context.drawList.AddCircleFilled(center, 1.15F, context.color, 8);
            }
        }

        void DrawHierarchyGenericIcon(const IconDrawContext &context) {
            DrawHierarchyIcon(context, false);
        }

        void DrawHierarchyMeshIcon(const IconDrawContext &context) {
            DrawHierarchyIcon(context, true);
        }

        void DrawCameraIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            context.drawList.AddRect({x + 1.0F, y + 4.0F}, {x + w * 0.68F, y + h - 3.0F}, context.color, 2.0F, 0, 1.4F);
            context.drawList.AddTriangle({x + w * 0.68F, y + 6.0F}, {x + w - 1.0F, y + 3.0F}, {x + w - 1.0F, y + h - 2.0F}, context.color,
                                         1.4F);
        }

        void DrawAudioSourceIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            const ImVec2 center = context.Center();
            context.drawList.AddTriangleFilled({x + 1.0F, center.y}, {x + 6.0F, y + 4.0F}, {x + 6.0F, y + h - 4.0F}, context.color);
            context.drawList.AddCircle(center, w * 0.30F, context.color, 16, 1.3F);
            context.drawList.AddCircle(center, w * 0.48F, context.color, 16, 1.3F);
        }

        void DrawSpotLightIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            const ImVec2 center = context.Center();
            context.drawList.AddCircleFilled({x + w * 0.30F, center.y}, w * 0.16F, context.color, 14);
            context.drawList.AddQuad({x + w * 0.36F, y + h * 0.34F}, {x + w - 1.0F, y + 2.0F}, {x + w - 1.0F, y + h - 2.0F},
                                     {x + w * 0.36F, y + h * 0.66F}, context.color, 1.4F);
        }

        void DrawDirectionalLightIcon(const IconDrawContext &context) {
            const ImVec2 center = context.Center();
            context.drawList.AddCircle(center, context.size.x * 0.22F, context.color, 16, 1.4F);
            for (int ray = 0; ray < 8; ++ray) {
                const float angle = static_cast<float>(ray) * std::numbers::pi_v<float> * 0.25F;
                const ImVec2 direction{std::cos(angle), std::sin(angle)};
                context.drawList.AddLine({center.x + direction.x * context.size.x * 0.32F, center.y + direction.y * context.size.y * 0.32F},
                                         {center.x + direction.x * context.size.x * 0.48F, center.y + direction.y * context.size.y * 0.48F},
                                         context.color, 1.3F);
            }
        }

        void DrawPointLightIcon(const IconDrawContext &context) {
            const ImVec2 center = context.Center();
            context.drawList.AddCircleFilled(center, context.size.x * 0.12F, context.color, 12);
            for (int ray = 0; ray < 4; ++ray) {
                const float angle = std::numbers::pi_v<float> * (0.25F + static_cast<float>(ray) * 0.5F);
                const ImVec2 direction{std::cos(angle), std::sin(angle)};
                context.drawList.AddLine({center.x + direction.x * context.size.x * 0.26F, center.y + direction.y * context.size.y * 0.26F},
                                         {center.x + direction.x * context.size.x * 0.45F, center.y + direction.y * context.size.y * 0.45F},
                                         context.color, 1.3F);
            }
        }

        void DrawLightIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            const ImVec2 center = context.Center();
            context.drawList.AddCircle(center, w * 0.27F, context.color, 16, 1.4F);
            context.drawList.AddLine({center.x, y}, {center.x, y + 3.0F}, context.color, 1.2F);
            context.drawList.AddLine({center.x, y + h - 3.0F}, {center.x, y + h}, context.color, 1.2F);
            context.drawList.AddLine({x, center.y}, {x + 3.0F, center.y}, context.color, 1.2F);
            context.drawList.AddLine({x + w - 3.0F, center.y}, {x + w, center.y}, context.color, 1.2F);
        }

        void DrawSphereIcon(const IconDrawContext &context) {
            const ImVec2 center = context.Center();
            context.drawList.AddCircle(center, context.size.x * 0.43F, context.color, 18, 1.4F);
            context.drawList.AddEllipse(center, {context.size.x * 0.18F, context.size.y * 0.43F}, context.color, 0.0F, 18, 1.0F);
        }

        void DrawCapsuleIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            context.drawList.AddRect({x + w * 0.25F, y + 1.0F}, {x + w * 0.75F, y + h - 1.0F}, context.color, w * 0.25F, 0, 1.4F);
        }

        void DrawCylinderIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            const ImVec2 center = context.Center();
            context.drawList.AddEllipse({center.x, y + 3.5F}, {w * 0.38F, 2.5F}, context.color, 0.0F, 16, 1.2F);
            context.drawList.AddEllipse({center.x, y + h - 3.5F}, {w * 0.38F, 2.5F}, context.color, 0.0F, 16, 1.2F);
            context.drawList.AddLine({x + w * 0.12F, y + 3.5F}, {x + w * 0.12F, y + h - 3.5F}, context.color, 1.2F);
            context.drawList.AddLine({x + w * 0.88F, y + 3.5F}, {x + w * 0.88F, y + h - 3.5F}, context.color, 1.2F);
        }

        void DrawConeIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            const ImVec2 center = context.Center();
            context.drawList.AddTriangle({center.x, y + 1.0F}, {x + 2.0F, y + h - 3.0F}, {x + w - 2.0F, y + h - 3.0F}, context.color, 1.4F);
            context.drawList.AddEllipse({center.x, y + h - 3.0F}, {w * 0.38F, 2.0F}, context.color, 0.0F, 16, 1.0F);
        }

        void DrawPlaneIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            const ImVec2 center = context.Center();
            context.drawList.AddQuad({center.x, y + 2.0F}, {x + w - 1.0F, center.y}, {center.x, y + h - 2.0F}, {x + 1.0F, center.y},
                                     context.color, 1.4F);
        }

        void DrawQuadIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            context.drawList.AddRect({x + 2.0F, y + 2.0F}, {x + w - 2.0F, y + h - 2.0F}, context.color, 1.0F, 0, 1.4F);
        }

        constexpr std::array kIconDescriptors{
            IconDescriptor{"", DrawNothing},
            IconDescriptor{"generic", DrawGenericIcon},
            IconDescriptor{"action.create", DrawCreateIcon},
            IconDescriptor{"action.rename", DrawRenameIcon},
            IconDescriptor{"action.duplicate", DrawDuplicateIcon},
            IconDescriptor{"action.delete", DrawDeleteIcon},
            IconDescriptor{"action.visibility", DrawVisibilityOnIcon},
            IconDescriptor{"action.visibility_off", DrawVisibilityOffIcon},
            IconDescriptor{"action.lock", DrawLockIcon},
            IconDescriptor{"hierarchy.generic", DrawHierarchyGenericIcon},
            IconDescriptor{"hierarchy.mesh", DrawHierarchyMeshIcon},
            IconDescriptor{"primitive.camera", DrawCameraIcon},
            IconDescriptor{"primitive.audio_source", DrawAudioSourceIcon},
            IconDescriptor{"primitive.light", DrawLightIcon},
            IconDescriptor{"primitive.light_spot", DrawSpotLightIcon},
            IconDescriptor{"primitive.light_directional", DrawDirectionalLightIcon},
            IconDescriptor{"primitive.light_point", DrawPointLightIcon},
            IconDescriptor{"primitive.sphere", DrawSphereIcon},
            IconDescriptor{"primitive.capsule", DrawCapsuleIcon},
            IconDescriptor{"primitive.cylinder", DrawCylinderIcon},
            IconDescriptor{"primitive.cone", DrawConeIcon},
            IconDescriptor{"primitive.plane", DrawPlaneIcon},
            IconDescriptor{"primitive.quad", DrawQuadIcon},
            IconDescriptor{"primitive.trigger_volume", DrawQuadIcon},
        };

        struct IconTokenAlias {
            std::string_view token;
            UiIcon icon;
        };

        constexpr std::array kIconTokenAliases{
            IconTokenAlias{"primitive.box", UiIcon::Generic},
            IconTokenAlias{"primitive.empty", UiIcon::Generic},
            IconTokenAlias{"primitive.collider.box", UiIcon::Generic},
            IconTokenAlias{"primitive.collider.sphere", UiIcon::Sphere},
            IconTokenAlias{"primitive.collider.capsule", UiIcon::Capsule},
            IconTokenAlias{"primitive.collider.plane", UiIcon::Plane},
            IconTokenAlias{"primitive.light.spot", UiIcon::SpotLight},
            IconTokenAlias{"primitive.light.directional", UiIcon::DirectionalLight},
            IconTokenAlias{"primitive.light.point", UiIcon::PointLight},
            IconTokenAlias{"create.group.objects_3d", UiIcon::Generic},
            IconTokenAlias{"create.group.lights", UiIcon::Light},
        };

        static_assert(kIconDescriptors.size() == static_cast<std::size_t>(UiIcon::Count));

        [[nodiscard]] std::optional<std::size_t> IconIndex(const UiIcon icon) noexcept {
            const auto index = static_cast<std::size_t>(icon);
            return index < kIconDescriptors.size() ? std::optional{index} : std::nullopt;
        }
    }  // namespace

    /** @copydoc UiIconRegistry::Resolve */
    std::optional<UiIcon> UiIconRegistry::Resolve(const std::string_view token) noexcept {
        const auto descriptor = std::ranges::find_if(kIconDescriptors, [token](const IconDescriptor &candidate) {
            return !candidate.token.empty() && candidate.token == token;
        });
        if (descriptor != kIconDescriptors.end())
            return static_cast<UiIcon>(std::distance(kIconDescriptors.begin(), descriptor));

        const auto alias = std::ranges::find_if(kIconTokenAliases, [token](const IconTokenAlias &candidate) {
            return candidate.token == token;
        });
        return alias != kIconTokenAliases.end() ? std::optional{alias->icon} : std::nullopt;
    }

    /** @copydoc UiIconRegistry::Token */
    std::string_view UiIconRegistry::Token(const UiIcon icon) noexcept {
        const std::optional<std::size_t> index = IconIndex(icon);
        return index.has_value() ? kIconDescriptors[*index].token : std::string_view{};
    }

    /** @copydoc DrawEditorIcon */
    void DrawEditorIcon(ImDrawList *drawList, const UiIcon icon, const ImVec2 position, const ImVec2 size, const ImU32 color) {
        if (drawList == nullptr || icon == UiIcon::None)
            return;
        const std::optional<std::size_t> index = IconIndex(icon);
        if (index.has_value())
            kIconDescriptors[*index].renderer(IconDrawContext{*drawList, position, size, color});
    }
}  // namespace Horo::Editor::Ui
