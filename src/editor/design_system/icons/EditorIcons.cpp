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

        void DrawNothing(const IconDrawContext &) {
            // Explicit no-op placeholder renderer for empty or unmapped icon tokens.
        }

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

        void DrawResetIcon(const IconDrawContext &context) {
            const ImVec2 center = context.Center();
            const float radius = std::min(context.size.x, context.size.y) * 0.34F;
            context.drawList.PathClear();
            context.drawList.PathArcTo(center, radius, -0.35F * std::numbers::pi_v<float>, 1.55F * std::numbers::pi_v<float>, 18);
            context.drawList.PathStroke(context.color, 0, 1.4F);
            context.drawList.AddTriangleFilled({center.x - radius - 1.0F, center.y - 1.0F}, {center.x - radius + 4.0F, center.y - 4.0F},
                                               {center.x - radius + 4.0F, center.y + 2.0F}, context.color);
        }

        void DrawCheckIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            context.drawList.AddRect({x + 1.5F, y + 1.5F}, {x + w - 1.5F, y + h - 1.5F}, context.color, 1.5F, 0, 1.3F);
            context.drawList.AddLine({x + 4.0F, y + h * 0.52F}, {x + w * 0.43F, y + h - 4.0F}, context.color, 1.4F);
            context.drawList.AddLine({x + w * 0.43F, y + h - 4.0F}, {x + w - 3.5F, y + 4.0F}, context.color, 1.4F);
        }

        void DrawUncheckedCheckboxIcon(const IconDrawContext &context) {
            const auto [x, y] = context.position;
            const auto [w, h] = context.size;
            context.drawList.AddRect({x + 1.5F, y + 1.5F}, {x + w - 1.5F, y + h - 1.5F}, context.color, 1.5F, 0, 1.3F);
        }

        void DrawSettingsIcon(const IconDrawContext &context) {
            const ImVec2 center = context.Center();
            const float radius = std::min(context.size.x, context.size.y) * 0.25F;
            context.drawList.AddCircle(center, radius, context.color, 16, 1.3F);
            context.drawList.AddCircleFilled(center, std::max(1.0F, radius * 0.28F), context.color, 10);
            for (int tooth = 0; tooth < 8; ++tooth) {
                const float angle = static_cast<float>(tooth) * std::numbers::pi_v<float> * 0.25F;
                const ImVec2 direction{std::cos(angle), std::sin(angle)};
                context.drawList.AddLine({center.x + direction.x * radius * 1.25F, center.y + direction.y * radius * 1.25F},
                                         {center.x + direction.x * radius * 1.75F, center.y + direction.y * radius * 1.75F}, context.color,
                                         1.4F);
            }
        }

        void DrawMoreVerticalIcon(const IconDrawContext &context) {
            const ImVec2 center = context.Center();
            const float radius = std::max(1.0F, std::min(context.size.x, context.size.y) * 0.08F);
            for (const float offset : {-0.28F, 0.0F, 0.28F})
                context.drawList.AddCircleFilled({center.x, center.y + context.size.y * offset}, radius, context.color, 10);
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
            IconDescriptor{"status.info", DrawNothing},
            IconDescriptor{"status.warning", DrawNothing},
            IconDescriptor{"status.error", DrawNothing},
            IconDescriptor{"action.create", DrawCreateIcon},
            IconDescriptor{"action.rename", DrawRenameIcon},
            IconDescriptor{"action.duplicate", DrawDuplicateIcon},
            IconDescriptor{"action.delete", DrawDeleteIcon},
            IconDescriptor{"action.reset", DrawResetIcon},
            IconDescriptor{"action.check", DrawCheckIcon},
            IconDescriptor{"action.checkbox_unchecked", DrawUncheckedCheckboxIcon},
            IconDescriptor{"action.settings", DrawSettingsIcon},
            IconDescriptor{"action.more_vertical", DrawMoreVerticalIcon},
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
            IconDescriptor{"navigation.arrow_back", DrawNothing},
            IconDescriptor{"navigation.arrow_forward", DrawNothing},
            IconDescriptor{"navigation.arrow_upward", DrawNothing},
            IconDescriptor{"action.search", DrawNothing},
            IconDescriptor{"view.grid", DrawNothing},
            IconDescriptor{"view.list", DrawNothing},
            IconDescriptor{"action.create_new_folder", DrawCreateIcon},
            IconDescriptor{"location.favorite", DrawNothing},
            IconDescriptor{"location.history", DrawNothing},
            IconDescriptor{"location.storage", DrawNothing},
            IconDescriptor{"location.package", DrawNothing},
            IconDescriptor{"location.account_tree", DrawNothing},
            IconDescriptor{"location.tag", DrawNothing},
            IconDescriptor{"asset.folder", DrawNothing},
            IconDescriptor{"asset.image", DrawNothing},
            IconDescriptor{"asset.audio_file", DrawAudioSourceIcon},
            IconDescriptor{"asset.description", DrawGenericIcon},
            IconDescriptor{"action.pause", DrawNothing},
            IconDescriptor{"action.download", DrawNothing},
            IconDescriptor{"action.stop", DrawNothing},
            IconDescriptor{"action.play", DrawNothing},
            IconDescriptor{"action.record", DrawNothing},
            IconDescriptor{"action.volume_off", DrawNothing},
            IconDescriptor{"action.clear_all", DrawNothing},
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

        struct MaterialSymbol {
            UiIcon icon;
            ImWchar glyph;
        };

        constexpr std::array kMaterialSymbols{
            MaterialSymbol{UiIcon::Info, 0xE88E},
            MaterialSymbol{UiIcon::Warning, 0xE002},
            MaterialSymbol{UiIcon::Error, 0xE000},
            MaterialSymbol{UiIcon::Create, 0xE145},
            MaterialSymbol{UiIcon::Delete, 0xE872},
            MaterialSymbol{UiIcon::Reset, 0xF053},
            MaterialSymbol{UiIcon::Check, 0xE834},
            MaterialSymbol{UiIcon::CheckboxUnchecked, 0xE835},
            MaterialSymbol{UiIcon::Settings, 0xE8B8},
            MaterialSymbol{UiIcon::MoreVertical, 0xE5D4},
            MaterialSymbol{UiIcon::ArrowBack, 0xE5C4},
            MaterialSymbol{UiIcon::ArrowForward, 0xE5C8},
            MaterialSymbol{UiIcon::ArrowUpward, 0xE5D8},
            MaterialSymbol{UiIcon::Search, 0xE8B6},
            MaterialSymbol{UiIcon::GridView, 0xE9B0},
            MaterialSymbol{UiIcon::ViewList, 0xE8EF},
            MaterialSymbol{UiIcon::CreateNewFolder, 0xE2CC},
            MaterialSymbol{UiIcon::Favorite, 0xE838},
            MaterialSymbol{UiIcon::History, 0xE889},
            MaterialSymbol{UiIcon::Storage, 0xE1DB},
            MaterialSymbol{UiIcon::Package, 0xE1A1},
            MaterialSymbol{UiIcon::AccountTree, 0xE97A},
            MaterialSymbol{UiIcon::Tag, 0xE892},
            MaterialSymbol{UiIcon::Folder, 0xE2C7},
            MaterialSymbol{UiIcon::Image, 0xE3F4},
            MaterialSymbol{UiIcon::AudioFile, 0xEB82},
            MaterialSymbol{UiIcon::Description, 0xE873},
            MaterialSymbol{UiIcon::Pause, 0xE034},
            MaterialSymbol{UiIcon::Download, 0xE2C4},
            MaterialSymbol{UiIcon::Stop, 0xE047},
            MaterialSymbol{UiIcon::Play, 0xE037},
            MaterialSymbol{UiIcon::Record, 0xE061},
            MaterialSymbol{UiIcon::VolumeOff, 0xE04F},
            MaterialSymbol{UiIcon::ClearAll, 0xE0B8},
        };

        [[nodiscard]] constexpr ImWchar MaterialSymbolGlyph(const UiIcon icon) noexcept {
            const auto match = std::ranges::find(kMaterialSymbols, icon, &MaterialSymbol::icon);
            return match == kMaterialSymbols.end() ? 0 : match->glyph;
        }

        [[nodiscard]] std::array<char, 4> EncodeBasicMultilingualPlaneGlyph(const ImWchar codepoint) noexcept {
            if (codepoint < 0x80)
                return {static_cast<char>(codepoint), '\0', '\0', '\0'};
            if (codepoint < 0x800) {
                return {
                    static_cast<char>(0xC0 | (codepoint >> 6)),
                    static_cast<char>(0x80 | (codepoint & 0x3F)),
                    '\0',
                    '\0',
                };
            }
            return {
                static_cast<char>(0xE0 | (codepoint >> 12)),
                static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)),
                static_cast<char>(0x80 | (codepoint & 0x3F)),
                '\0',
            };
        }
    }  // namespace

    /** @copydoc UiIconRegistry::Resolve */
    std::optional<UiIcon> UiIconRegistry::Resolve(const std::string_view token) noexcept {
        if (const auto descriptor = std::ranges::find_if(kIconDescriptors,
                                                         [token](const IconDescriptor &candidate) {
            return !candidate.token.empty() && candidate.token == token;
        });
            descriptor != kIconDescriptors.end())
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

    /** @copydoc UiIconRegistry::MaterialSymbolGlyphRanges */
    std::span<const ImWchar> UiIconRegistry::MaterialSymbolGlyphRanges() noexcept {
        static constexpr std::array<ImWchar, 45> ranges{
            0xE000, 0xE003,  // status icons
            0xE034, 0xE061,  // media controls
            0xE0B8, 0xE0B8,  // clear_all
            0xE145, 0xE145,  // add
            0xE1A1, 0xE1A1,  // inventory_2
            0xE1DB, 0xE1DB,  // storage
            0xE2C7, 0xE2C7,  // folder
            0xE2CC, 0xE2CC,  // create_new_folder
            0xE3F4, 0xE3F4,  // image
            0xE5C4, 0xE5C4,  // arrow_back
            0xE5C8, 0xE5C8,  // arrow_forward
            0xE5D4, 0xE5D8,  // more_vert, arrow_upward
            0xE834, 0xE838,  // check boxes, star
            0xE86C, 0xE873,  // check_circle, description
            0xE889, 0xE892,  // history, label
            0xE8B6, 0xE8B8,  // search, settings
            0xE8EF, 0xE8EF,  // view_list
            0xE97A, 0xE97A,  // account_tree
            0xE9B0, 0xE9B0,  // grid_view
            0xEB82, 0xEB82,  // audio_file
            0xEF4A, 0xEF4B,  // circle
            0xF053, 0xF054,  // restart_alt
            0,
        };
        return ranges;
    }

    /** @copydoc DrawEditorIcon */
    void DrawEditorIcon(ImDrawList *drawList, const UiIcon icon, const ImVec2 position, const ImVec2 size, const ImU32 color,
                        ImFont *const iconFont) {
        if (drawList == nullptr || icon == UiIcon::None)
            return;
        if (const ImWchar glyph = MaterialSymbolGlyph(icon); glyph != 0 && iconFont != nullptr && iconFont->FindGlyphNoFallback(glyph)) {
            const std::array utf8 = EncodeBasicMultilingualPlaneGlyph(glyph);
            const float glyphSize = std::min(size.x, size.y);
            const ImVec2 textSize = iconFont->CalcTextSizeA(glyphSize, FLT_MAX, 0.0F, utf8.data());
            drawList->AddText(iconFont, glyphSize, {position.x + (size.x - textSize.x) * 0.5F, position.y + (size.y - textSize.y) * 0.5F},
                              color, utf8.data());
            return;
        }
        const std::optional<std::size_t> index = IconIndex(icon);
        if (index.has_value())
            kIconDescriptors[*index].renderer(IconDrawContext{*drawList, position, size, color});
    }
}  // namespace Horo::Editor::Ui
