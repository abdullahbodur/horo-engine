#include "HierarchyPanel.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <algorithm>
#include <array>
#include <cstdio>
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
constexpr char kHierarchyPayload[] = "HORO_HIERARCHY_NODE";

struct BadgePresentation
{
    const char *label{nullptr};
    ImVec4 color{};
};

[[nodiscard]] BadgePresentation GetBadgePresentation(const HierarchyNodeType type)
{
    switch (type)
    {
    case HierarchyNodeType::Mesh:
        return {"Mesh", Theme::Ok()};
    case HierarchyNodeType::Empty:
        return {"Empty", Theme::Dim()};
    case HierarchyNodeType::Light:
        return {"Light", Theme::Warn()};
    case HierarchyNodeType::Camera:
        return {"Camera", Theme::Accent()};
    case HierarchyNodeType::Collection:
        return {};
    }
    return {};
}

[[nodiscard]] ImFont *ResolveFont(ImFont *preferred)
{
    return preferred != nullptr ? preferred : ImGui::GetFont();
}
} // namespace

HierarchyPanel::HierarchyPanel() : model_(CreateMockHierarchyModel())
{
    visibleRows_.reserve(128);
}

void HierarchyPanel::BeginRename(const HierarchyNodeId id)
{
    const HierarchyNode *node = model_.Find(id);
    if (node == nullptr)
    {
        return;
    }
    std::snprintf(renameBuffer_.data(), renameBuffer_.size(), "%s", node->name.c_str());
    renamingId_ = id;
    requestRenameFocus_ = true;
}

void HierarchyPanel::DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, const ImU32 color)
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

