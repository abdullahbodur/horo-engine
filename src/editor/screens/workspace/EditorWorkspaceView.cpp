#include "EditorWorkspaceView.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "editor/menu/EditorMenuPlatform.h"

#include <algorithm>
#include <array>
#include <format>
#include <numbers>

namespace Horo::Editor
{
    constexpr float kMenuBarH = 28.0F;
    constexpr float kToolbarH = 38.0F;
    constexpr float kActivityBarW = 36.0F;
    constexpr float kMinimumDocumentW = 120.0F;
    constexpr float kMinimumMainH = 100.0F;

    EditorWorkspaceView::EditorWorkspaceView(const EditorGuiContext& context,
                                             const WorkspacePanelRegistry& panelRegistry,
                                             const std::uintptr_t logoTexture, Input::InputRouter& inputRouter,
                                             Input::InputContextToken& workspaceInputContext)
        : m_context(context), m_panelRegistry(panelRegistry), m_logoTexture(logoTexture), m_inputRouter(inputRouter),
          m_workspaceInputContext(workspaceInputContext)
    {
    }

    bool EditorWorkspaceView::EnsurePanelDragCapture() const
    {
        if (m_panelDragCapture.IsActive()) return true;
        if (!m_inputRouter.Snapshot().State(Input::PointerButton::Primary).down) return false;
        m_panelDragContext = m_inputRouter.PushContext(Input::InputContextId{"editor.workspace.panel_drag"},
                                                       Input::InputContextKind::EditorToolCapture);
        Result<Input::PointerCaptureToken> captured =
            m_inputRouter.CapturePointer(m_panelDragContext, Input::PointerButton::Primary,
                                         const_cast<EditorWorkspaceView&>(*this));
        if (captured.HasError())
        {
            m_panelDragContext.Reset();
            return false;
        }
        m_panelDragCapture = std::move(captured).Value();
        return true;
    }

    bool EditorWorkspaceView::PanelDragEligible() const noexcept
    {
        return m_panelDragCapture.IsActive() && m_inputRouter.IsContextActive(m_panelDragContext);
    }

    void EditorWorkspaceView::OnInputCaptureCancelled(Input::CaptureCancellationReason) noexcept
    {
        m_panelDragCapture.Release();
        m_panelDragContext.Reset();
    }

    void EditorWorkspaceView::Draw(const EditorWorkspaceViewModel& viewModel,
                                   EditorWorkspaceViewCommandData& outCommand,
                                   const GuiContentRegion& contentRegion) const
    {
        if (!m_inputRouter.Snapshot().State(Input::PointerButton::Primary).down)
        {
            m_panelDragCapture.Release();
            m_panelDragContext.Reset();
        }
        const ImVec2 display{contentRegion.width, contentRegion.height};

        const float menuH = UsesNativeEditorMenuBar() ? 0.0F : kMenuBarH;
        constexpr float toolH = kToolbarH;
        // The shell has already removed its persistent status-bar height.
        const float activityBarH = (std::max)(0.0F, display.y - menuH - toolH);

        const bool bottomDockActive =
            viewModel.bottomDockMode == BottomDockMode::Full
                ? !viewModel.activeBottomPanelId.empty()
                : !viewModel.activeBottomLeftPanelId.empty() || !viewModel.activeBottomRightPanelId.empty();
        const float contentH =
            !bottomDockActive
                ? 0.0F
                : (std::max)(0.0F, (std::min)(viewModel.bottomPanelHeight,
                                              (std::max)(0.0F, activityBarH - kMinimumMainH)));

        // Main row height (Hierarchy, Viewport, Inspector)
        const float mainH = (std::max)(0.0F, activityBarH - contentH);

        constexpr float leftActivityW = kActivityBarW;
        constexpr float rightActivityW = kActivityBarW;
        const float availableDockW = (std::max)(0.0F, display.x - leftActivityW - rightActivityW);
        const bool leftDockActive =
            viewModel.leftDockMode == SideDockMode::Full
                ? !viewModel.activeLeftPanelId.empty()
                : !viewModel.activeLeftTopPanelId.empty() || !viewModel.activeLeftBottomPanelId.empty();
        const bool rightDockActive =
            viewModel.rightDockMode == SideDockMode::Full
                ? !viewModel.activeRightPanelId.empty()
                : !viewModel.activeRightTopPanelId.empty() || !viewModel.activeRightBottomPanelId.empty();
        float hierarchyW = leftDockActive ? (std::max)(0.0F, viewModel.leftPanelWidth) : 0.0F;
        float inspectorW = rightDockActive ? (std::max)(0.0F, viewModel.rightPanelWidth) : 0.0F;

        hierarchyW = (std::min)(hierarchyW, (std::max)(0.0F, availableDockW - kMinimumDocumentW));
        inspectorW = (std::min)(inspectorW, (std::max)(0.0F, availableDockW - hierarchyW - kMinimumDocumentW));

        const float centerW = (std::max)(0.0F, availableDockW - hierarchyW - inspectorW);
        const float bottomDockW = availableDockW;

        float curY = 0.0F;

        // ── Menu bar ────────────────────────────────────────────────────
        if (menuH > 0.0F)
        {
            DrawMenuBar(display, viewModel, outCommand);
            if (!m_inputRouter.IsContextActive(m_workspaceInputContext))
                outCommand.menuInvocation.reset();
        }
        curY += menuH;

        // ── Toolbar ─────────────────────────────────────────────────────
        DrawToolbar(ImVec2(0, curY), ImVec2(display.x, toolH), viewModel, outCommand);
        if (!m_inputRouter.IsContextActive(m_workspaceInputContext))
            outCommand.command = EditorWorkspaceViewCommand::None;
        curY += toolH;

        // Resolve splitter ownership before any dock renders its panel-header drag source. The resize seams
        // overlap panel windows by four pixels on each side; without early ownership, the same press can start
        // both a resize and a panel drag/drop operation.
        std::array<WorkspaceSplitterRegion, 3> splitterRegions{};
        std::size_t splitterRegionCount = 0;
        const auto addSplitterRegion = [&splitterRegions, &splitterRegionCount](const WorkspaceSplitterId id,
            const WorkspaceSplitterAxis axis,
            const ImVec2& pos, const ImVec2& size)
        {
            splitterRegions[splitterRegionCount++] = WorkspaceSplitterRegion{
                .id = id, .axis = axis, .minX = pos.x, .minY = pos.y, .maxX = pos.x + size.x, .maxY = pos.y + size.y
            };
        };
        if (hierarchyW > 0.0F)
        {
            addSplitterRegion(WorkspaceSplitterId::Left, WorkspaceSplitterAxis::Horizontal,
                              ImVec2(leftActivityW + hierarchyW - 4.0F, curY), ImVec2(8.0F, mainH));
        }
        if (inspectorW > 0.0F)
        {
            addSplitterRegion(WorkspaceSplitterId::Right, WorkspaceSplitterAxis::Horizontal,
                              ImVec2(display.x - rightActivityW - inspectorW - 4.0F, curY), ImVec2(8.0F, mainH));
        }
        if (contentH > 0.0F)
        {
            addSplitterRegion(WorkspaceSplitterId::Bottom, WorkspaceSplitterAxis::Vertical,
                              ImVec2(leftActivityW, curY + mainH - 4.0F), ImVec2(bottomDockW, 8.0F));
        }

        const Input::RawInputSnapshot& inputSnapshot = m_inputRouter.Snapshot();
        const bool inputBlocked =
            ImGui::GetDragDropPayload() != nullptr || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        const WorkspaceSplitterInteractionResult splitter = m_splitterInteraction.Update(
            std::span<const WorkspaceSplitterRegion>(splitterRegions.data(), splitterRegionCount),
            WorkspaceSplitterPointerInput{
                .x = inputSnapshot.pointer.x,
                .y = inputSnapshot.pointer.y,
                .deltaX = inputSnapshot.pointer.deltaX,
                .deltaY = inputSnapshot.pointer.deltaY,
                .primaryClicked = inputSnapshot.State(Input::PointerButton::Primary).pressed && !inputBlocked,
                .primaryDown = inputSnapshot.State(Input::PointerButton::Primary).down && !inputBlocked
            },
            m_inputRouter, m_workspaceInputContext);
        if (splitter.axis == WorkspaceSplitterAxis::Horizontal)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        else if (splitter.axis == WorkspaceSplitterAxis::Vertical)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }

