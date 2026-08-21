#include "editor/screens/workspace/panels/hierarchy/HierarchyPanel.h"

#include "Horo/Editor/EditorIcons.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/screens/workspace/AssetSceneDrop.h"
#include "editor/screens/workspace/panels/hierarchy/HierarchyRowLayout.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <string_view>

namespace Horo::Editor {
    namespace {
        constexpr float kTabHeight = 28.0F;
        constexpr float kOuterPadding = 8.0F;
        constexpr float kRowActionsWidth = 48.0F;

        [[nodiscard]] ImVec4 BlendColor(const ImVec4 &first, const ImVec4 &second, const float amount) noexcept {
            const float clamped = std::clamp(amount, 0.0F, 1.0F);
            return {
                first.x + (second.x - first.x) * clamped,
                first.y + (second.y - first.y) * clamped,
                first.z + (second.z - first.z) * clamped,
                first.w + (second.w - first.w) * clamped,
            };
        }

        struct HierarchyIconPresentation {
            Ui::UiIcon icon{Ui::UiIcon::HierarchyGeneric};
            const char *tooltipKey{nullptr};
            ImVec4 color{};
        };

        [[nodiscard]] HierarchyIconPresentation GetIconPresentation(const HierarchyNodeType type) {
            switch (type) {
                case HierarchyNodeType::Mesh:
                    return {Ui::UiIcon::HierarchyMesh, "workspace.hierarchy.type.mesh", Theme::Ok()};
                case HierarchyNodeType::Empty:
                case HierarchyNodeType::Collection:
                    return {Ui::UiIcon::HierarchyGeneric, "workspace.hierarchy.type.empty", Theme::Muted()};
                case HierarchyNodeType::Light:
                    return {Ui::UiIcon::Light, "workspace.hierarchy.type.light", Theme::Warn()};
                case HierarchyNodeType::PointLight:
                    return {Ui::UiIcon::PointLight, "workspace.hierarchy.type.light_point", Theme::Warn()};
                case HierarchyNodeType::DirectionalLight:
                    return {Ui::UiIcon::DirectionalLight, "workspace.hierarchy.type.light_directional", Theme::Warn()};
                case HierarchyNodeType::SpotLight:
                    return {Ui::UiIcon::SpotLight, "workspace.hierarchy.type.light_spot", Theme::Warn()};
                case HierarchyNodeType::Camera:
                    return {Ui::UiIcon::Camera, "workspace.hierarchy.type.camera", Theme::Accent()};
                case HierarchyNodeType::TriggerVolume:
                    return {Ui::UiIcon::TriggerVolume, "workspace.hierarchy.type.volume", Theme::Warn()};
                case HierarchyNodeType::AudioSource:
                    return {Ui::UiIcon::AudioSource, "workspace.hierarchy.type.audio", BlendColor(Theme::Accent(), Theme::Err(), 0.45F)};
            }
            return {Ui::UiIcon::HierarchyGeneric, "workspace.hierarchy.type.empty", Theme::Muted()};
        }