void HierarchyPanel::DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                               EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx)
{
    static_cast<void>(pos);
    static_cast<void>(vm);
    static_cast<void>(cmd);
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

    model_.BuildVisibleRows(searchBuffer_.data(), visibleRows_);
    if (panelFocused && !searchActive && !renamingId_.has_value() && model_.SelectedId().has_value())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
        {
            BeginRename(*model_.SelectedId());
        }
    }

    std::optional<HierarchyNodeId> pendingDelete;
    struct PendingMove
    {
        HierarchyNodeId source{0};
        std::optional<HierarchyNodeId> parent;
    };
    std::optional<PendingMove> pendingMove;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
    const float listWidth = std::max(1.0F, size.x - kOuterPadding * 2.0F);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImFont *nameFont = ResolveFont(ctx.theme.fonts.sans);
    const float nameFontSize = nameFont->FontSize;
    ImFont *badgeFont = nameFont;
    const float badgeFontSize = nameFontSize;

    for (const HierarchyVisibleRow &row : visibleRows_)
    {
        const HierarchyNode &node = *row.node;
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

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            const float mouseX = ImGui::GetIO().MousePos.x;
            if (!node.children.empty() && mouseX <= chevronCenterX + 7.0F && searchBuffer_[0] == '\0')
            {
                static_cast<void>(model_.SetExpanded(node.id, !node.expanded));
            }
            else
            {
                static_cast<void>(model_.Select(node.id));
            }
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload(kHierarchyPayload, &node.id, sizeof(node.id));
            ImGui::TextUnformatted(node.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload =
                    ImGui::AcceptDragDropPayload(kHierarchyPayload, ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                        ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                if (payload->DataSize == sizeof(HierarchyNodeId))
                {
                    const auto source = *static_cast<const HierarchyNodeId *>(payload->Data);
                    const float relativeY = (ImGui::GetIO().MousePos.y - rowMin.y) / kRowHeight;
                    const bool asChild = relativeY >= 0.25F && relativeY <= 0.75F;
                    if (asChild)
                    {
                        drawList->AddRect(rowMin, rowMax, Theme::U32(Theme::Accent()), Theme::Layout::Radius, 0, 1.5F);
                    }
                    else
                    {
                        const float lineY = relativeY < 0.5F ? rowMin.y : rowMax.y;
                        drawList->AddLine(ImVec2(rowMin.x, lineY), ImVec2(rowMax.x, lineY), Theme::U32(Theme::Accent()),
                                          2.0F);
                    }
                    if (payload->IsDelivery())
                    {
                        pendingMove = PendingMove{.source = source,
                                                  .parent = asChild ? std::optional<HierarchyNodeId>{node.id}
                                                                    : model_.ParentId(node.id)};
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("##HierarchyContext"))
        {
            static_cast<void>(model_.Select(node.id));
            if (ImGui::MenuItem(ctx.localization.Get("editor", "workspace.hierarchy.rename").c_str(), "F2"))
            {
                BeginRename(node.id);
            }
            if (ImGui::MenuItem(ctx.localization.Get("editor", "workspace.hierarchy.move_to_root").c_str(), nullptr,
                                false, model_.ParentId(node.id).has_value()))
            {
                pendingMove = PendingMove{.source = node.id, .parent = std::nullopt};
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ctx.localization.Get("editor", "workspace.hierarchy.delete").c_str(), "Delete"))
            {
                pendingDelete = node.id;
            }
            ImGui::EndPopup();
        }

        const BadgePresentation badge = GetBadgePresentation(node.type);
        float badgeMinX = rowMax.x;
        if (badge.label != nullptr)
        {
            const ImVec2 badgeTextSize = badgeFont->CalcTextSizeA(badgeFontSize, 100.0F, 0.0F, badge.label);
            const float badgeWidth = badgeTextSize.x + 14.0F;
            badgeMinX = rowMax.x - badgeWidth - 5.0F;
            ImVec4 badgeBackground = badge.color;
            badgeBackground.w = 0.12F;
            drawList->AddRectFilled(ImVec2(badgeMinX, centerY - 10.0F), ImVec2(rowMax.x - 5.0F, centerY + 10.0F),
                                    Theme::U32(badgeBackground), 4.0F);
            drawList->AddText(badgeFont, badgeFontSize, ImVec2(badgeMinX + 7.0F, centerY - badgeTextSize.y * 0.5F),
                              Theme::U32(badge.color), badge.label);
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
            const bool submitted =
                ImGui::InputText("##Rename", renameBuffer_.data(), renameBuffer_.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            if (submitted)
            {
                if (model_.Rename(node.id, renameBuffer_.data()) == HierarchyMutationResult::Success)
                {
                    renamingId_.reset();
                }
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
                                   .Get("editor", searchBuffer_[0] == '\0' ? "workspace.hierarchy.empty"
                                                                           : "workspace.hierarchy.no_matches")
                                   .c_str());
        ImGui::PopStyleColor();
    }

    const ImVec2 rootDropMin = ImGui::GetCursorScreenPos();
    const ImVec2 remaining = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##HierarchyRootDrop", ImVec2(std::max(1.0F, remaining.x), std::max(32.0F, remaining.y)));
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload =
                ImGui::AcceptDragDropPayload(kHierarchyPayload, ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
        {
            drawList->AddRect(
                rootDropMin,
                ImVec2(rootDropMin.x + std::max(1.0F, remaining.x), rootDropMin.y + std::max(32.0F, remaining.y)),
                Theme::U32(Theme::Accent()), Theme::Layout::Radius, 0, 1.0F);
            if (payload->DataSize == sizeof(HierarchyNodeId) && payload->IsDelivery())
            {
                pendingMove =
                    PendingMove{.source = *static_cast<const HierarchyNodeId *>(payload->Data), .parent = std::nullopt};
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopStyleVar();

    if (panelFocused && !searchActive && !renamingId_.has_value() && model_.SelectedId().has_value())
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
            (io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
        {
            pendingDelete = *model_.SelectedId();
        }
    }
    if (pendingMove.has_value())
    {
        static_cast<void>(model_.Reparent(pendingMove->source, pendingMove->parent));
    }
    if (pendingDelete.has_value())
    {
        if (renamingId_ == pendingDelete)
        {
            renamingId_.reset();
        }
        static_cast<void>(model_.Delete(*pendingDelete));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace Horo::Editor
