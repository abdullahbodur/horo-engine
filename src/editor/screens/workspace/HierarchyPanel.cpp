#include "HierarchyPanel.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace Horo::Editor
{
    namespace
    {
        constexpr float kTabHeight = 28.0F;
        constexpr float kOuterPadding = 8.0F;
        constexpr float kRowHeight = 24.0F;
        constexpr float kIndentWidth = 14.0F;

        struct BadgePresentation
        {
            const char* labelKey{nullptr};
            ImVec4 color{};
        };

        [[nodiscard]] BadgePresentation GetBadgePresentation(const HierarchyNodeType type)
        {
            switch (type)
            {
            case HierarchyNodeType::Mesh:
                return {"workspace.hierarchy.type.mesh", Theme::Ok()};
            case HierarchyNodeType::Empty:
                return {"workspace.hierarchy.type.empty", Theme::Dim()};
            case HierarchyNodeType::Light:
                return {"workspace.hierarchy.type.light", Theme::Warn()};
            case HierarchyNodeType::Camera:
                return {"workspace.hierarchy.type.camera", Theme::Accent()};
            case HierarchyNodeType::TriggerVolume:
                return {"workspace.hierarchy.type.volume", Theme::Warn()};
            case HierarchyNodeType::AudioSource:
                return {"workspace.hierarchy.type.audio", Theme::Accent()};
            case HierarchyNodeType::Collection:
                return {};
            }
            return {};
        }

        [[nodiscard]] ImFont* ResolveFont(ImFont* preferred)
        {
            return preferred != nullptr ? preferred : ImGui::GetFont();
        }

        void DrawCreateMenuItems(const std::vector<EditorMenuItem>& items, const std::optional<SceneObjectId> parent,
                                 EditorWorkspaceViewCommandData& command, const EditorGuiContext& context)
        {
            for (const EditorMenuItem& item : items)
            {
                const std::string& label = context.localization.Get("editor", item.labelKey);
                if (item.kind == EditorMenuItemKind::Submenu)
                {
                    if (Ui::BeginContextSubmenu(label.c_str(), context.theme.fonts))
                    {
                        DrawCreateMenuItems(item.children, parent, command, context);
                        Ui::EndContextSubmenu();
                    }
                    continue;
                }
                if (item.kind == EditorMenuItemKind::Command && item.action == EditorMenuAction::CreatePrimitive &&
                    item.primitive.has_value() &&
                    Ui::ContextMenuItem(label.c_str(), nullptr, context.theme.fonts))
                {
                    command.command = EditorWorkspaceViewCommand::CreatePrimitive;
                    command.primitivePayload = item.primitive;
                    command.objectPayload = parent;
                }
            }
        }
    } // namespace

    void HierarchyPanel::OnAttach(PanelContext& context)
    {
        inputRouter_ = context.inputRouter;
        workspaceInputContext_ = context.workspaceInputContext;
    }

    void HierarchyPanel::OnDetach()
    {
        focusedWidgetContext_.Reset();
        inputRouter_ = nullptr;
        workspaceInputContext_ = nullptr;
    }

    void HierarchyPanel::BeginRename(const HierarchyNodeId id)
    {
        const HierarchyNode* node = model_.Find(id);
        if (node == nullptr)
        {
            return;
        }
        std::snprintf(renameBuffer_.data(), renameBuffer_.size(), "%s", node->name.c_str());
        renamingId_ = id;
        requestRenameFocus_ = true;
    }

    void HierarchyPanel::DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, const ImU32 color)
    {
        const float ox = pos.x + (size.x - 14.0f) * 0.5f;
        const float oy = pos.y + (size.y - 14.0f) * 0.5f;

        // Simple hierarchy icon (nodes and branches)
        dl->AddLine(ImVec2(ox + 2, oy + 2), ImVec2(ox + 12, oy + 2), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 2), ImVec2(ox + 4, oy + 7), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 7), ImVec2(ox + 12, oy + 7), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 7), ImVec2(ox + 4, oy + 12), color, 1.5f);
        dl->AddLine(ImVec2(ox + 4, oy + 12), ImVec2(ox + 12, oy + 12), color, 1.5f);
    }

    void HierarchyPanel::DrawPanel(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& vm,
                                   EditorWorkspaceViewCommandData& cmd, const EditorGuiContext& ctx)
    {
        static_cast<void>(pos);
        if (!projectionInitialized_ || projectedRevision_ != vm.documentRevision)
        {
            hierarchyInputs_.clear();
            hierarchyInputs_.reserve(vm.objects.size());
            for (const SceneObject& object : vm.objects)
            {
                hierarchyInputs_.push_back(HierarchyNodeInput{
                    .id = object.id.value,
                    .parent = object.parent.has_value()
                                  ? std::optional<HierarchyNodeId>{object.parent->value}
                                  : std::nullopt,
                    .name = object.name,
                    .type = [&object]
                    {
                        switch (object.kind)
                        {
                        case SceneObjectKind::Mesh: return HierarchyNodeType::Mesh;
                        case SceneObjectKind::Camera: return HierarchyNodeType::Camera;
                        case SceneObjectKind::Light: return HierarchyNodeType::Light;
                        case SceneObjectKind::TriggerVolume: return HierarchyNodeType::TriggerVolume;
                        case SceneObjectKind::AudioSource: return HierarchyNodeType::AudioSource;
                        case SceneObjectKind::Empty: return HierarchyNodeType::Empty;
                        }
                        return HierarchyNodeType::Empty;
                    }(),
                });
            }
            model_.Replace(hierarchyInputs_);
            if (vm.hierarchyRevealObject.has_value() && vm.hierarchyRevealRevision != handledRevealRevision_)
            {
                std::optional<SceneObjectId> ancestor = vm.hierarchyRevealObject;
                while (ancestor.has_value())
                {
                    const auto object = std::ranges::find(vm.objects, *ancestor, &SceneObject::id);
                    if (object == vm.objects.end())
                    {
                        break;
                    }
                    if (object->parent.has_value())
                    {
                        static_cast<void>(model_.SetExpanded(object->parent->value, true));
                    }
                    ancestor = object->parent;
                }
                handledRevealRevision_ = vm.hierarchyRevealRevision;
            }
            projectedRevision_ = vm.documentRevision;
            projectionInitialized_ = true;
        }
        if (vm.primarySelection.has_value())
        {
            static_cast<void>(model_.Select(vm.primarySelection->value));
        }
        else
        {
            model_.ClearSelection();
        }
        const std::array tabNames{ctx.localization.Get("editor", "workspace.panel.hierarchy").c_str()};
        Ui::DrawDockTabs(tabNames, 0, ctx.theme.fonts);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::BeginChild("##HierarchyContent", ImVec2(size.x, size.y - kTabHeight), false,
                          ImGuiWindowFlags_NoSavedSettings);

        ImGui::SetCursorPos(ImVec2(kOuterPadding, kOuterPadding));
        ImGui::SetNextItemWidth(std::max(1.0F, size.x - kOuterPadding * 2.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0F, 5.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Layout::Radius);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::Bg3());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
        bool searchActive = false;
        {
            const Theme::ScopedFont searchFont(ctx.theme.fonts.sansCompact);
            ImGui::InputTextWithHint("##HierarchySearch",
                                     ctx.localization.Get("editor", "workspace.hierarchy.search").c_str(),
                                     searchBuffer_.data(), searchBuffer_.size());
            searchActive = ImGui::IsItemActive();
        }
        const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);

        const bool needsFocusedContext = searchActive || renamingId_.has_value();
        if (needsFocusedContext && !focusedWidgetContext_.IsActive() && inputRouter_ != nullptr)
            focusedWidgetContext_ = inputRouter_->PushContext(Input::InputContextId{"editor.hierarchy.text"},
                                                              Input::InputContextKind::FocusedGuiWidget);
        else if (!needsFocusedContext)
            focusedWidgetContext_.Reset();
        const bool workspaceEligible = inputRouter_ != nullptr && workspaceInputContext_ != nullptr &&
            inputRouter_->IsContextActive(*workspaceInputContext_);

        model_.BuildVisibleRows(searchBuffer_.data(), visibleRows_);
        if (workspaceEligible && panelFocused && !searchActive && !renamingId_.has_value() && model_.SelectedId().
            has_value())
        {
            if (inputRouter_->ConsumeKey(*workspaceInputContext_, Input::Key::F2))
            {
                BeginRename(*model_.SelectedId());
            }
        }

        std::optional<HierarchyNodeId> pendingDelete;

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
        const float listWidth = std::max(1.0F, size.x - kOuterPadding * 2.0F);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImFont* nameFont = ResolveFont(ctx.theme.fonts.sans);
        const float nameFontSize = nameFont->FontSize;
        ImFont* badgeFont = nameFont;
        const float badgeFontSize = nameFontSize;

        for (const HierarchyVisibleRow& row : visibleRows_)
        {
            const HierarchyNode& node = *row.node;
            ImGui::PushID(static_cast<int>(node.id));
            ImGui::SetCursorPosX(kOuterPadding);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const ImVec2 rowMax{rowMin.x + listWidth, rowMin.y + kRowHeight};
            ImGui::InvisibleButton("##HierarchyRow", ImVec2(listWidth, kRowHeight));
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = model_.SelectedId() == node.id;

            if (selected)
            {
                drawList->AddRectFilled(rowMin, rowMax, Theme::U32(Theme::AccentSoft()), Theme::Layout::Radius);
                drawList->AddRectFilled(rowMin, ImVec2(rowMin.x + 2.0F, rowMax.y), Theme::U32(Theme::Accent()),
                                        Theme::Layout::Radius);
            }
            else if (hovered)
            {
                drawList->AddRectFilled(rowMin, rowMax, Theme::U32(Theme::Hover()), Theme::Layout::Radius);
            }

            const float indent = 6.0F + static_cast<float>(row.depth) * kIndentWidth;
            const float centerY = rowMin.y + kRowHeight * 0.5F;
            const float chevronCenterX = rowMin.x + indent + 6.0F;
            if (row.depth > 0)
            {
                const float guideX = chevronCenterX - kIndentWidth;
                drawList->AddLine(ImVec2(guideX, rowMin.y), ImVec2(guideX, centerY), Theme::U32(Theme::Border()), 1.0F);
                drawList->AddLine(ImVec2(guideX, centerY), ImVec2(chevronCenterX - 3.0F, centerY),
                                  Theme::U32(Theme::Border()), 1.0F);
            }
            if (!node.children.empty())
            {
                if (node.expanded || searchBuffer_[0] != '\0')
                {
                    drawList->AddTriangleFilled(ImVec2(chevronCenterX - 3.0F, centerY - 2.0F),
                                                ImVec2(chevronCenterX + 3.0F, centerY - 2.0F),
                                                ImVec2(chevronCenterX, centerY + 2.0F), Theme::U32(Theme::Dim()));
                }
                else
                {
                    drawList->AddTriangleFilled(ImVec2(chevronCenterX - 2.0F, centerY - 3.0F),
                                                ImVec2(chevronCenterX - 2.0F, centerY + 3.0F),
                                                ImVec2(chevronCenterX + 2.0F, centerY), Theme::U32(Theme::Dim()));
                }
            }
            else
            {
                drawList->AddCircleFilled(ImVec2(chevronCenterX, centerY), 1.0F, Theme::U32(Theme::Dim()));
            }

            if (workspaceEligible && ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                const float mouseX = ImGui::GetIO().MousePos.x;
                if (!node.children.empty() && mouseX <= chevronCenterX + 7.0F && searchBuffer_[0] == '\0')
                {
                    static_cast<void>(model_.SetExpanded(node.id, !node.expanded));
                }
                else
                {
                    static_cast<void>(model_.Select(node.id));
                    cmd.command = EditorWorkspaceViewCommand::SelectObject;
                    cmd.objectPayload = SceneObjectId{node.id};
                }
            }

            if (workspaceEligible && ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                static_cast<void>(model_.Select(node.id));
                cmd.command = EditorWorkspaceViewCommand::SelectObject;
                cmd.objectPayload = SceneObjectId{node.id};
            }
            if (Ui::BeginContextMenu("##HierarchyContext"))
            {
                if (workspaceEligible &&
                    Ui::BeginContextSubmenu(ctx.localization.Get("editor", "workspace.create").c_str(),
                                            ctx.theme.fonts))
                {
                    DrawCreateMenuItems(GetPrimitiveCreateMenuItems(), SceneObjectId{node.id}, cmd, ctx);
                    Ui::EndContextSubmenu();
                }
                Ui::ContextMenuSeparator();
                if (workspaceEligible && Ui::ContextMenuItem(
                    ctx.localization.Get("editor", "workspace.hierarchy.rename").c_str(), "F2",
                    ctx.theme.fonts))
                {
                    BeginRename(node.id);
                }
                if (workspaceEligible && Ui::ContextMenuItem(
                    ctx.localization.Get("editor", "workspace.hierarchy.duplicate").c_str(), nullptr,
                    ctx.theme.fonts))
                {
                    cmd.command = EditorWorkspaceViewCommand::DuplicateObject;
                    cmd.objectPayload = SceneObjectId{node.id};
                }
                Ui::ContextMenuSeparator();
                if (workspaceEligible && Ui::ContextMenuItem(
                    ctx.localization.Get("editor", "workspace.hierarchy.delete").c_str(), "Delete",
                    ctx.theme.fonts, Ui::ContextMenuItemTone::Danger))
                {
                    pendingDelete = node.id;
                }
                Ui::EndContextMenu();
            }

            const BadgePresentation badge = GetBadgePresentation(node.type);
            float badgeMinX = rowMax.x;
            if (badge.labelKey != nullptr)
            {
                const std::string& badgeLabel = ctx.localization.Get("editor", badge.labelKey);
                const ImVec2 badgeTextSize = badgeFont->CalcTextSizeA(badgeFontSize, 100.0F, 0.0F, badgeLabel.c_str());
                const float badgeWidth = badgeTextSize.x + 14.0F;
                badgeMinX = rowMax.x - badgeWidth - 5.0F;
                ImVec4 badgeBackground = badge.color;
                badgeBackground.w = 0.12F;
                drawList->AddRectFilled(ImVec2(badgeMinX, centerY - 10.0F), ImVec2(rowMax.x - 5.0F, centerY + 10.0F),
                                        Theme::U32(badgeBackground), 4.0F);
                drawList->AddText(badgeFont, badgeFontSize, ImVec2(badgeMinX + 7.0F, centerY - badgeTextSize.y * 0.5F),
                                  Theme::U32(badge.color), badgeLabel.c_str());
            }

            const float nameX = chevronCenterX + 9.0F;
            if (renamingId_ == node.id)
            {
                ImGui::SetCursorScreenPos(ImVec2(nameX - 3.0F, rowMin.y + 1.0F));
                ImGui::SetNextItemWidth(std::max(32.0F, badgeMinX - nameX - 3.0F));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
                ImGui::PushStyleColor(ImGuiCol_Border, Theme::Accent());
                if (requestRenameFocus_)
                {
                    ImGui::SetKeyboardFocusHere();
                    requestRenameFocus_ = false;
                }
                const bool submittedByWidget =
                    ImGui::InputText("##Rename", renameBuffer_.data(), renameBuffer_.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                const bool submitted = submittedByWidget && inputRouter_ != nullptr &&
                    inputRouter_->ConsumeKey(focusedWidgetContext_, Input::Key::Enter);
                const bool cancelled = inputRouter_ != nullptr &&
                    inputRouter_->ConsumeKey(focusedWidgetContext_, Input::Key::Escape);
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
                if (submitted)
                {
                    cmd.command = EditorWorkspaceViewCommand::UpdateObjectName;
                    cmd.objectPayload = SceneObjectId{node.id};
                    cmd.stringPayload = std::string(renameBuffer_.data());
                    renamingId_.reset();
                }
                else if (cancelled)
                {
                    renamingId_.reset();
                }
            }
            else
            {
                drawList->PushClipRect(ImVec2(nameX, rowMin.y), ImVec2(badgeMinX - 4.0F, rowMax.y), true);
                const ImVec2 nameSize = nameFont->CalcTextSizeA(nameFontSize, 1000.0F, 0.0F, node.name.c_str());
                drawList->AddText(nameFont, nameFontSize, ImVec2(nameX, centerY - nameSize.y * 0.5F),
                                  Theme::U32(selected ? Theme::Text() : Theme::Muted()), node.name.c_str());
                drawList->PopClipRect();
            }
            ImGui::PopID();
        }

        if (visibleRows_.empty())
        {
            ImGui::SetCursorPosX(kOuterPadding + 8.0F);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
            ImGui::TextUnformatted(ctx.localization
                                      .Get("editor", searchBuffer_[0] == '\0'
                                                         ? "workspace.hierarchy.empty"
                                                         : "workspace.hierarchy.no_matches")
                                      .c_str());
            ImGui::PopStyleColor();
        }

        const ImVec2 remaining = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##HierarchyRootDrop",
                               ImVec2(std::max(1.0F, remaining.x), std::max(32.0F, remaining.y)));
        if (Ui::BeginContextMenu("##HierarchyRootContext"))
        {
            if (workspaceEligible &&
                Ui::BeginContextSubmenu(ctx.localization.Get("editor", "workspace.create").c_str(), ctx.theme.fonts))
            {
                DrawCreateMenuItems(GetPrimitiveCreateMenuItems(), std::nullopt, cmd, ctx);
                Ui::EndContextSubmenu();
            }
            Ui::EndContextMenu();
        }
        ImGui::PopStyleVar();

        if (workspaceEligible && panelFocused && !searchActive && !renamingId_.has_value() && model_.SelectedId().
            has_value())
        {
            if (inputRouter_->ConsumeKey(*workspaceInputContext_, Input::Key::Delete) ||
                (inputRouter_->Snapshot().modifiers.command &&
                    inputRouter_->ConsumeKey(*workspaceInputContext_, Input::Key::Backspace)))
            {
                pendingDelete = *model_.SelectedId();
            }
        }
        if (pendingDelete.has_value())
        {
            if (renamingId_ == pendingDelete)
            {
                renamingId_.reset();
            }
            cmd.command = EditorWorkspaceViewCommand::DeleteObject;
            cmd.objectPayload = SceneObjectId{*pendingDelete};
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
} // namespace Horo::Editor