        [[nodiscard]] ImFont *ResolveFont(ImFont *preferred) {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        [[nodiscard]] bool DrawHierarchyLabel(ImDrawList &drawList, ImFont &font, const float fontSize, const ImVec2 minimum,
                                              const ImVec2 maximum, const float centerY, const ImU32 color, const std::string_view text) {
            if (text.empty() || maximum.x <= minimum.x)
                return !text.empty();
            const ImVec2 fullSize = font.CalcTextSizeA(fontSize, 100000.0F, 0.0F, text.data(), text.data() + text.size());
            const float availableWidth = maximum.x - minimum.x;
            const ImVec2 textPosition{minimum.x, centerY - fullSize.y * 0.5F};
            drawList.PushClipRect(minimum, maximum, true);
            if (fullSize.x <= availableWidth) {
                drawList.AddText(&font, fontSize, textPosition, color, text.data(), text.data() + text.size());
                drawList.PopClipRect();
                return false;
            }

            constexpr std::string_view ellipsis{"\xE2\x80\xA6"};
            const float ellipsisWidth = font.CalcTextSizeA(fontSize, 100.0F, 0.0F, ellipsis.data(), ellipsis.data() + ellipsis.size()).x;
            const char *visibleEnd = text.data();
            if (availableWidth > ellipsisWidth) {
                static_cast<void>(font.CalcTextSizeA(fontSize, availableWidth - ellipsisWidth, 0.0F, text.data(), text.data() + text.size(),
                                                     &visibleEnd));
                drawList.AddText(&font, fontSize, textPosition, color, text.data(), visibleEnd);
            }
            const float visibleWidth = font.CalcTextSizeA(fontSize, 100000.0F, 0.0F, text.data(), visibleEnd).x;
            drawList.AddText(&font, fontSize, {minimum.x + visibleWidth, textPosition.y}, color, ellipsis.data(),
                             ellipsis.data() + ellipsis.size());
            drawList.PopClipRect();
            return true;
        }

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

        [[nodiscard]] bool AcceptAssetDrop(const std::optional<SceneObjectId> parent, const AssetSceneDropTarget target,
                                           const ImVec2 minimum, const ImVec2 maximum, const DocumentRevision revision,
                                           EditorWorkspaceViewCommandData &command, ImDrawList &drawList) {
            if (!ImGui::BeginDragDropTarget())
                return false;
            bool delivered = false;
            const ImGuiPayload *accepted =
                ImGui::AcceptDragDropPayload(AssetSceneDragPayloadType,
                                             ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (const std::optional<AssetSceneDragPayload> payload = ReadAssetPayload(accepted); payload.has_value()) {
                const AssetSceneDropPolicyResult policy = EvaluateAssetSceneDrop(*payload);
                drawList.AddRect(minimum, maximum, Theme::U32(policy.canInstantiate ? Theme::Accent() : Theme::Err()),
                                 Theme::Layout::Radius, 0, 2.0F);
                if (accepted->IsDelivery()) {
                    delivered = true;
                    command.command = EditorWorkspaceViewCommand::InstantiateAsset;
                    command.assetSceneDrop = AssetSceneDropRequest{
                        .assetId = payload->assetId.data(),
                        .assetType = payload->assetType.data(),
                        .parent = parent,
                        .target = target,
                        .documentRevision = revision,
                    };
                }
            }
            ImGui::EndDragDropTarget();
            return delivered;
        }

        void DrawCreateMenuItems(const std::vector<EditorMenuItem> &items, const std::optional<SceneObjectId> parent,
                                 EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
            for (const EditorMenuItem &item : items) {
                const std::string &label = context.localization.Get("editor", item.labelKey);
                const std::string stableLabel = label + "###hierarchy_create_" + std::string(item.labelKey);
                if (item.kind == EditorMenuItemKind::Submenu) {
                    if (Ui::BeginContextSubmenu(stableLabel.c_str(), context.theme.fonts)) {
                        DrawCreateMenuItems(item.children, parent, command, context);
                        Ui::EndContextSubmenu();
                    }
                    continue;
                }
                if (item.kind == EditorMenuItemKind::Command && item.action == EditorMenuAction::CreatePrimitive &&
                    item.primitive.has_value() && Ui::ContextMenuItem(stableLabel.c_str(), nullptr, context.theme.fonts)) {
                    command = HierarchyEditSession::CreateCommand(*item.primitive, parent);
                }
            }
        }
    }  // namespace

    void HierarchyPanel::OnAttach(PanelContext &context) {
        inputRouter_ = context.inputRouter;
        workspaceInputContext_ = context.workspaceInputContext;
    }

    void HierarchyPanel::OnDetach() {
        focusedWidgetContext_.Reset();
        inputRouter_ = nullptr;
        workspaceInputContext_ = nullptr;
    }

    void HierarchyPanel::BeginRename(const HierarchyNodeId id) {
        const HierarchyNode *node = editSession_.Find(id);
        if (node == nullptr) {
            return;
        }
        const auto result = std::format_to_n(renameBuffer_.data(), renameBuffer_.size() - 1U, "{}", node->name);
        *result.out = '\0';
        renamingId_ = id;
        requestRenameFocus_ = true;
    }

    void HierarchyPanel::DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, const ImU32 color) {
        const float ox = pos.x + (size.x - 14.0f) * 0.5f;
        const float oy = pos.y + (size.y - 14.0f) * 0.5f;

        // Simple hierarchy icon (nodes and branches)
        dl->AddLine(ImVec2(ox + 2, oy + 2), ImVec2(ox + 12, oy + 2), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 2), ImVec2(ox + 4, oy + 7), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 7), ImVec2(ox + 12, oy + 7), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 7), ImVec2(ox + 4, oy + 12), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 12), ImVec2(ox + 12, oy + 12), color, 1.5f);
    }

    void HierarchyPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                                   EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx) {
        static_cast<void>(pos);
        editSession_.Synchronize(vm);
        const float uiScale = Theme::GetActiveTokens().sizes.uiScale;
        const std::array tabNames{ctx.localization.Get("editor", "workspace.panel.hierarchy").c_str()};
        Ui::DrawDockTabs(tabNames, 0, ctx.theme.fonts);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::BeginChild("##HierarchyContent", ImVec2(size.x, size.y - kTabHeight), false, ImGuiWindowFlags_NoSavedSettings);

        ImGui::SetCursorPos(ImVec2(kOuterPadding * uiScale, kOuterPadding * uiScale));
        ImGui::SetNextItemWidth(std::max(1.0F, size.x - kOuterPadding * 2.0F * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0F * uiScale, 5.0F * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
        bool searchActive = false;
        {
            const Theme::ScopedFont searchFont(ctx.theme.fonts.sansCompact);
            ImGui::InputTextWithHint("##HierarchySearch", ctx.localization.Get("editor", "workspace.hierarchy.search").c_str(),
                                     searchBuffer_.data(), searchBuffer_.size());
            searchActive = ImGui::IsItemActive();
        }
        const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);

        const bool needsFocusedContext = searchActive || renamingId_.has_value();
        if (needsFocusedContext && !focusedWidgetContext_.IsActive() && inputRouter_ != nullptr)
            focusedWidgetContext_ =
                inputRouter_->PushContext(Input::InputContextId{"editor.hierarchy.text"}, Input::InputContextKind::FocusedGuiWidget);
        else if (!needsFocusedContext)
            focusedWidgetContext_.Reset();
        const bool workspaceEligible =
            inputRouter_ != nullptr && workspaceInputContext_ != nullptr && inputRouter_->IsContextActive(*workspaceInputContext_);

        const std::vector<HierarchyVisibleRow> &visibleRows = editSession_.VisibleRows(searchBuffer_.data());
        if (workspaceEligible && panelFocused && !searchActive && !renamingId_.has_value() && editSession_.SelectedId().has_value() &&
            inputRouter_->ConsumeKey(*workspaceInputContext_, Input::Key::F2)) {
            const HierarchyNode *selectedNode = editSession_.Find(*editSession_.SelectedId());
            if (selectedNode != nullptr && !selectedNode->effectivelyLocked)
                BeginRename(*editSession_.SelectedId());
        }

        bool pendingDelete = false;

        const float outerPadding = kOuterPadding * uiScale;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0F * uiScale);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
        const float listWidth = std::max(1.0F, size.x - outerPadding * 2.0F);
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImFont *nameFont = ResolveFont(ctx.theme.fonts.sans);
        const float nameFontSize = nameFont->FontSize * uiScale;

        for (const HierarchyVisibleRow &row : visibleRows) {
            const HierarchyNode &node = *row.node;
            ImGui::PushID(&node.id);
            ImGui::SetCursorPosX(outerPadding);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const HierarchyRowLayout layout = CalculateHierarchyRowLayout(listWidth, row.depth, uiScale, kRowActionsWidth * uiScale);
            const ImVec2 rowMax{rowMin.x + listWidth, rowMin.y + layout.height};
            const ImVec2 chevronMin{rowMin.x + layout.chevron.minimum, rowMin.y};
            const ImVec2 chevronMax{rowMin.x + layout.chevron.maximum, rowMax.y};
            const ImVec2 typeIconMin{rowMin.x + layout.typeIcon.minimum, rowMin.y};
            const ImVec2 typeIconMax{rowMin.x + layout.typeIcon.maximum, rowMax.y};
            const ImVec2 labelMin{rowMin.x + layout.label.minimum, rowMin.y};
            const ImVec2 labelMax{rowMin.x + layout.label.maximum, rowMax.y};
            const ImVec2 actionsMin{rowMin.x + layout.actions.minimum, rowMin.y};
            const ImVec2 actionsMax{rowMin.x + layout.actions.maximum, rowMax.y};
            const ImVec2 visibilityMin{rowMin.x + layout.visibilityAction.minimum, rowMin.y};
            const ImVec2 visibilityMax{rowMin.x + layout.visibilityAction.maximum, rowMax.y};
            const ImVec2 lockMin{rowMin.x + layout.lockAction.minimum, rowMin.y};
            const ImVec2 lockMax{rowMin.x + layout.lockAction.maximum, rowMax.y};
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##hierarchy_object_row", ImVec2(listWidth, layout.height));
            const bool rowHovered = ImGui::IsItemHovered();
            const bool rowFocused = ImGui::IsItemFocused();
            const bool rowLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool rowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            const ImVec2 nextRowCursor = ImGui::GetCursorScreenPos();
            const bool selected = editSession_.IsSelected(node.id);
            const bool primarySelected = editSession_.SelectedId() == node.id;
            const bool pointerInActions = ImGui::IsMouseHoveringRect(actionsMin, actionsMax);
            const bool assetDropDelivered = AcceptAssetDrop(SceneObjectId{node.id}, AssetSceneDropTarget::HierarchyChild, rowMin, rowMax,
                                                            vm.documentRevision, cmd, *drawList);

            if (workspaceEligible && rowRightClicked && !pointerInActions && !selected) {
                editSession_.Select(node.id);
                cmd = editSession_.SelectCommand(node.id, HierarchySelectionGesture::Replace);
            }
            if (!pointerInActions && Ui::BeginContextMenu("##HierarchyContext")) {
                if (workspaceEligible && !node.effectivelyLocked &&
                    Ui::BeginContextSubmenu((ctx.localization.Get("editor", "workspace.create") + "###hierarchy_create_root").c_str(),
                                            ctx.theme.fonts)) {
                    DrawCreateMenuItems(GetPrimitiveCreateMenuItems(), SceneObjectId{node.id}, cmd, ctx);
                    Ui::EndContextSubmenu();
                }
                Ui::ContextMenuSeparator();
                if (workspaceEligible && !node.effectivelyLocked &&
                    Ui::ContextMenuItem(ctx.localization.Get("editor", "workspace.hierarchy.rename").c_str(), "F2", ctx.theme.fonts)) {
                    BeginRename(node.id);
                }
                if (workspaceEligible && !node.effectivelyLocked &&
                    Ui::ContextMenuItem(ctx.localization.Get("editor", "workspace.hierarchy.duplicate").c_str(), nullptr,
                                        ctx.theme.fonts)) {
                    cmd = HierarchyEditSession::DuplicateCommand(node.id);
                }
                Ui::ContextMenuSeparator();
                if (workspaceEligible && !node.effectivelyLocked &&
                    Ui::ContextMenuItem(ctx.localization.Get("editor", "workspace.hierarchy.delete").c_str(), "Delete", ctx.theme.fonts,
                                        Ui::ContextMenuItemTone::Danger)) {
                    pendingDelete = true;
                }
                Ui::EndContextMenu();
            }

            bool chevronHovered = false;
            bool chevronPressed = false;
            if (!node.children.empty() && searchBuffer_[0] == '\0') {
                ImGui::SetCursorScreenPos(chevronMin);
                ImGui::InvisibleButton("##hierarchy_chevron", ImVec2(layout.chevron.Width(), layout.height));
                chevronHovered = ImGui::IsItemHovered();
                chevronPressed = workspaceEligible && ImGui::IsItemClicked(ImGuiMouseButton_Left);
                ImGui::SetCursorScreenPos(nextRowCursor);
            }
            bool visibilityHovered = false;
            bool visibilityPressed = false;
            bool lockHovered = false;
            bool lockPressed = false;
            if (layout.visibilityAction.Width() > 0.0F) {
                ImGui::SetCursorScreenPos(visibilityMin);
                ImGui::InvisibleButton("##hierarchy_visibility", ImVec2(layout.visibilityAction.Width(), layout.height));
                visibilityHovered = ImGui::IsItemHovered();
                visibilityPressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                if (visibilityHovered) {
                    const char *tooltip = "workspace.hierarchy.show";
                    if (node.hiddenByParent && node.locallyVisible) {
                        tooltip = "workspace.hierarchy.hidden_by_parent";
                    } else if (node.locallyVisible) {
                        tooltip = "workspace.hierarchy.hide";
                    }
                    ImGui::SetTooltip("%s", ctx.localization.Get("editor", tooltip).c_str());
                }
                ImGui::SetCursorScreenPos(lockMin);
                ImGui::InvisibleButton("##hierarchy_lock", ImVec2(layout.lockAction.Width(), layout.height));
                lockHovered = ImGui::IsItemHovered();
                lockPressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                if (lockHovered) {
                    const char *tooltip = "workspace.hierarchy.lock";
                    if (node.lockedByParent && !node.locallyLocked) {
                        tooltip = "workspace.hierarchy.locked_by_parent";
                    } else if (node.locallyLocked) {
                        tooltip = "workspace.hierarchy.unlock";
                    }
                    ImGui::SetTooltip("%s", ctx.localization.Get("editor", tooltip).c_str());
                }
                ImGui::SetCursorScreenPos(nextRowCursor);
            }
            const bool hovered = rowHovered || chevronHovered || visibilityHovered || lockHovered;

            if (selected) {
                ImVec4 selectedBackground = Theme::Accent();
                selectedBackground.w = hovered ? 0.17F : 0.13F;
                drawList->AddRectFilled(rowMin, rowMax, Theme::U32(selectedBackground), 2.0F * uiScale);
                if (primarySelected) {
                    drawList->AddRectFilled(rowMin, ImVec2(rowMin.x + 2.0F * uiScale, rowMax.y), Theme::U32(Theme::Accent()),
                                            2.0F * uiScale);
                }
            } else if (hovered) {
                drawList->AddRectFilled(rowMin, rowMax, Theme::U32(Theme::Hover()), 2.0F * uiScale);
            }
            if (rowFocused)
                drawList->AddRect(rowMin, rowMax, Theme::U32(Theme::BorderStrong()), 2.0F * uiScale, 0, 1.0F * uiScale);

            const float centerY = rowMin.y + layout.height * 0.5F;
            const float chevronCenterX = (chevronMin.x + chevronMax.x) * 0.5F;
            if (row.depth > 0) {
                const float guideX = chevronCenterX - 12.0F * uiScale;
                drawList->AddLine(ImVec2(guideX, rowMin.y), ImVec2(guideX, centerY), Theme::U32(Theme::Border()), 1.0F);
                drawList->AddLine(ImVec2(guideX, centerY), ImVec2(chevronMin.x, centerY), Theme::U32(Theme::Border()), 1.0F);
            }
            if (!node.children.empty()) {
                const float chevronScale = uiScale;
                if (node.expanded || searchBuffer_[0] != '\0') {
                    drawList->AddTriangleFilled(ImVec2(chevronCenterX - 3.0F * chevronScale, centerY - 2.0F * chevronScale),
                                                ImVec2(chevronCenterX + 3.0F * chevronScale, centerY - 2.0F * chevronScale),
                                                ImVec2(chevronCenterX, centerY + 2.0F * chevronScale),
                                                Theme::U32(chevronHovered ? Theme::Text() : Theme::Dim()));
                } else {
                    drawList->AddTriangleFilled(ImVec2(chevronCenterX - 2.0F * chevronScale, centerY - 3.0F * chevronScale),
                                                ImVec2(chevronCenterX - 2.0F * chevronScale, centerY + 3.0F * chevronScale),
                                                ImVec2(chevronCenterX + 2.0F * chevronScale, centerY),
                                                Theme::U32(chevronHovered ? Theme::Text() : Theme::Dim()));
                }
            }

            const HierarchyIconPresentation icon = GetIconPresentation(node.type);
            const float iconSize = 16.0F * uiScale;
            const ImVec2 iconPosition{typeIconMin.x, centerY - iconSize * 0.5F};
            ImVec4 typeColor = icon.color;
            if (node.effectivelyLocked)
                typeColor.w *= 0.65F;
            Ui::DrawEditorIcon(drawList, icon.icon, iconPosition, {iconSize, iconSize}, Theme::U32(typeColor));
            if (icon.tooltipKey != nullptr && ImGui::IsMouseHoveringRect(typeIconMin, typeIconMax))
                ImGui::SetTooltip("%s", ctx.localization.Get("editor", icon.tooltipKey).c_str());

            if (layout.visibilityAction.Width() > 0.0F) {
                const float actionIconSize =
                    std::max(0.0F, std::min({15.0F * uiScale, layout.visibilityAction.Width() - 4.0F * uiScale,
                                             layout.lockAction.Width() - 4.0F * uiScale, layout.height - 4.0F * uiScale}));
                const auto drawAction = [&](const Ui::UiIcon actionIcon, const ImVec2 minimum, const ImVec2 maximum,
                                            const bool actionHovered, const bool active, const bool inherited) {
                    if (actionIconSize <= 0.0F)
                        return;
                    ImVec4 color = actionHovered || active ? Theme::Text() : Theme::Muted();
                    if (active && !actionHovered)
                        color.w *= 0.82F;
                    if (inherited)
                        color.w *= 0.55F;
                    const ImVec2 position{minimum.x + ((maximum.x - minimum.x) - actionIconSize) * 0.5F,
                                          minimum.y + ((maximum.y - minimum.y) - actionIconSize) * 0.5F};
                    Ui::DrawEditorIcon(drawList, actionIcon, position, {actionIconSize, actionIconSize}, Theme::U32(color));
                };
                drawAction(node.effectivelyVisible ? Ui::UiIcon::Visibility : Ui::UiIcon::VisibilityOff, visibilityMin, visibilityMax,
                           visibilityHovered, !node.effectivelyVisible, node.hiddenByParent && node.locallyVisible);
                drawAction(Ui::UiIcon::Lock, lockMin, lockMax, lockHovered, node.effectivelyLocked,
                           node.lockedByParent && !node.locallyLocked);
            }

            if (!assetDropDelivered && chevronPressed) {
                editSession_.ToggleExpanded(node.id);
            } else if (!assetDropDelivered && workspaceEligible && visibilityPressed) {
                if (!(node.hiddenByParent && node.locallyVisible))
                    cmd = HierarchyEditSession::ToggleVisibilityCommand(node);
            } else if (!assetDropDelivered && workspaceEligible && lockPressed) {
                if (!(node.lockedByParent && !node.locallyLocked))
                    cmd = HierarchyEditSession::ToggleLockCommand(node);
            } else if (!assetDropDelivered && workspaceEligible && rowLeftClicked && !pointerInActions) {
                editSession_.Select(node.id);
                const ImGuiIO &io = ImGui::GetIO();
                HierarchySelectionGesture gesture = HierarchySelectionGesture::Replace;
                if (io.KeyShift) {
                    gesture = HierarchySelectionGesture::Range;
                } else if (io.KeyCtrl || io.KeySuper) {
                    gesture = HierarchySelectionGesture::Toggle;
                }
                cmd = editSession_.SelectCommand(node.id, gesture);
            }

            if (renamingId_ == node.id) {
                ImGui::SetCursorScreenPos(ImVec2(labelMin.x, rowMin.y + 2.0F * uiScale));
                ImGui::SetNextItemWidth(std::max(1.0F, labelMax.x - labelMin.x));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0F * uiScale, 1.0F * uiScale));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
                ImGui::PushStyleColor(ImGuiCol_Border, Theme::Accent());
                if (requestRenameFocus_) {
                    ImGui::SetKeyboardFocusHere();
                    requestRenameFocus_ = false;
                }
                const bool submittedByWidget = ImGui::InputText("##Rename", renameBuffer_.data(), renameBuffer_.size(),
                                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                const bool submitted =
                    submittedByWidget && inputRouter_ != nullptr && inputRouter_->ConsumeKey(focusedWidgetContext_, Input::Key::Enter);
                const bool cancelled = inputRouter_ != nullptr && inputRouter_->ConsumeKey(focusedWidgetContext_, Input::Key::Escape);
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
                if (submitted) {
                    cmd = HierarchyEditSession::RenameCommand(node.id, renameBuffer_.data());
                    renamingId_.reset();
                } else if (cancelled) {
                    renamingId_.reset();
                }
            } else {
                const bool truncated = DrawHierarchyLabel(*drawList, *nameFont, nameFontSize, labelMin, labelMax, centerY,
                                                          Theme::U32(node.effectivelyLocked ? Theme::Muted() : Theme::Text()), node.name);
                if (truncated && ImGui::IsMouseHoveringRect(labelMin, labelMax))
                    ImGui::SetTooltip("%s", node.name.c_str());
            }
            ImGui::SetCursorScreenPos(nextRowCursor);
            ImGui::PopID();
        }

        if (visibleRows.empty()) {
            ImGui::SetCursorPosX(outerPadding + 8.0F * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
            ImGui::TextUnformatted(
                ctx.localization.Get("editor", searchBuffer_[0] == '\0' ? "workspace.hierarchy.empty" : "workspace.hierarchy.no_matches")
                    .c_str());
            ImGui::PopStyleColor();
        }

        const ImVec2 remaining = ImGui::GetContentRegionAvail();
        const ImVec2 rootDropMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##HierarchyRootDrop", ImVec2(std::max(1.0F, remaining.x), std::max(32.0F, remaining.y)));
        const ImVec2 rootDropMax{rootDropMin.x + std::max(1.0F, remaining.x), rootDropMin.y + std::max(32.0F, remaining.y)};
        static_cast<void>(AcceptAssetDrop(std::nullopt, AssetSceneDropTarget::HierarchyRoot, rootDropMin, rootDropMax, vm.documentRevision,
                                          cmd, *drawList));
        if (Ui::BeginContextMenu("##HierarchyRootContext")) {
            if (workspaceEligible &&
                Ui::BeginContextSubmenu((ctx.localization.Get("editor", "workspace.create") + "###hierarchy_create_root").c_str(),
                                        ctx.theme.fonts)) {
                DrawCreateMenuItems(GetPrimitiveCreateMenuItems(), std::nullopt, cmd, ctx);
                Ui::EndContextSubmenu();
            }
            Ui::EndContextMenu();
        }
        ImGui::PopStyleVar();

        if (workspaceEligible && panelFocused && !searchActive && !renamingId_.has_value() && editSession_.SelectedId().has_value()) {
            const Input::ModifierState &modifiers = inputRouter_->Snapshot().modifiers;
            if ((HierarchyEditSession::IsDeleteShortcut(Input::Key::Delete, modifiers) &&
                 inputRouter_->ConsumeKey(*workspaceInputContext_, Input::Key::Delete)) ||
                (HierarchyEditSession::IsDeleteShortcut(Input::Key::Backspace, modifiers) &&
                 inputRouter_->ConsumeKey(*workspaceInputContext_, Input::Key::Backspace))) {
                pendingDelete = true;
            }
        }
        if (pendingDelete) {
            renamingId_.reset();
            cmd = editSession_.DeleteSelectionCommand();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
}  // namespace Horo::Editor
