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

        struct HierarchyLabelDrawRequest {
            ImDrawList &drawList;
            ImFont &font;
            float fontSize{0.0F};
            ImVec2 minimum{};
            ImVec2 maximum{};
            float centerY{0.0F};
            ImU32 color{};
            std::string_view text;
        };

        [[nodiscard]] bool DrawHierarchyLabel(const HierarchyLabelDrawRequest &request) {
            const auto &[drawList, font, fontSize, minimum, maximum, centerY, color, text] = request;
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

    struct HierarchyPanel::PanelInteractionState {
        bool searchActive{false};
        bool panelFocused{false};
        bool workspaceEligible{false};
    };

    struct HierarchyRowGeometry {
        HierarchyRowLayout layout;
        ImVec2 rowMin{};
        ImVec2 rowMax{};
        ImVec2 chevronMin{};
        ImVec2 chevronMax{};
        ImVec2 typeIconMin{};
        ImVec2 typeIconMax{};
        ImVec2 labelMin{};
        ImVec2 labelMax{};
        ImVec2 actionsMin{};
        ImVec2 actionsMax{};
        ImVec2 visibilityMin{};
        ImVec2 visibilityMax{};
        ImVec2 lockMin{};
        ImVec2 lockMax{};
        ImVec2 nextRowCursor{};
    };

    struct HierarchyPanel::RowFrame {
        const HierarchyVisibleRow &row;
        const HierarchyNode &node;
        ImDrawList &drawList;
        ImFont &nameFont;
        HierarchyRowGeometry geometry;
        float uiScale{1.0F};
        float nameFontSize{0.0F};
        bool rowHovered{false};
        bool rowFocused{false};
        bool rowLeftClicked{false};
        bool rowRightClicked{false};
        bool selected{false};
        bool primarySelected{false};
        bool pointerInActions{false};
        bool assetDropDelivered{false};
        bool searching{false};
    };

    struct HierarchyPanel::RowControls {
        bool chevronHovered{false};
        bool chevronPressed{false};
        bool visibilityHovered{false};
        bool visibilityPressed{false};
        bool lockHovered{false};
        bool lockPressed{false};

        [[nodiscard]] bool IsHovered(const RowFrame &frame) const noexcept {
            return frame.rowHovered || chevronHovered || visibilityHovered || lockHovered;
        }
    };

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

    HierarchyPanel::PanelInteractionState HierarchyPanel::DrawSearch(const float panelWidth, const float uiScale,
                                                                     const EditorGuiContext &context) {
        ImGui::SetCursorPos(ImVec2(kOuterPadding * uiScale, kOuterPadding * uiScale));
        ImGui::SetNextItemWidth(std::max(1.0F, panelWidth - kOuterPadding * 2.0F * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0F * uiScale, 5.0F * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
        bool searchActive = false;
        {
            const Theme::ScopedFont searchFont(context.theme.fonts.sansCompact);
            ImGui::InputTextWithHint("##HierarchySearch", context.localization.Get("editor", "workspace.hierarchy.search").c_str(),
                                     searchBuffer_.data(), searchBuffer_.size());
            searchActive = ImGui::IsItemActive();
        }
        const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        const bool workspaceEligible =
            inputRouter_ != nullptr && workspaceInputContext_ != nullptr && inputRouter_->IsContextActive(*workspaceInputContext_);
        return {.searchActive = searchActive, .panelFocused = panelFocused, .workspaceEligible = workspaceEligible};
    }

    void HierarchyPanel::UpdateFocusedInputContext(const bool searchActive) {
        const bool needsFocusedContext = searchActive || renamingId_.has_value();
        if (needsFocusedContext && !focusedWidgetContext_.IsActive() && inputRouter_ != nullptr)
            focusedWidgetContext_ =
                inputRouter_->PushContext(Input::InputContextId{"editor.hierarchy.text"}, Input::InputContextKind::FocusedGuiWidget);
        else if (!needsFocusedContext)
            focusedWidgetContext_.Reset();
    }

    void HierarchyPanel::HandleRenameShortcut(const PanelInteractionState &interaction) {
        if (!interaction.workspaceEligible || !interaction.panelFocused || interaction.searchActive || renamingId_.has_value() ||
            !editSession_.SelectedId().has_value() || !inputRouter_->ConsumeKey(*workspaceInputContext_, Input::Key::F2))
            return;
        const HierarchyNode *selectedNode = editSession_.Find(*editSession_.SelectedId());
        if (selectedNode != nullptr && !selectedNode->effectivelyLocked)
            BeginRename(*editSession_.SelectedId());
    }

    void HierarchyPanel::DrawRowContextMenu(const RowFrame &frame, const bool workspaceEligible, bool &pendingDelete,
                                            EditorWorkspaceViewCommandData &command, const EditorGuiContext &context) {
        if (frame.pointerInActions || !Ui::BeginContextMenu("##HierarchyContext"))
            return;
        const bool editable = workspaceEligible && !frame.node.effectivelyLocked;
        if (editable &&
            Ui::BeginContextSubmenu((context.localization.Get("editor", "workspace.create") + "###hierarchy_create_root").c_str(),
                                    context.theme.fonts)) {
            DrawCreateMenuItems(GetPrimitiveCreateMenuItems(), SceneObjectId{frame.node.id}, command, context);
            Ui::EndContextSubmenu();
        }
        Ui::ContextMenuSeparator();
        if (editable &&
            Ui::ContextMenuItem(context.localization.Get("editor", "workspace.hierarchy.rename").c_str(), "F2", context.theme.fonts))
            BeginRename(frame.node.id);
        if (editable &&
            Ui::ContextMenuItem(context.localization.Get("editor", "workspace.hierarchy.duplicate").c_str(), nullptr, context.theme.fonts))
            command = HierarchyEditSession::DuplicateCommand(frame.node.id);
        Ui::ContextMenuSeparator();
        if (editable && Ui::ContextMenuItem(context.localization.Get("editor", "workspace.hierarchy.delete").c_str(), "Delete",
                                            context.theme.fonts, Ui::ContextMenuItemTone::Danger))
            pendingDelete = true;
        Ui::EndContextMenu();
    }

    HierarchyPanel::RowControls HierarchyPanel::DrawRowControls(const RowFrame &frame, const bool workspaceEligible,
                                                                const EditorGuiContext &context) {
        RowControls controls;
        if (!frame.node.children.empty() && searchBuffer_[0] == '\0') {
            ImGui::SetCursorScreenPos(frame.geometry.chevronMin);
            ImGui::InvisibleButton("##hierarchy_chevron", ImVec2(frame.geometry.layout.chevron.Width(), frame.geometry.layout.height));
            controls.chevronHovered = ImGui::IsItemHovered();
            controls.chevronPressed = workspaceEligible && ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImGui::SetCursorScreenPos(frame.geometry.nextRowCursor);
        }
        if (frame.geometry.layout.visibilityAction.Width() <= 0.0F)
            return controls;

        ImGui::SetCursorScreenPos(frame.geometry.visibilityMin);
        ImGui::InvisibleButton("##hierarchy_visibility",
                               ImVec2(frame.geometry.layout.visibilityAction.Width(), frame.geometry.layout.height));
        controls.visibilityHovered = ImGui::IsItemHovered();
        controls.visibilityPressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        if (controls.visibilityHovered) {
            const char *tooltip = "workspace.hierarchy.show";
            if (frame.node.hiddenByParent && frame.node.locallyVisible)
                tooltip = "workspace.hierarchy.hidden_by_parent";
            else if (frame.node.locallyVisible)
                tooltip = "workspace.hierarchy.hide";
            ImGui::SetTooltip("%s", context.localization.Get("editor", tooltip).c_str());
        }

        ImGui::SetCursorScreenPos(frame.geometry.lockMin);
        ImGui::InvisibleButton("##hierarchy_lock", ImVec2(frame.geometry.layout.lockAction.Width(), frame.geometry.layout.height));
        controls.lockHovered = ImGui::IsItemHovered();
        controls.lockPressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        if (controls.lockHovered) {
            const char *tooltip = "workspace.hierarchy.lock";
            if (frame.node.lockedByParent && !frame.node.locallyLocked)
                tooltip = "workspace.hierarchy.locked_by_parent";
            else if (frame.node.locallyLocked)
                tooltip = "workspace.hierarchy.unlock";
            ImGui::SetTooltip("%s", context.localization.Get("editor", tooltip).c_str());
        }
        ImGui::SetCursorScreenPos(frame.geometry.nextRowCursor);
        return controls;
    }

    void HierarchyPanel::DrawRowPresentation(const RowFrame &frame, const RowControls &controls, const EditorGuiContext &context) {
        const bool hovered = controls.IsHovered(frame);
        if (frame.selected) {
            ImVec4 selectedBackground = Theme::Accent();
            selectedBackground.w = hovered ? 0.17F : 0.13F;
            frame.drawList.AddRectFilled(frame.geometry.rowMin, frame.geometry.rowMax, Theme::U32(selectedBackground),
                                         2.0F * frame.uiScale);
            if (frame.primarySelected)
                frame.drawList.AddRectFilled(frame.geometry.rowMin,
                                             {frame.geometry.rowMin.x + 2.0F * frame.uiScale, frame.geometry.rowMax.y},
                                             Theme::U32(Theme::Accent()), 2.0F * frame.uiScale);
        } else if (hovered) {
            frame.drawList.AddRectFilled(frame.geometry.rowMin, frame.geometry.rowMax, Theme::U32(Theme::Hover()), 2.0F * frame.uiScale);
        }
        if (frame.rowFocused)
            frame.drawList.AddRect(frame.geometry.rowMin, frame.geometry.rowMax, Theme::U32(Theme::BorderStrong()), 2.0F * frame.uiScale, 0,
                                   frame.uiScale);

        const float centerY = frame.geometry.rowMin.y + frame.geometry.layout.height * 0.5F;
        const float chevronCenterX = (frame.geometry.chevronMin.x + frame.geometry.chevronMax.x) * 0.5F;
        if (frame.row.depth > 0) {
            const float guideX = chevronCenterX - 12.0F * frame.uiScale;
            frame.drawList.AddLine({guideX, frame.geometry.rowMin.y}, {guideX, centerY}, Theme::U32(Theme::Border()), 1.0F);
            frame.drawList.AddLine({guideX, centerY}, {frame.geometry.chevronMin.x, centerY}, Theme::U32(Theme::Border()), 1.0F);
        }
        if (!frame.node.children.empty()) {
            if (frame.node.expanded || frame.searching)
                frame.drawList.AddTriangleFilled({chevronCenterX - 3.0F * frame.uiScale, centerY - 2.0F * frame.uiScale},
                                                 {chevronCenterX + 3.0F * frame.uiScale, centerY - 2.0F * frame.uiScale},
                                                 {chevronCenterX, centerY + 2.0F * frame.uiScale},
                                                 Theme::U32(controls.chevronHovered ? Theme::Text() : Theme::Dim()));
            else
                frame.drawList.AddTriangleFilled({chevronCenterX - 2.0F * frame.uiScale, centerY - 3.0F * frame.uiScale},
                                                 {chevronCenterX - 2.0F * frame.uiScale, centerY + 3.0F * frame.uiScale},
                                                 {chevronCenterX + 2.0F * frame.uiScale, centerY},
                                                 Theme::U32(controls.chevronHovered ? Theme::Text() : Theme::Dim()));
        }

        const HierarchyIconPresentation icon = GetIconPresentation(frame.node.type);
        const float iconSize = 16.0F * frame.uiScale;
        ImVec4 typeColor = icon.color;
        if (frame.node.effectivelyLocked)
            typeColor.w *= 0.65F;
        Ui::DrawEditorIcon(&frame.drawList, icon.icon, {frame.geometry.typeIconMin.x, centerY - iconSize * 0.5F}, {iconSize, iconSize},
                           Theme::U32(typeColor));
        if (icon.tooltipKey != nullptr && ImGui::IsMouseHoveringRect(frame.geometry.typeIconMin, frame.geometry.typeIconMax))
            ImGui::SetTooltip("%s", context.localization.Get("editor", icon.tooltipKey).c_str());

        if (frame.geometry.layout.visibilityAction.Width() <= 0.0F)
            return;
        const float actionIconSize =
            std::max(0.0F, std::min({15.0F * frame.uiScale, frame.geometry.layout.visibilityAction.Width() - 4.0F * frame.uiScale,
                                     frame.geometry.layout.lockAction.Width() - 4.0F * frame.uiScale,
                                     frame.geometry.layout.height - 4.0F * frame.uiScale}));
        const auto drawAction = [&](const Ui::UiIcon actionIcon, const ImVec2 minimum, const ImVec2 maximum, const bool actionHovered,
                                    const bool active, const bool inherited) {
            if (actionIconSize <= 0.0F)
                return;
            ImVec4 color = actionHovered || active ? Theme::Text() : Theme::Muted();
            if (active && !actionHovered)
                color.w *= 0.82F;
            if (inherited)
                color.w *= 0.55F;
            const ImVec2 position{minimum.x + ((maximum.x - minimum.x) - actionIconSize) * 0.5F,
                                  minimum.y + ((maximum.y - minimum.y) - actionIconSize) * 0.5F};
            Ui::DrawEditorIcon(&frame.drawList, actionIcon, position, {actionIconSize, actionIconSize}, Theme::U32(color));
        };
        drawAction(frame.node.effectivelyVisible ? Ui::UiIcon::Visibility : Ui::UiIcon::VisibilityOff, frame.geometry.visibilityMin,
                   frame.geometry.visibilityMax, controls.visibilityHovered, !frame.node.effectivelyVisible,
                   frame.node.hiddenByParent && frame.node.locallyVisible);
        drawAction(Ui::UiIcon::Lock, frame.geometry.lockMin, frame.geometry.lockMax, controls.lockHovered, frame.node.effectivelyLocked,
                   frame.node.lockedByParent && !frame.node.locallyLocked);
    }

    void HierarchyPanel::ApplyRowInteraction(const RowFrame &frame, const RowControls &controls, const bool workspaceEligible,
                                             EditorWorkspaceViewCommandData &command) {
        if (frame.assetDropDelivered)
            return;
        if (controls.chevronPressed) {
            editSession_.ToggleExpanded(frame.node.id);
            return;
        }
        if (workspaceEligible && controls.visibilityPressed) {
            if (!(frame.node.hiddenByParent && frame.node.locallyVisible))
                command = HierarchyEditSession::ToggleVisibilityCommand(frame.node);
            return;
        }
        if (workspaceEligible && controls.lockPressed) {
            if (!(frame.node.lockedByParent && !frame.node.locallyLocked))
                command = HierarchyEditSession::ToggleLockCommand(frame.node);
            return;
        }
        if (!workspaceEligible || !frame.rowLeftClicked || frame.pointerInActions)
            return;
        editSession_.Select(frame.node.id);
        const ImGuiIO &io = ImGui::GetIO();
        HierarchySelectionGesture gesture = HierarchySelectionGesture::Replace;
        if (io.KeyShift)
            gesture = HierarchySelectionGesture::Range;
        else if (io.KeyCtrl || io.KeySuper)
            gesture = HierarchySelectionGesture::Toggle;
        command = editSession_.SelectCommand(frame.node.id, gesture);
    }

    void HierarchyPanel::DrawRowLabel(const RowFrame &frame, EditorWorkspaceViewCommandData &command) {
        const float centerY = frame.geometry.rowMin.y + frame.geometry.layout.height * 0.5F;
        if (renamingId_ != frame.node.id) {
            const bool truncated = DrawHierarchyLabel({.drawList = frame.drawList,
                                                       .font = frame.nameFont,
                                                       .fontSize = frame.nameFontSize,
                                                       .minimum = frame.geometry.labelMin,
                                                       .maximum = frame.geometry.labelMax,
                                                       .centerY = centerY,
                                                       .color = Theme::U32(frame.node.effectivelyLocked ? Theme::Muted() : Theme::Text()),
                                                       .text = frame.node.name});
            if (truncated && ImGui::IsMouseHoveringRect(frame.geometry.labelMin, frame.geometry.labelMax))
                ImGui::SetTooltip("%s", frame.node.name.c_str());
            return;
        }

        ImGui::SetCursorScreenPos({frame.geometry.labelMin.x, frame.geometry.rowMin.y + 2.0F * frame.uiScale});
        ImGui::SetNextItemWidth(std::max(1.0F, frame.geometry.labelMax.x - frame.geometry.labelMin.x));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {3.0F * frame.uiScale, 1.0F * frame.uiScale});
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
            command = HierarchyEditSession::RenameCommand(frame.node.id, renameBuffer_.data());
            renamingId_.reset();
        } else if (cancelled) {
            renamingId_.reset();
        }
    }

    bool HierarchyPanel::DrawRows(const std::vector<HierarchyVisibleRow> &rows, const float listWidth, const float outerPadding,
                                  const float uiScale, const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &command,
                                  const EditorGuiContext &context) {
        bool pendingDelete = false;
        ImDrawList &drawList = *ImGui::GetWindowDrawList();
        ImFont &nameFont = *ResolveFont(context.theme.fonts.sans);
        const float nameFontSize = nameFont.FontSize * uiScale;
        const bool workspaceEligible =
            inputRouter_ != nullptr && workspaceInputContext_ != nullptr && inputRouter_->IsContextActive(*workspaceInputContext_);

        for (const HierarchyVisibleRow &row : rows) {
            const HierarchyNode &node = *row.node;
            ImGui::PushID(&node.id);
            ImGui::SetCursorPosX(outerPadding);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const HierarchyRowLayout layout = CalculateHierarchyRowLayout(listWidth, row.depth, uiScale, kRowActionsWidth * uiScale);
            const ImVec2 rowMax{rowMin.x + listWidth, rowMin.y + layout.height};
            const ImVec2 actionsMin{rowMin.x + layout.actions.minimum, rowMin.y};
            const ImVec2 actionsMax{rowMin.x + layout.actions.maximum, rowMax.y};
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##hierarchy_object_row", {listWidth, layout.height});
            const ImVec2 nextRowCursor = ImGui::GetCursorScreenPos();
            const bool rowHovered = ImGui::IsItemHovered();
            const bool rowFocused = ImGui::IsItemFocused();
            const bool rowLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool rowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            const bool pointerInActions = ImGui::IsMouseHoveringRect(actionsMin, actionsMax);
            const bool assetDropDelivered = AcceptAssetDrop(SceneObjectId{node.id}, AssetSceneDropTarget::HierarchyChild, rowMin, rowMax,
                                                            viewModel.documentRevision, command, drawList);
            const RowFrame frame{
                .row = row,
                .node = node,
                .drawList = drawList,
                .nameFont = nameFont,
                .geometry =
                    {
                        .layout = layout,
                        .rowMin = rowMin,
                        .rowMax = rowMax,
                        .chevronMin = {rowMin.x + layout.chevron.minimum, rowMin.y},
                        .chevronMax = {rowMin.x + layout.chevron.maximum, rowMax.y},
                        .typeIconMin = {rowMin.x + layout.typeIcon.minimum, rowMin.y},
                        .typeIconMax = {rowMin.x + layout.typeIcon.maximum, rowMax.y},
                        .labelMin = {rowMin.x + layout.label.minimum, rowMin.y},
                        .labelMax = {rowMin.x + layout.label.maximum, rowMax.y},
                        .actionsMin = actionsMin,
                        .actionsMax = actionsMax,
                        .visibilityMin = {rowMin.x + layout.visibilityAction.minimum, rowMin.y},
                        .visibilityMax = {rowMin.x + layout.visibilityAction.maximum, rowMax.y},
                        .lockMin = {rowMin.x + layout.lockAction.minimum, rowMin.y},
                        .lockMax = {rowMin.x + layout.lockAction.maximum, rowMax.y},
                        .nextRowCursor = nextRowCursor,
                    },
                .uiScale = uiScale,
                .nameFontSize = nameFontSize,
                .rowHovered = rowHovered,
                .rowFocused = rowFocused,
                .rowLeftClicked = rowLeftClicked,
                .rowRightClicked = rowRightClicked,
                .selected = editSession_.IsSelected(node.id),
                .primarySelected = editSession_.SelectedId() == node.id,
                .pointerInActions = pointerInActions,
                .assetDropDelivered = assetDropDelivered,
                .searching = searchBuffer_[0] != '\0',
            };

            if (workspaceEligible && frame.rowRightClicked && !frame.pointerInActions && !frame.selected) {
                editSession_.Select(node.id);
                command = editSession_.SelectCommand(node.id, HierarchySelectionGesture::Replace);
            }
            DrawRowContextMenu(frame, workspaceEligible, pendingDelete, command, context);
            const RowControls controls = DrawRowControls(frame, workspaceEligible, context);
            DrawRowPresentation(frame, controls, context);
            ApplyRowInteraction(frame, controls, workspaceEligible, command);
            DrawRowLabel(frame, command);
            ImGui::SetCursorScreenPos(nextRowCursor);
            ImGui::PopID();
        }

        if (rows.empty()) {
            ImGui::SetCursorPosX(outerPadding + 8.0F * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
            ImGui::TextUnformatted(
                context.localization
                    .Get("editor", searchBuffer_[0] == '\0' ? "workspace.hierarchy.empty" : "workspace.hierarchy.no_matches")
                    .c_str());
            ImGui::PopStyleColor();
        }
        return pendingDelete;
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

        const PanelInteractionState interaction = DrawSearch(size.x, uiScale, ctx);
        UpdateFocusedInputContext(interaction.searchActive);
        const std::vector<HierarchyVisibleRow> &visibleRows = editSession_.VisibleRows(searchBuffer_.data());
        HandleRenameShortcut(interaction);

        const float outerPadding = kOuterPadding * uiScale;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0F * uiScale);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
        const float listWidth = std::max(1.0F, size.x - outerPadding * 2.0F);
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        bool pendingDelete = DrawRows(visibleRows, listWidth, outerPadding, uiScale, vm, cmd, ctx);

        const ImVec2 remaining = ImGui::GetContentRegionAvail();
        const ImVec2 rootDropMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##HierarchyRootDrop", ImVec2(std::max(1.0F, remaining.x), std::max(32.0F, remaining.y)));
        const ImVec2 rootDropMax{rootDropMin.x + std::max(1.0F, remaining.x), rootDropMin.y + std::max(32.0F, remaining.y)};
        static_cast<void>(AcceptAssetDrop(std::nullopt, AssetSceneDropTarget::HierarchyRoot, rootDropMin, rootDropMax, vm.documentRevision,
                                          cmd, *drawList));
        if (Ui::BeginContextMenu("##HierarchyRootContext")) {
            if (interaction.workspaceEligible &&
                Ui::BeginContextSubmenu((ctx.localization.Get("editor", "workspace.create") + "###hierarchy_create_root").c_str(),
                                        ctx.theme.fonts)) {
                DrawCreateMenuItems(GetPrimitiveCreateMenuItems(), std::nullopt, cmd, ctx);
                Ui::EndContextSubmenu();
            }
            Ui::EndContextMenu();
        }
        ImGui::PopStyleVar();

        if (interaction.workspaceEligible && interaction.panelFocused && !interaction.searchActive && !renamingId_.has_value() &&
            editSession_.SelectedId().has_value()) {
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