        // ── Left Activity Bar ───────────────────────────────────────────
        DrawActivityBar(ImVec2(0, curY), ImVec2(leftActivityW, activityBarH), m_panelRegistry, viewModel, outCommand,
                        WorkspaceDockArea::Left, false, !m_splitterInteraction.OwnsPrimaryPointer());

        // ── Middle Row ──────────────────────────────────────────────────
        float curX = leftActivityW;

        // Left Dock
        if (hierarchyW > 0.0f)
        {
            if (viewModel.leftDockMode == SideDockMode::Full)
            {
                DrawDockArea(WorkspaceDockArea::Left, "##DockLeft", ImVec2(curX, curY), ImVec2(hierarchyW, mainH),
                             viewModel.activeLeftPanelId, viewModel, outCommand);
            }
            else
            {
                const float halfHeight = mainH * 0.5F;
                DrawDockArea(WorkspaceDockArea::Left, "##DockLeftTop", ImVec2(curX, curY),
                             ImVec2(hierarchyW, halfHeight),
                             viewModel.activeLeftTopPanelId, viewModel, outCommand);
                DrawDockArea(WorkspaceDockArea::Left, "##DockLeftBottom", ImVec2(curX, curY + halfHeight),
                             ImVec2(hierarchyW, mainH - halfHeight), viewModel.activeLeftBottomPanelId, viewModel,
                             outCommand);
            }
            curX += hierarchyW;
        }

        // Document Dock
        DrawDockArea(WorkspaceDockArea::Document, "##DockDocument", ImVec2(curX, curY), ImVec2(centerW, mainH),
                     viewModel.activeDocumentPanelId, viewModel, outCommand);
        curX += centerW;

        // Right Dock
        if (inspectorW > 0.0f)
        {
            if (viewModel.rightDockMode == SideDockMode::Full)
            {
                DrawDockArea(WorkspaceDockArea::Right, "##DockRight", ImVec2(curX, curY), ImVec2(inspectorW, mainH),
                             viewModel.activeRightPanelId, viewModel, outCommand);
            }
            else
            {
                const float halfHeight = mainH * 0.5F;
                DrawDockArea(WorkspaceDockArea::Right, "##DockRightTop", ImVec2(curX, curY),
                             ImVec2(inspectorW, halfHeight),
                             viewModel.activeRightTopPanelId, viewModel, outCommand);
                DrawDockArea(WorkspaceDockArea::Right, "##DockRightBottom", ImVec2(curX, curY + halfHeight),
                             ImVec2(inspectorW, mainH - halfHeight), viewModel.activeRightBottomPanelId, viewModel,
                             outCommand);
            }
            curX += inspectorW;
        }

        // ── Right Activity Bar ──────────────────────────────────────────
        DrawActivityBar(ImVec2(curX, curY), ImVec2(rightActivityW, activityBarH), m_panelRegistry, viewModel,
                        outCommand,
                        WorkspaceDockArea::Right, true, !m_splitterInteraction.OwnsPrimaryPointer());

        // ── Bottom Dock ─────────────────────────────────────────────────
        if (contentH > 0.0f)
        {
            const ImVec2 bottomPos(leftActivityW, curY + mainH);
            if (viewModel.bottomDockMode == BottomDockMode::Full)
            {
                DrawDockArea(WorkspaceDockArea::Bottom, "##DockBottom", bottomPos, ImVec2(bottomDockW, contentH),
                             viewModel.activeBottomPanelId, viewModel, outCommand);
            }
            else
            {
                const float halfWidth = bottomDockW * 0.5F;
                if (!viewModel.activeBottomLeftPanelId.empty())
                {
                    DrawDockArea(WorkspaceDockArea::Bottom, "##DockBottomLeft", bottomPos, ImVec2(halfWidth, contentH),
                                 viewModel.activeBottomLeftPanelId, viewModel, outCommand);
                }
                if (!viewModel.activeBottomRightPanelId.empty())
                {
                    DrawDockArea(WorkspaceDockArea::Bottom, "##DockBottomRight",
                                 ImVec2(bottomPos.x + halfWidth, bottomPos.y),
                                 ImVec2(bottomDockW - halfWidth, contentH),
                                 viewModel.activeBottomRightPanelId, viewModel, outCommand);
                }
            }
        }

        // Activity Bar icons can be dropped into canonical half-panel allocations. Split seams add
        // narrow merge targets whose hit rectangles preview and activate the full dock allocation.
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            payload != nullptr && std::strcmp(payload->DataType, "HORO_ACTIVITY_BAR_PANEL") == 0 &&
            payload->Data != nullptr && payload->DataSize > 1)
        {
            const std::string_view draggedPanelId(static_cast<const char*>(payload->Data));
            const auto& allPanels = m_panelRegistry.GetAllPanels();
            const bool knownPanel = std::any_of(allPanels.begin(), allPanels.end(),
                                                [draggedPanelId](const std::shared_ptr<IWorkspacePanel>& panel)
                                                {
                                                    return panel->GetId() == draggedPanelId;
                                                });

            if (knownPanel)
            {
                const float retainedBottomH = (std::min)((std::max)(0.0F, viewModel.bottomPanelHeight),
                                                         (std::max)(0.0F, activityBarH - kMinimumMainH));
                const float retainedMainH = (std::max)(0.0F, activityBarH - retainedBottomH);
                const float retainedLeftW = (std::min)((std::max)(0.0F, viewModel.leftPanelWidth),
                                                       (std::max)(0.0F, availableDockW - kMinimumDocumentW));
                const float retainedRightW =
                    (std::min)((std::max)(0.0F, viewModel.rightPanelWidth),
                               (std::max)(0.0F, availableDockW - retainedLeftW - kMinimumDocumentW));
                const float retainedDocumentW = (std::max)(0.0F, availableDockW - retainedLeftW - retainedRightW);

                struct AllocationTarget
                {
                    const char* windowId;
                    WorkspaceDockArea area;
                    ActivityBarSlot appendSlot;
                    std::optional<BottomDockSlot> bottomSlot;
                    std::optional<SideDockSlot> sideSlot;
                    ImVec2 hitPos;
                    ImVec2 hitSize;
                    ImVec2 previewPos;
                    ImVec2 previewSize;
                    bool preserveActivitySlotWithinArea = false;
                };
                const auto appendIndex = [&viewModel](const ActivityBarRail rail, const std::size_t groupIndex)
                {
                    return viewModel.activityBarLayout.Groups(rail)[groupIndex].items.size();
                };
                const float retainedBottomHalfW = availableDockW * 0.5F;
                const float retainedMainHalfH = retainedMainH * 0.5F;
                const float retainedRightX = display.x - rightActivityW - retainedRightW;
                constexpr float mergeHalfSpan = 8.0F;
                const float actualRightX = display.x - rightActivityW - inspectorW;
                const auto slotBelongsToArea = [](const ActivityBarSlot& slot, const WorkspaceDockArea area)
                {
                    switch (area)
                    {
                    case WorkspaceDockArea::Left:
                        return slot.rail == ActivityBarRail::Left && slot.groupIndex < 2;
                    case WorkspaceDockArea::Right:
                        return slot.rail == ActivityBarRail::Right && slot.groupIndex < 2;
                    case WorkspaceDockArea::Bottom:
                        return (slot.rail == ActivityBarRail::Left || slot.rail == ActivityBarRail::Right) &&
                            slot.groupIndex == 2;
                    case WorkspaceDockArea::Document:
                        return slot.rail == ActivityBarRail::DocumentTop && slot.groupIndex == 0;
                    }
                    return false;
                };
                const std::array<AllocationTarget, 10> targets = {
                    AllocationTarget{
                        "##ActivityPanelPreviewLeftTop", WorkspaceDockArea::Left,
                        ActivityBarSlot{ActivityBarRail::Left, 0, appendIndex(ActivityBarRail::Left, 0)},
                        std::nullopt, SideDockSlot::Top, ImVec2(leftActivityW, curY),
                        ImVec2(retainedLeftW, retainedMainHalfH), ImVec2(leftActivityW, curY),
                        ImVec2(retainedLeftW, retainedMainHalfH)
                    },
                    AllocationTarget{
                        "##ActivityPanelPreviewLeftBottom", WorkspaceDockArea::Left,
                        ActivityBarSlot{ActivityBarRail::Left, 1, appendIndex(ActivityBarRail::Left, 1)},
                        std::nullopt, SideDockSlot::Bottom, ImVec2(leftActivityW, curY + retainedMainHalfH),
                        ImVec2(retainedLeftW, retainedMainH - retainedMainHalfH),
                        ImVec2(leftActivityW, curY + retainedMainHalfH),
                        ImVec2(retainedLeftW, retainedMainH - retainedMainHalfH)
                    },
                    AllocationTarget{
                        "##ActivityPanelPreviewDocument", WorkspaceDockArea::Document,
                        ActivityBarSlot{ActivityBarRail::DocumentTop, 0, appendIndex(ActivityBarRail::DocumentTop, 0)},
                        std::nullopt, std::nullopt, ImVec2(leftActivityW + retainedLeftW, curY),
                        ImVec2(retainedDocumentW, retainedMainH), ImVec2(leftActivityW + retainedLeftW, curY),
                        ImVec2(retainedDocumentW, retainedMainH)
                    },
                    AllocationTarget{
                        "##ActivityPanelPreviewRightTop", WorkspaceDockArea::Right,
                        ActivityBarSlot{ActivityBarRail::Right, 0, appendIndex(ActivityBarRail::Right, 0)},
                        std::nullopt, SideDockSlot::Top, ImVec2(retainedRightX, curY),
                        ImVec2(retainedRightW, retainedMainHalfH), ImVec2(retainedRightX, curY),
                        ImVec2(retainedRightW, retainedMainHalfH)
                    },
                    AllocationTarget{
                        "##ActivityPanelPreviewRightBottom", WorkspaceDockArea::Right,
                        ActivityBarSlot{ActivityBarRail::Right, 1, appendIndex(ActivityBarRail::Right, 1)},
                        std::nullopt, SideDockSlot::Bottom, ImVec2(retainedRightX, curY + retainedMainHalfH),
                        ImVec2(retainedRightW, retainedMainH - retainedMainHalfH),
                        ImVec2(retainedRightX, curY + retainedMainHalfH),
                        ImVec2(retainedRightW, retainedMainH - retainedMainHalfH)
                    },
                    AllocationTarget{
                        "##ActivityPanelPreviewBottomLeft", WorkspaceDockArea::Bottom,
                        ActivityBarSlot{ActivityBarRail::Left, 2, appendIndex(ActivityBarRail::Left, 2)},
                        BottomDockSlot::Left, std::nullopt, ImVec2(leftActivityW, curY + retainedMainH),
                        ImVec2(retainedBottomHalfW, retainedBottomH),
                        ImVec2(leftActivityW, curY + retainedMainH),
                        ImVec2(retainedBottomHalfW, retainedBottomH)
                    },
                    AllocationTarget{
                        "##ActivityPanelPreviewBottomRight", WorkspaceDockArea::Bottom,
                        ActivityBarSlot{ActivityBarRail::Right, 2, appendIndex(ActivityBarRail::Right, 2)},
                        BottomDockSlot::Right, std::nullopt,
                        ImVec2(leftActivityW + retainedBottomHalfW, curY + retainedMainH),
                        ImVec2(availableDockW - retainedBottomHalfW, retainedBottomH),
                        ImVec2(leftActivityW + retainedBottomHalfW, curY + retainedMainH),
                        ImVec2(availableDockW - retainedBottomHalfW, retainedBottomH)
                    },
                    AllocationTarget{
                        "##ActivityPanelMergeLeft", WorkspaceDockArea::Left,
                        ActivityBarSlot{ActivityBarRail::Left, 0, appendIndex(ActivityBarRail::Left, 0)},
                        std::nullopt, std::nullopt, ImVec2(leftActivityW, curY + mainH * 0.5F - mergeHalfSpan),
                        ImVec2(hierarchyW, mergeHalfSpan * 2.0F), ImVec2(leftActivityW, curY),
                        ImVec2(hierarchyW, mainH), true
                    },
                    AllocationTarget{
                        "##ActivityPanelMergeRight", WorkspaceDockArea::Right,
                        ActivityBarSlot{ActivityBarRail::Right, 0, appendIndex(ActivityBarRail::Right, 0)},
                        std::nullopt, std::nullopt, ImVec2(actualRightX, curY + mainH * 0.5F - mergeHalfSpan),
                        ImVec2(inspectorW, mergeHalfSpan * 2.0F), ImVec2(actualRightX, curY),
                        ImVec2(inspectorW, mainH), true
                    },
                    AllocationTarget{
                        "##ActivityPanelMergeBottom", WorkspaceDockArea::Bottom,
                        ActivityBarSlot{ActivityBarRail::Left, 2, appendIndex(ActivityBarRail::Left, 2)},
                        std::nullopt, std::nullopt,
                        ImVec2(leftActivityW + bottomDockW * 0.5F - mergeHalfSpan, curY + mainH),
                        ImVec2(mergeHalfSpan * 2.0F, contentH), ImVec2(leftActivityW, curY + mainH),
                        ImVec2(bottomDockW, contentH), true
                    }
                };

                for (const AllocationTarget& target : targets)
                {
                    if (target.hitSize.x <= 0.0F || target.hitSize.y <= 0.0F)
                    {
                        continue;
                    }
                    ImGui::SetNextWindowPos(target.hitPos);
                    ImGui::SetNextWindowSize(target.hitSize);
                    ImGui::SetNextWindowBgAlpha(0.0F);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
                    ImGui::Begin(target.windowId, nullptr,
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavInputs |
                                 ImGuiWindowFlags_NoNavFocus);
                    ImGui::SetCursorPos(ImVec2(0.0F, 0.0F));
                    ImGui::InvisibleButton("##ActivityPanelAllocationTarget", target.hitSize);
                    const bool previewHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                    if (previewHovered)
                    {
                        ImDrawList* previewDrawList = ImGui::GetWindowDrawList();
                        const ImVec2 previewMax(target.previewPos.x + target.previewSize.x,
                                                target.previewPos.y + target.previewSize.y);
                        previewDrawList->PushClipRectFullScreen();
                        previewDrawList->AddRectFilled(target.previewPos, previewMax, Theme::U32(Theme::AccentSoft()),
                                                       4.0F);
                        previewDrawList->AddRect(ImVec2(target.previewPos.x + 0.5F, target.previewPos.y + 0.5F),
                                                 ImVec2(previewMax.x - 0.5F, previewMax.y - 0.5F),
                                                 Theme::U32(Theme::Accent()), 4.0F, 0, 1.0F);
                        previewDrawList->PopClipRect();
                    }
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* acceptedPayload = ImGui::AcceptDragDropPayload(
                                "HORO_ACTIVITY_BAR_PANEL", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                            acceptedPayload != nullptr && PanelDragEligible())
                        {
                            outCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
                            outCommand.targetIndex = static_cast<int>(target.area);
                            outCommand.stringPayload = static_cast<const char*>(acceptedPayload->Data);
                            const std::string_view acceptedPanelId(static_cast<const char*>(acceptedPayload->Data));
                            const std::optional<ActivityBarSlot> currentSlot =
                                viewModel.activityBarLayout.FindSlot(acceptedPanelId);
                            if (!target.preserveActivitySlotWithinArea || !currentSlot.has_value() ||
                                !slotBelongsToArea(*currentSlot, target.area))
                            {
                                outCommand.activityBarSlot = target.appendSlot;
                            }
                            outCommand.bottomDockSlot = target.bottomSlot;
                            outCommand.sideDockSlot = target.sideSlot;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::End();
                    ImGui::PopStyleVar(3);
                }
            }
        }

        if (splitter.active == WorkspaceSplitterId::Left)
        {
            if (const float d = splitter.delta; d != 0.0F)
            {
                outCommand.command = EditorWorkspaceViewCommand::ResizePanel;
                outCommand.targetIndex = 0;
                outCommand.floatPayload =
                    std::max(100.0f, std::min(display.x - leftActivityW - rightActivityW - inspectorW - 100.0f,
                                              viewModel.leftPanelWidth + d));
                outCommand.layoutPayload = WorkspaceLayoutSize{
                    .leftWidth = *outCommand.floatPayload,
                    .leftHeight = mainH,
                    .rightWidth = inspectorW,
                    .rightHeight = mainH,
                    .bottomWidth = bottomDockW,
                    .bottomHeight = contentH,
                    .documentWidth = display.x - leftActivityW - *outCommand.floatPayload - inspectorW - rightActivityW,
                    .documentHeight = mainH
                };
            }
        }

        else if (splitter.active == WorkspaceSplitterId::Right)
        {
            if (const float d = splitter.delta; d != 0.0F)
            {
                outCommand.command = EditorWorkspaceViewCommand::ResizePanel;
                outCommand.targetIndex = 1;
                outCommand.floatPayload =
                    std::max(100.0f, std::min(display.x - leftActivityW - rightActivityW - hierarchyW - 100.0f,
                                              viewModel.rightPanelWidth - d));
                outCommand.layoutPayload = WorkspaceLayoutSize{
                    .leftWidth = hierarchyW,
                    .leftHeight = mainH,
                    .rightWidth = *outCommand.floatPayload,
                    .rightHeight = mainH,
                    .bottomWidth = bottomDockW,
                    .bottomHeight = contentH,
                    .documentWidth = display.x - leftActivityW - hierarchyW -
                    *outCommand.floatPayload - rightActivityW,
                    .documentHeight = mainH
                };
            }
        }

        else if (splitter.active == WorkspaceSplitterId::Bottom)
        {
            if (const float d = splitter.delta; d != 0.0F)
            {
                outCommand.command = EditorWorkspaceViewCommand::ResizePanel;
                outCommand.targetIndex = 2;
                outCommand.floatPayload =
                    std::max(100.0f, std::min(activityBarH - 100.0f, viewModel.bottomPanelHeight - d));
                const float newMainH = activityBarH - *outCommand.floatPayload;
                outCommand.layoutPayload = WorkspaceLayoutSize{
                    .leftWidth = hierarchyW,
                    .leftHeight = newMainH,
                    .rightWidth = inspectorW,
                    .rightHeight = newMainH,
                    .bottomWidth = bottomDockW,
                    .bottomHeight = *outCommand.floatPayload,
                    .documentWidth = centerW,
                    .documentHeight = newMainH
                };
            }
        }

        curY += activityBarH;
    }

    namespace
    {
        [[nodiscard]] bool IsFallbackMenuItemEnabled(const EditorMenuItem& item,
                                                     const EditorWorkspaceViewModel& viewModel)
        {
            if (!item.enabledByDefault)
            {
                return false;
            }
            if (item.action == EditorMenuAction::SaveScene)
            {
                return viewModel.isDirty;
            }
            if (item.action == EditorMenuAction::Undo)
            {
                return viewModel.canUndo;
            }
            if (item.action == EditorMenuAction::Redo)
            {
                return viewModel.canRedo;
            }
            return true;
        }

        void DrawFallbackMenuChildren(const EditorMenuItem& parent, const EditorWorkspaceViewModel& viewModel,
                                      EditorWorkspaceViewCommandData& outCommand, const EditorGuiContext& context)
        {
            for (const EditorMenuItem& item : parent.children)
            {
                if (item.kind == EditorMenuItemKind::Separator)
                {
                    ImGui::Separator();
                    continue;
                }

                const std::string& label = context.localization.Get("editor", item.labelKey);
                if (item.kind == EditorMenuItemKind::Submenu)
                {
                    if (ImGui::BeginMenu(label.c_str()))
                    {
                        DrawFallbackMenuChildren(item, viewModel, outCommand, context);
                        ImGui::EndMenu();
                    }
                    continue;
                }

                const bool enabled = IsFallbackMenuItemEnabled(item, viewModel);
                if (const char* shortcut = item.shortcut.empty() ? nullptr : item.shortcut.data();
                    ImGui::MenuItem(label.c_str(), shortcut, false, enabled))
                {
                    outCommand.menuInvocation = EditorMenuInvocation{item.action, item.primitive};
                }
            }
        }
    } // namespace

    void EditorWorkspaceView::DrawMenuBar(const ImVec2& display, const EditorWorkspaceViewModel& viewModel,
                                          EditorWorkspaceViewCommandData& outCommand) const
    {
        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(display.x, kMenuBarH));
        ImGui::SetNextWindowBgAlpha(1.0F);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg0());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::Bg2());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 0.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0F, 5.0F));

        ImGui::Begin("##MenuBar", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar);

        if (ImGui::BeginMenuBar())
        {
            constexpr ImVec2 logoSize(22.0F, 22.0F);
            const ImVec2 logoMin = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##HoroAppLogo", logoSize))
            {
                outCommand.menuInvocation = EditorMenuInvocation{EditorMenuAction::OpenProject, std::nullopt};
            }
            if (m_logoTexture != 0)
            {
                ImGui::GetWindowDrawList()->AddImage(m_logoTexture, logoMin,
                                                     ImVec2(logoMin.x + logoSize.x, logoMin.y + logoSize.y));
            }
            else
            {
                ImGui::GetWindowDrawList()->AddText(logoMin, Theme::U32(Theme::Accent()), "HORO");
            }
            ImGui::SameLine(0.0F, 10.0F);

            for (const EditorMenuItem& menu : GetEditorMenuModel().menus)
            {
                const std::string& label = m_context.localization.Get("editor", menu.labelKey);
                if (ImGui::BeginMenu(label.c_str()))
                {
                    DrawFallbackMenuChildren(menu, viewModel, outCommand, m_context);
                    ImGui::EndMenu();
                }
            }

            const std::string version = std::format("Horo Engine {}", HORO_ENGINE_VERSION_STRING);
            const float versionWidth = ImGui::CalcTextSize(version.c_str()).x;
            const float versionX = display.x - versionWidth - 12.0F;
            if (ImGui::GetCursorPosX() + 12.0F < versionX)
            {
                ImGui::SetCursorPosX(versionX);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                ImGui::TextUnformatted(version.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::EndMenuBar();
        }

        const ImVec2 windowPos = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(windowPos.x, windowPos.y + kMenuBarH - 1.0F),
                                            ImVec2(windowPos.x + display.x, windowPos.y + kMenuBarH - 1.0F),
                                            Theme::U32(Theme::Border()));
        ImGui::End();
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(3);
    }

    namespace
    {
        void DrawSelectIcon(ImDrawList* dl, const float x, const float y, const float w, const float h, const ImU32 col)
        {
            const float ox = x + (w - 14.0f) * 0.5f;
            const float oy = y + (h - 14.0f) * 0.5f;
            const std::array<ImVec2, 4> pts = {
                ImVec2(ox + 2.5f, oy + 2.5f), ImVec2(ox + 6.3f, oy + 12.0f),
                ImVec2(ox + 8.0f, oy + 8.0f), ImVec2(ox + 12.2f, oy + 6.2f)
            };
            dl->AddPolyline(pts.data(), pts.size(), col, ImDrawFlags_Closed, 1.5f);
        }

        void DrawMoveIcon(ImDrawList* dl, const float x, const float y, const float w, const float h, const ImU32 col)
        {
            const float ox = x + (w - 14.0f) * 0.5f;
            const float oy = y + (h - 14.0f) * 0.5f;
            dl->AddLine(ImVec2(ox + 7, oy + 2), ImVec2(ox + 7, oy + 12), col, 1.4f);
            dl->AddLine(ImVec2(ox + 2, oy + 7), ImVec2(ox + 12, oy + 7), col, 1.4f);
            const std::array<ImVec2, 3> p1 = {ImVec2(ox + 5, oy + 4), ImVec2(ox + 7, oy + 2), ImVec2(ox + 9, oy + 4)};
            dl->AddPolyline(p1.data(), p1.size(), col, 0, 1.4f);
            const std::array<ImVec2, 3> p2 = {
                ImVec2(ox + 5, oy + 10), ImVec2(ox + 7, oy + 12), ImVec2(ox + 9, oy + 10)
            };
            dl->AddPolyline(p2.data(), p2.size(), col, 0, 1.4f);
            const std::array<ImVec2, 3> p3 = {ImVec2(ox + 4, oy + 5), ImVec2(ox + 2, oy + 7), ImVec2(ox + 4, oy + 9)};
            dl->AddPolyline(p3.data(), p3.size(), col, 0, 1.4f);
            const std::array<ImVec2, 3> p4 = {
                ImVec2(ox + 10, oy + 5), ImVec2(ox + 12, oy + 7), ImVec2(ox + 10, oy + 9)
            };
            dl->AddPolyline(p4.data(), p4.size(), col, 0, 1.4f);
        }

        void DrawRotateIcon(ImDrawList* dl, const float x, const float y, const float w, const float h, const ImU32 col)
        {
            const float ox = x + (w - 14.0f) * 0.5f;
            const float oy = y + (h - 14.0f) * 0.5f;
            dl->PathArcTo(ImVec2(ox + 7, oy + 7), 4.0f, std::numbers::pi_v<float> * 1.5f,
                          std::numbers::pi_v<float> * -0.2f);
            dl->PathStroke(col, 0, 1.4f);
            const std::array<ImVec2, 3> p1 = {
                ImVec2(ox + 12, oy + 1.5f), ImVec2(ox + 11, oy + 4.1f),
                ImVec2(ox + 8.4f, oy + 3)
            };
            dl->AddPolyline(p1.data(), p1.size(), col, 0, 1.4f);
        }

        void DrawScaleIcon(ImDrawList* dl, const float x, const float y, const float w, const float h, const ImU32 col)
        {
            const float ox = x + (w - 14.0f) * 0.5f;
            const float oy = y + (h - 14.0f) * 0.5f;
            dl->AddRect(ImVec2(ox + 4.5f, oy + 4.5f), ImVec2(ox + 9.5f, oy + 9.5f), col, 0.5f, 0, 1.4f);
            dl->AddLine(ImVec2(ox + 9.5f, oy + 4.5f), ImVec2(ox + 12, oy + 2), col, 1.4f);
            dl->AddLine(ImVec2(ox + 9.5f, oy + 9.5f), ImVec2(ox + 12, oy + 12), col, 1.4f);
            dl->AddLine(ImVec2(ox + 4.5f, oy + 9.5f), ImVec2(ox + 2, oy + 12), col, 1.4f);
            dl->AddLine(ImVec2(ox + 4.5f, oy + 4.5f), ImVec2(ox + 2, oy + 2), col, 1.4f);
        }

        void DrawLocalIcon(ImDrawList* dl, ImFont* font, const float x, const float y, const float w, const float h,
                           const ImU32 col)
        {
            const ImVec2 ts = ImGui::CalcTextSize("L");
            dl->AddText(font, font->FontSize, ImVec2(x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f), col, "L");
        }

        void DrawWorldIcon(ImDrawList* dl, ImFont* font, const float x, const float y, const float w, const float h)
        {
            const ImVec2 ts = ImGui::CalcTextSize("W");
            dl->AddText(font, font->FontSize, ImVec2(x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f),
                        Theme::U32(Theme::Dim()),
                        "W");
        }

        void DrawViewModeIcon(ImDrawList* dl, ImFont* font, const float x, const float y, const float w, const float h,
                              const ImU32 col)
        {
            const ImVec2 ts = ImGui::CalcTextSize("Scene");
            dl->AddText(font, font->FontSize, ImVec2(x + 10.0f, y + (h - ts.y) * 0.5f), col, "Scene");
            const float ax = x + w - 14.0f;
            const float ay = y + h * 0.5f;
            dl->AddTriangleFilled(ImVec2(ax - 3, ay - 1.5f), ImVec2(ax + 3, ay - 1.5f), ImVec2(ax, ay + 2.5f), col);
        }

        void DrawSettingsIcon(ImDrawList* dl, const float x, const float y, const float w, const float h,
                              const ImU32 col)
        {
            const float ox = x + (w - 14.0f) * 0.5f;
            const float oy = y + (h - 14.0f) * 0.5f;
            dl->AddCircle(ImVec2(ox + 7, oy + 7), 1.8f, col, 12, 1.35f);
            dl->AddLine(ImVec2(ox + 7, oy + 1.5f), ImVec2(ox + 7, oy + 2.7f), col, 1.35f);
            dl->AddLine(ImVec2(ox + 7, oy + 11.3f), ImVec2(ox + 7, oy + 12.5f), col, 1.35f);
            dl->AddLine(ImVec2(ox + 1.5f, oy + 7), ImVec2(ox + 2.7f, oy + 7), col, 1.35f);
            dl->AddLine(ImVec2(ox + 11.3f, oy + 7), ImVec2(ox + 12.5f, oy + 7), col, 1.35f);
            dl->AddLine(ImVec2(ox + 3.1f, oy + 3.1f), ImVec2(ox + 3.95f, oy + 3.95f), col, 1.35f);
            dl->AddLine(ImVec2(ox + 10.05f, oy + 10.05f), ImVec2(ox + 10.9f, oy + 10.9f), col, 1.35f);
            dl->AddLine(ImVec2(ox + 10.9f, oy + 3.1f), ImVec2(ox + 10.05f, oy + 3.95f), col, 1.35f);
            dl->AddLine(ImVec2(ox + 3.95f, oy + 10.05f), ImVec2(ox + 3.1f, oy + 10.9f), col, 1.35f);
        }

        void DrawHelpIcon(ImDrawList* dl, const float x, const float y, const float w, const float h, const ImU32 col)
        {
            const float ox = x + (w - 14.0f) * 0.5f;
            const float oy = y + (h - 14.0f) * 0.5f;
            dl->AddCircle(ImVec2(ox + 7, oy + 7), 5.5f, col, 24, 1.4f);
            dl->PathArcTo(ImVec2(ox + 7, oy + 5.5f), 1.5f, std::numbers::pi_v<float>, 0);
            dl->PathLineTo(ImVec2(ox + 7, oy + 8.5f));
            dl->PathStroke(col, 0, 1.4f);
            dl->AddCircleFilled(ImVec2(ox + 7, oy + 10.5f), 0.8f, col);
        }

        template <typename DrawIconFunc>
        bool DrawToolButton(ImDrawList* dl, const float centerY, float& curX, const char* id, float width,
                            const bool active,
                            DrawIconFunc drawIcon)
        {
            const float h = 26.0f;
            const float y = centerY - h * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(curX, y));
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, h));
            const bool hovered = ImGui::IsItemHovered();
            if (active)
            {
                dl->AddRectFilled(ImVec2(curX + 2.0f, y + 2.0f), ImVec2(curX + width - 2.0f, y + h - 2.0f),
                                  Theme::U32(Theme::Bg3()), 3.0f);
            }
            else if (hovered)
            {
                dl->AddRectFilled(ImVec2(curX + 2.0f, y + 2.0f), ImVec2(curX + width - 2.0f, y + h - 2.0f),
                                  Theme::U32(Theme::Hover()), 3.0f);
            }
            drawIcon(curX, y, width, h, Theme::U32(active || hovered ? Theme::Text() : Theme::Muted()));
            curX += width;
            return clicked;
        }
    } // namespace

    void EditorWorkspaceView::DrawToolbar(const ImVec2& pos, const ImVec2& size,
                                          const EditorWorkspaceViewModel& viewModel,
                                          EditorWorkspaceViewCommandData& outCommand) const
    {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowBgAlpha(1.0F);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg1());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 0.0F));

        ImGui::Begin("##Toolbar", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        float curX = pos.x + 10.0f;
        const float centerY = pos.y + size.y * 0.5f;

        auto drawToolGroupBg = [dl, centerY, &curX](const float width)
        {
            constexpr float h = 26.0f;
            const float y = centerY - h * 0.5f;
            dl->AddRectFilled(ImVec2(curX, y), ImVec2(curX + width, y + h), Theme::U32(Theme::Bg0()), 4.0f);
            dl->AddRect(ImVec2(curX, y), ImVec2(curX + width, y + h), Theme::U32(Theme::Border()), 4.0f);
            return y;
        };

        // Transform tools
        drawToolGroupBg(28.0f * 4);
        if (DrawToolButton(dl, centerY, curX, "##Select", 28.0f,
                           viewModel.activeTransformTool == EditorTransformTool::Select,
                           [dl](const float x, const float y, const float w, const float h, const ImU32 col)
                           {
                               DrawSelectIcon(dl, x, y, w, h, col);
                           }))
        {
            outCommand.command = EditorWorkspaceViewCommand::ChangeTransformTool;
            outCommand.transformToolPayload = EditorTransformTool::Select;
        }
        if (DrawToolButton(dl, centerY, curX, "##Move", 28.0f,
                           viewModel.activeTransformTool == EditorTransformTool::Move,
                           [dl](const float x, const float y, const float w, const float h, const ImU32 col)
                           {
                               DrawMoveIcon(dl, x, y, w, h, col);
                           }))
        {
            outCommand.command = EditorWorkspaceViewCommand::ChangeTransformTool;
            outCommand.transformToolPayload = EditorTransformTool::Move;
        }
        if (DrawToolButton(dl, centerY, curX, "##Rotate", 28.0f,
                           viewModel.activeTransformTool == EditorTransformTool::Rotate,
                           [dl](const float x, const float y, const float w, const float h, const ImU32 col)
                           {
                               DrawRotateIcon(dl, x, y, w, h, col);
                           }))
        {
            outCommand.command = EditorWorkspaceViewCommand::ChangeTransformTool;
            outCommand.transformToolPayload = EditorTransformTool::Rotate;
        }
        if (DrawToolButton(dl, centerY, curX, "##Scale", 28.0f,
                           viewModel.activeTransformTool == EditorTransformTool::Scale,
                           [dl](const float x, const float y, const float w, const float h, const ImU32 col)
                           {
                               DrawScaleIcon(dl, x, y, w, h, col);
                           }))
        {
            outCommand.command = EditorWorkspaceViewCommand::ChangeTransformTool;
            outCommand.transformToolPayload = EditorTransformTool::Scale;
        }

        curX += 16.0f; // Gap

        // Space
        drawToolGroupBg(28.0f * 2);
        if (DrawToolButton(dl, centerY, curX, "##Local", 28.0f,
                           viewModel.activeTransformSpace == EditorTransformSpace::Local,
                           [dl, this](const float x, const float y, const float w, const float h, const ImU32 col)
                           {
                               DrawLocalIcon(dl, m_context.theme.fonts.sans, x, y, w, h, col);
                           }))
        {
            outCommand.command = EditorWorkspaceViewCommand::ChangeTransformSpace;
            outCommand.transformSpacePayload = EditorTransformSpace::Local;
        }
        if (DrawToolButton(dl, centerY, curX, "##World", 28.0f,
                           viewModel.activeTransformSpace == EditorTransformSpace::World,
                           [dl, this](const float x, const float y, const float w, const float h,
                                      [[maybe_unused]] ImU32 col)
                           {
                               DrawWorldIcon(dl, m_context.theme.fonts.sans, x, y, w, h);
                           }))
        {
            outCommand.command = EditorWorkspaceViewCommand::ChangeTransformSpace;
            outCommand.transformSpacePayload = EditorTransformSpace::World;
        }

        curX += 16.0f;

        // View mode
        constexpr float viewModeW = 80.0f;
        drawToolGroupBg(viewModeW);
        DrawToolButton(dl, centerY, curX, "##ViewMode", viewModeW, false,
                       [dl, this](const float x, const float y, const float w, const float h, const ImU32 col)
                       {
                           DrawViewModeIcon(dl, m_context.theme.fonts.sans, x, y, w, h, col);
                       });

        // The remaining strip is the document activity/file-tab rail. Play is the
        // left-most member of the right-aligned controls.
        constexpr float playW = 70.0f;
        constexpr float utilW = 28.0f * 2;
        const float utilX = pos.x + size.x - utilW - 10.0F;
        const float playX = utilX - playW - 8.0F;
        const float documentRailMinX = curX + 8.0F;
        const float documentRailMaxX = playX - 8.0F;

        if (documentRailMaxX > documentRailMinX)
        {
            ImGui::PushClipRect(ImVec2(documentRailMinX, pos.y), ImVec2(documentRailMaxX, pos.y + size.y), true);
            float tabX = documentRailMinX;
            const auto& documentGroups = viewModel.activityBarLayout.Groups(ActivityBarRail::DocumentTop);
            if (!documentGroups.empty())
            {
                for (const std::string& panelId : documentGroups.front().items)
                {
                    const auto panelIt = std::find_if(
                        m_panelRegistry.GetAllPanels().begin(), m_panelRegistry.GetAllPanels().end(),
                        [&panelId](const std::shared_ptr<IWorkspacePanel>& panel)
                        {
                            return panel->GetId() == panelId;
                        });
                    if (panelIt == m_panelRegistry.GetAllPanels().end())
                    {
                        continue;
                    }

                    constexpr float tabW = 32.0F;
                    constexpr float tabH = 26.0F;
                    const float tabY = centerY - tabH * 0.5F;
                    ImGui::SetCursorScreenPos(ImVec2(tabX, tabY));
                    ImGui::PushID(panelId.c_str());
                    if (ImGui::InvisibleButton("##DocumentActivityItem", ImVec2(tabW, tabH)))
                    {
                        outCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
                        outCommand.targetIndex = static_cast<int>(WorkspaceDockArea::Document);
                        outCommand.stringPayload = panelId;
                    }
                    if (!m_splitterInteraction.OwnsPrimaryPointer() &&
                        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        if (EnsurePanelDragCapture())
                        {
                            ImGui::SetDragDropPayload("HORO_ACTIVITY_BAR_PANEL", panelId.c_str(), panelId.size() + 1);
                            ImGui::TextUnformatted((*panelIt)->GetDisplayName().c_str());
                        }
                        ImGui::EndDragDropSource();
                    }
                    const bool active = panelId == viewModel.activeDocumentPanelId;
                    const ImVec2 itemMin(tabX, tabY);
                    const ImVec2 itemMax(tabX + tabW, tabY + tabH);
                    if (active || ImGui::IsItemHovered())
                    {
                        dl->AddRectFilled(itemMin, itemMax, Theme::U32(active ? Theme::Bg3() : Theme::Hover()), 3.0F);
                    }
                    dl->AddRect(itemMin, itemMax, Theme::U32(Theme::Border()), 3.0F);
                    (*panelIt)->DrawIcon(dl, itemMin, ImVec2(tabW, tabH),
                                         Theme::U32(active ? Theme::Text() : Theme::Dim()));
                    ImGui::PopID();
                    tabX += tabW + 2.0F;
                }
            }
            ImGui::PopClipRect();
        }

        // Play Button
        curX = playX;

        const float py = centerY - 13.0f;
        ImGui::SetCursorScreenPos(ImVec2(curX, py));
        if (ImGui::InvisibleButton("##Play", ImVec2(playW, 26.0f)))
        {
            // TODO: Handle play clicked
        }
        const bool playHovered = ImGui::IsItemHovered();
        dl->AddRectFilled(ImVec2(curX, py), ImVec2(curX + playW, py + 26.0f), Theme::U32(Theme::Bg0()), 4.0f);
        dl->AddRect(ImVec2(curX, py), ImVec2(curX + playW, py + 26.0f), Theme::U32(Theme::Border()), 4.0f);
        if (playHovered)
            dl->AddRectFilled(ImVec2(curX + 2, py + 2), ImVec2(curX + playW - 2, py + 24.0f),
                              Theme::U32(Theme::Hover()),
                              3.0f);

        const ImU32 playCol = Theme::U32(Theme::Ok());
        const float tox = curX + 10.0f;
        const float toy = py + 8.0f;
        dl->AddTriangleFilled(ImVec2(tox, toy), ImVec2(tox + 8.5f, toy + 4.5f), ImVec2(tox, toy + 9.0f), playCol);
        dl->AddText(m_context.theme.fonts.sans, m_context.theme.fonts.sans->FontSize, ImVec2(curX + 24.0f, py + 5.0f),
                    playCol, "Play");

        // Utility group (Right aligned)
        curX = utilX;
        drawToolGroupBg(utilW);
        DrawToolButton(dl, centerY, curX, "##Settings", 28.0f, false,
                       [dl](const float x, const float y, const float w, const float h, const ImU32 col)
                       {
                           DrawSettingsIcon(dl, x, y, w, h, col);
                       });
        DrawToolButton(dl, centerY, curX, "##Help", 28.0f, false,
                       [dl](const float x, const float y, const float w, const float h, const ImU32 col)
                       {
                           DrawHelpIcon(dl, x, y, w, h, col);
                       });

        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    void EditorWorkspaceView::DrawDockArea(const WorkspaceDockArea area, const char* windowId, const ImVec2& pos,
                                           const ImVec2& size, const std::string_view activePanelId,
                                           const EditorWorkspaceViewModel& viewModel,
                                           EditorWorkspaceViewCommandData& outCommand) const
    {
        // A screen transition, minimize, or sufficiently narrow host window can temporarily leave a dock with no
        // drawable area. InvisibleButton requires both dimensions to be non-zero, so defer the dock until layout
        // produces a usable rectangle on a later frame.
        if (!(size.x > 0.0F) || !(size.y > 0.0F))
        {
            return;
        }

        const auto& panels = m_panelRegistry.GetAllPanels();

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowBgAlpha(1.0F);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg1());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

        ImGui::Begin(windowId, nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

        std::shared_ptr<IWorkspacePanel> activePanel = nullptr;
        for (const auto& p : panels)
        {
            if (p->GetId() == activePanelId)
            {
                activePanel = p;
                break;
            }
        }

        if (!activePanel)
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);
            return;
        }

        constexpr float paneChromeHeight = 28.0F;
        ImDrawList* paneDrawList = ImGui::GetWindowDrawList();
        paneDrawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + paneChromeHeight), Theme::U32(Theme::Bg0()));
        paneDrawList->AddLine(ImVec2(pos.x, pos.y + paneChromeHeight - 1.0F),
                              ImVec2(pos.x + size.x, pos.y + paneChromeHeight - 1.0F), Theme::U32(Theme::Border()),
                              1.0F);

        const char* targetNodeId = area == WorkspaceDockArea::Left
                                       ? "workspace.left"
                                       : area == WorkspaceDockArea::Right
                                       ? "workspace.right"
                                       : "workspace.document";
        auto drawWorkspaceDropTarget = [&](const char* id, const ImVec2 targetPos, const ImVec2 targetSize,
                                           const WorkspacePanelHost::DropKind kind)
        {
            ImGui::SetCursorScreenPos(targetPos);
            ImGui::PushID(id);
            ImGui::InvisibleButton("##WorkspaceDropTarget", targetSize);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(targetPos,
                                                          ImVec2(targetPos.x + targetSize.x,
                                                                 targetPos.y + targetSize.y),
                                                          Theme::U32(Theme::AccentSoft()), 4.0F);
                ImGui::GetWindowDrawList()->AddRect(targetPos,
                                                    ImVec2(targetPos.x + targetSize.x, targetPos.y + targetSize.y),
                                                    Theme::U32(Theme::Accent()), 1.0F, 0, 2.0F);
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HORO_WORKSPACE_PANEL");
                    payload != nullptr && PanelDragEligible())
                {
                    outCommand.command = EditorWorkspaceViewCommand::DockWorkspacePanel;
                    outCommand.stringPayload =
                        std::string(static_cast<const char*>(payload->Data),
                                    static_cast<std::size_t>(payload->DataSize));
                    outCommand.workspaceDropTarget = WorkspacePanelDropTarget{targetNodeId, kind};
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        };

        ImGui::SetCursorPos(ImVec2(0.0F, 0.0F));
        if (m_splitterInteraction.OwnsPrimaryPointer())
        {
            ImGui::Dummy(ImVec2(size.x, paneChromeHeight));
        }
        else
        {
            ImGui::InvisibleButton("##WorkspacePanelDragHandle", ImVec2(size.x, paneChromeHeight));
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                if (EnsurePanelDragCapture())
                {
                    ImGui::SetDragDropPayload("HORO_WORKSPACE_PANEL", activePanelId.data(), activePanelId.size());
                    ImGui::TextUnformatted(activePanelId.data(), activePanelId.data() + activePanelId.size());
                }
                ImGui::EndDragDropSource();
            }
        }

        const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
        if (dragPayload != nullptr && dragPayload->IsDataType("HORO_WORKSPACE_PANEL"))
        {
            constexpr float edgeFraction = 0.22F;
            const float edgeW = size.x * edgeFraction;
            const float edgeH = size.y * edgeFraction;
            drawWorkspaceDropTarget("##DropLeft", ImVec2(pos.x, pos.y), ImVec2(edgeW, size.y),
                                    WorkspacePanelHost::DropKind::SplitLeft);
            drawWorkspaceDropTarget("##DropRight", ImVec2(pos.x + size.x - edgeW, pos.y), ImVec2(edgeW, size.y),
                                    WorkspacePanelHost::DropKind::SplitRight);
            drawWorkspaceDropTarget("##DropTop", ImVec2(pos.x + edgeW, pos.y), ImVec2(size.x - edgeW * 2.0F, edgeH),
                                    WorkspacePanelHost::DropKind::SplitTop);
            drawWorkspaceDropTarget("##DropBottom", ImVec2(pos.x + edgeW, pos.y + size.y - edgeH),
                                    ImVec2(size.x - edgeW * 2.0F, edgeH), WorkspacePanelHost::DropKind::SplitBottom);
            drawWorkspaceDropTarget("##DropCenter", ImVec2(pos.x + edgeW, pos.y + edgeH),
                                    ImVec2(size.x - edgeW * 2.0F, size.y - edgeH * 2.0F),
                                    WorkspacePanelHost::DropKind::TabCenter);
        }

        // Render the active panel content inside a child view.
        // The panel itself is responsible for drawing its own tabs.
        ImGui::SetCursorPos(ImVec2(0.0F, paneChromeHeight));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
        ImGui::BeginChild("##DockContent", ImVec2(0.0F, size.y - paneChromeHeight), false,
                          ImGuiWindowFlags_NoSavedSettings);
        activePanel->DrawPanel(ImGui::GetWindowPos(), ImGui::GetWindowSize(), viewModel, outCommand, m_context);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    void EditorWorkspaceView::DrawActivityBar(const ImVec2& pos, const ImVec2& size,
                                              const WorkspacePanelRegistry& registry,
                                              const EditorWorkspaceViewModel& viewModel,
                                              EditorWorkspaceViewCommandData& outCommand, const WorkspaceDockArea area,
                                              const bool indicatorOnRight, const bool allowDragSources) const
    {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowBgAlpha(1.0F);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg0());
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 8.0F));

        const char* windowId = indicatorOnRight ? "##ActivityRight" : "##ActivityLeft";
        ImGui::Begin(windowId, nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavInputs |
                     ImGuiWindowFlags_NoNavFocus);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
        constexpr float activityBarBorder = 1.0F;
        constexpr float cellInset = 1.0F;
        constexpr float preferredCellSize = 32.0F;
        const float outerWidth = (std::max)(0.0F, size.x);
        const float availableCellSize = (std::max)(0.0F, outerWidth - 2.0F * (activityBarBorder + cellInset));
        const float cellSize = (std::min)(preferredCellSize, availableCellSize);
        const float cellX = pos.x + (outerWidth - cellSize) * 0.5F;
        const float contentY = windowPos.y + contentMin.y;
        const float itemHeight = cellSize;
        const auto& groups = viewModel.activityBarLayout.Groups(area == WorkspaceDockArea::Right
                                                                    ? ActivityBarRail::Right
                                                                    : ActivityBarRail::Left);
        constexpr float itemGap = 0.0F;
        const bool draggingActivityItem = ImGui::GetDragDropPayload() != nullptr;

        auto findPanel = [&registry](const std::string_view panelId) -> std::shared_ptr<IWorkspacePanel>
        {
            for (const auto& panel : registry.GetAllPanels())
            {
                if (panel->GetId() == panelId)
                {
                    return panel;
                }
            }
            return {};
        };
        auto areaIndex = [](const WorkspaceDockArea dockArea)
        {
            switch (dockArea)
            {
            case WorkspaceDockArea::Left:
                return 0;
            case WorkspaceDockArea::Right:
                return 1;
            case WorkspaceDockArea::Bottom:
                return 2;
            case WorkspaceDockArea::Document:
                return 3;
            }
            return 3;
        };

        constexpr float activityBarBottomPadding = 8.0F;
        const float usableHeight = (std::max)(0.0F, size.y - contentMin.y - activityBarBottomPadding);
        const float groupHeight = groups.empty() ? 0.0F : usableHeight / static_cast<float>(groups.size());

        auto drawDropSlot = [&](const ActivityBarSlot slot, const float y) -> bool
        {
            if (!draggingActivityItem)
            {
                return false;
            }

            ImGui::SetCursorScreenPos(ImVec2(cellX, contentY + y));
            ImGui::PushID(static_cast<int>(slot.groupIndex * 1000 + slot.itemIndex));
            ImGui::InvisibleButton("##ActivityInsertSlot", ImVec2(cellSize, cellSize));
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            const ImVec2 targetMin = ImGui::GetItemRectMin();
            const ImVec2 targetMax = ImGui::GetItemRectMax();
            if (hovered)
            {
                const ImVec2 placeholderMin(targetMin.x + 0.5F, targetMin.y + 0.5F);
                const ImVec2 placeholderMax(targetMax.x - 0.5F, targetMax.y - 0.5F);
                drawList->AddRectFilled(placeholderMin, placeholderMax, Theme::U32(Theme::AccentSoft()), 2.0F);
                drawList->AddRect(placeholderMin, placeholderMax, Theme::U32(Theme::Accent()), 1.0F, 0, 2.0F);
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("HORO_ACTIVITY_BAR_PANEL",
                                                     ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                    payload != nullptr && PanelDragEligible())
                {
                    const std::string droppedPanel(static_cast<const char*>(payload->Data));
                    outCommand.command = EditorWorkspaceViewCommand::ReorderActivityBarItem;
                    outCommand.stringPayload = droppedPanel;
                    outCommand.activityBarSlot = slot;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
            return hovered;
        };

        for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
        {
            const float groupTop = static_cast<float>(groupIndex) * groupHeight;
            const float groupBottom = groupTop + groupHeight;
            float currentY = groupTop;
            if (groupIndex > 0)
            {
                drawList->AddLine(ImVec2(cellX + 6.0F, contentY + groupTop),
                                  ImVec2(cellX + cellSize - 6.0F, contentY + groupTop), Theme::U32(Theme::Border()),
                                  1.0F);
            }

            ImGui::PushClipRect(ImVec2(pos.x, contentY + groupTop), ImVec2(pos.x + size.x, contentY + groupBottom),
                                true);
            const auto& group = groups[groupIndex];
            if (group.items.empty())
            {
                if (draggingActivityItem)
                {
                    drawDropSlot(
                        ActivityBarSlot{
                            area == WorkspaceDockArea::Right ? ActivityBarRail::Right : ActivityBarRail::Left,
                            groupIndex, 0
                        },
                        currentY);
                    currentY += cellSize;
                }
                ImGui::PopClipRect();
                continue;
            }

            for (std::size_t itemIndex = 0; itemIndex < group.items.size(); ++itemIndex)
            {
                if (drawDropSlot(
                    ActivityBarSlot{
                        area == WorkspaceDockArea::Right ? ActivityBarRail::Right : ActivityBarRail::Left,
                        groupIndex, itemIndex
                    },
                    currentY))
                {
                    currentY += cellSize;
                }

                const std::string& panelId = group.items[itemIndex];
                const auto panel = findPanel(panelId);
                if (!panel)
                {
                    continue;
                }

                WorkspaceDockArea panelArea = panel->GetDefaultDockArea();
                if (const auto placement = viewModel.panelDockAreas.find(panelId);
                    placement != viewModel.panelDockAreas.end())
                {
                    panelArea = placement->second;
                }
                const bool isActive =
                    panelId == viewModel.activeLeftPanelId || panelId == viewModel.activeRightPanelId ||
                    panelId == viewModel.activeLeftTopPanelId || panelId == viewModel.activeLeftBottomPanelId ||
                    panelId == viewModel.activeRightTopPanelId || panelId == viewModel.activeRightBottomPanelId ||
                    panelId == viewModel.activeBottomLeftPanelId || panelId == viewModel.activeBottomRightPanelId ||
                    panelId == viewModel.activeBottomPanelId || panelId == viewModel.activeDocumentPanelId;
                const bool activeInBottomSplit =
                    viewModel.bottomDockMode == BottomDockMode::Split &&
                    (panelId == viewModel.activeBottomLeftPanelId || panelId == viewModel.activeBottomRightPanelId);
                const bool activeInSideSplit =
                    (viewModel.leftDockMode == SideDockMode::Split &&
                        (panelId == viewModel.activeLeftTopPanelId || panelId == viewModel.activeLeftBottomPanelId)) ||
                    (viewModel.rightDockMode == SideDockMode::Split &&
                        (panelId == viewModel.activeRightTopPanelId || panelId == viewModel.activeRightBottomPanelId));
                const ImVec2 itemMin(cellX, contentY + currentY);
                const ImVec2 itemMax(cellX + cellSize, contentY + currentY + cellSize);
                ImGui::SetCursorScreenPos(itemMin);
                ImGui::PushID(panelId.c_str());
                if (ImGui::InvisibleButton("##ActivityItem", ImVec2(cellSize, itemHeight)))
                {
                    outCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
                    outCommand.targetIndex = areaIndex(panelArea);
                    outCommand.stringPayload =
                        isActive && !activeInBottomSplit && !activeInSideSplit ? std::string{} : panelId;
                }
                if (allowDragSources && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    if (EnsurePanelDragCapture())
                    {
                        ImGui::SetDragDropPayload("HORO_ACTIVITY_BAR_PANEL", panelId.c_str(), panelId.size() + 1);
                        ImGui::TextUnformatted(panel->GetDisplayName().c_str());
                    }
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();

                drawList->AddRect(ImVec2(itemMin.x + 0.5F, itemMin.y + 0.5F),
                                  ImVec2(itemMax.x - 0.5F, itemMax.y - 0.5F),
                                  Theme::U32(Theme::Border()), 0.0F, 0, 1.0F);
                const ImU32 iconColor = isActive ? Theme::U32(Theme::Text()) : Theme::U32(Theme::Dim());
                panel->DrawIcon(drawList, itemMin, ImVec2(cellSize, itemHeight), iconColor);
                currentY += itemHeight + itemGap;
            }

            if (drawDropSlot(
                ActivityBarSlot{
                    area == WorkspaceDockArea::Right ? ActivityBarRail::Right : ActivityBarRail::Left,
                    groupIndex, group.items.size()
                },
                currentY))
            {
                currentY += cellSize;
            }
            ImGui::PopClipRect();
        }

        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }
} // namespace Horo::Editor
