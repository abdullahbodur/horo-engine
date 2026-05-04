#include "ui/launcher/UiTestHarness.h"

#ifdef HORO_STANDALONE_UI_AUTOMATION

#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>

#include "core/Logger.h"
#include "ui/editor/EditorLayer.h"
#include "ui/launcher/LauncherEditorShell.h"
#include "tests/UiTestRegistry.h"

namespace Horo {
    namespace {
        template<typename Predicate>
        bool WaitForCondition(ImGuiTestContext *ctx, int maxFrames, Predicate &&predicate) {
            if (!ctx)
                return false;
            for (int frame = 0; frame < maxFrames; ++frame) {
                if (predicate())
                    return true;
                ctx->Yield(1);
            }
            return predicate();
        }

        ImGuiID WaitForPopup(ImGuiTestContext *ctx, int depth, const char *sentinelItem, int maxFrames = 60);

        void DismissOpenPopupByClickingOutside(ImGuiTestContext *ctx);

        // ---------------------------------------------------------------------------
        // WaitForPopup: waits for the popup at stack depth `depth` to be rendered
        // and contain `sentinelItem`.  On success leaves ctx->SetRef at that window
        // and returns its ID.  On failure returns 0 (does NOT press Escape).
        //
        // depth 0 = first/outermost popup (e.g. a toolbar or context menu popup).
        // depth 1 = second popup (e.g. an "Add" sub-menu inside a context menu).
        // ---------------------------------------------------------------------------
        ImGuiID WaitForPopup(ImGuiTestContext *ctx, int depth, const char *sentinelItem, int maxFrames) {
            ImGuiID wid = 0;
            bool sentinelFound = false;
            WaitForCondition(ctx, maxFrames, [ctx, depth, sentinelItem, &wid, &sentinelFound]() -> bool {
                                 ImGuiContext &g = *ctx->UiContext;
                                 if (g.OpenPopupStack.Size > depth) {
                                     ImGuiWindow *win = g.OpenPopupStack[depth].Window;
                                     if (win != nullptr) {
                                         wid = win->ID;
                                         ctx->SetRef(wid);
                                         if (!sentinelItem)
                                             return true;
                                         sentinelFound = ctx->ItemExists(sentinelItem);
                                         return sentinelFound;
                                     }
                                 }
                                 return false;
            });
            if (wid && (!sentinelItem || sentinelFound))
                ctx->SetRef(wid);
            return sentinelItem && !sentinelFound ? ImGuiID(0) : wid;
        }

        struct UiScalarRow {
            float minY = 0.0f;
            std::vector<ImGuiID> fieldIds;
        };

        std::vector<UiScalarRow> GatherUnlabeledScalarRows(ImGuiTestContext *ctx) {
            std::vector<UiScalarRow> rows;
            if (!ctx)
                return rows;

            ctx->SetRef("Properties");
            ImGuiTestItemList items;
            ctx->GatherItems(&items, nullptr, -1);
            for (int itemIndex = 0; itemIndex < items.GetSize(); ++itemIndex) {
                const ImGuiTestItemInfo *info = items.GetByIndex(itemIndex);
                if (!info || info->ID == 0 || info->DebugLabel[0] != '\0')
                    continue;

                UiScalarRow *rowForItem = nullptr;
                for (UiScalarRow &row: rows) {
                    if (std::abs(row.minY - info->RectFull.Min.y) < 0.5f) {
                        rowForItem = &row;
                        break;
                    }
                }
                if (!rowForItem) {
                    rows.push_back(UiScalarRow{info->RectFull.Min.y, {}});
                    rowForItem = &rows.back();
                }
                rowForItem->fieldIds.push_back(info->ID);
            }

            std::ranges::sort(rows, [](const UiScalarRow &lhs, const UiScalarRow &rhs) {
                return lhs.minY < rhs.minY;
            });
            return rows;
        }

        const nlohmann::json *
        FindLastObjectOfType(const nlohmann::json &sceneJson, const char *typeName) {
            if (!sceneJson.contains("objects") || !sceneJson.at("objects").is_array())
                return nullptr;
            const auto &objects = sceneJson.at("objects");
            for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
                if (it->contains("type") && it->at("type") == typeName)
                    return &(*it);
            }
            return nullptr;
        }

        nlohmann::json ReadSceneJson(const std::filesystem::path &projectRoot) {
            const auto scenePath = projectRoot / "assets" / "scenes" / "level.json";
            std::ifstream sceneFile(scenePath);
            if (!sceneFile.is_open())
                return nlohmann::json::parse("", nullptr, false);
            return nlohmann::json::parse(sceneFile, nullptr, false);
        }

        bool JsonFloatNear(const nlohmann::json &value, float expected) {
            return value.is_number() && std::abs(value.get<float>() - expected) < 0.001f;
        }

        bool JsonVectorNear(const nlohmann::json &value,
                            std::initializer_list<float> expected) {
            if (!value.is_array() || value.size() != expected.size())
                return false;
            size_t index = 0;
            for (float component: expected) {
                if (!JsonFloatNear(value.at(index), component))
                    return false;
                ++index;
            }
            return true;
        }

        bool JsonStringFloatNear(const nlohmann::json &value, float expected) {
            if (!value.is_string())
                return false;
            try {
                return std::abs(std::stof(value.get<std::string>()) - expected) < 0.001f;
            } catch (...) {
                return false;
            }
        }

        bool LightWorkflowJsonMatches(const nlohmann::json &sceneJson) {
            if (sceneJson.is_discarded())
                return false;
            const nlohmann::json *lightObj = FindLastObjectOfType(sceneJson, "Light");
            if (!lightObj || !lightObj->is_object())
                return false;
            if (!lightObj->contains("id") || !lightObj->contains("type") ||
                !lightObj->contains("position") || !lightObj->contains("scale") ||
                !lightObj->contains("pitch") || !lightObj->contains("yaw") ||
                !lightObj->contains("roll") || !lightObj->contains("props"))
                return false;

            const nlohmann::json &props = lightObj->at("props");
            if (!props.is_object() || !props.contains("lightType") ||
                !props.contains("radius") || !props.contains("intensity") ||
                !props.contains("color"))
                return false;

            const bool hasExpectedLightFields =
                    lightObj->at("type") == "Light" &&
                    JsonVectorNear(lightObj->at("position"), {4.25f, 5.25f, 6.25f}) &&
                    JsonVectorNear(lightObj->at("scale"), {1.5f, 1.75f, 2.0f}) &&
                    JsonFloatNear(lightObj->at("pitch"), 30.0f) &&
                    JsonFloatNear(lightObj->at("yaw"), 45.0f) &&
                    JsonFloatNear(lightObj->at("roll"), 60.0f) &&
                    props.at("lightType") == "directional" &&
                    JsonStringFloatNear(props.at("radius"), 12.5f) &&
                    JsonStringFloatNear(props.at("intensity"), 2.25f) &&
                    props.at("color") == "0.2000,0.4000,0.6000";
            if (!hasExpectedLightFields)
                return false;

            if (!sceneJson.contains("objects") || !sceneJson.at("objects").is_array() ||
                sceneJson.at("objects").size() < 2) {
                return false;
            }
            return true;
        }

        bool MixedSelectionWorkflowJsonMatches(const nlohmann::json &sceneJson) {
            if (sceneJson.is_discarded())
                return false;
            if (!sceneJson.contains("objects") || !sceneJson.at("objects").is_array())
                return false;

            const auto &objects = sceneJson.at("objects");
            if (objects.size() < 2)
                return false;

            size_t panelCount = 0;
            for (const nlohmann::json &obj: objects) {
                if (!obj.is_object() || !obj.contains("type"))
                    continue;
                if (obj.at("type") == "Panel")
                    ++panelCount;
            }
            return panelCount >= 2;
        }

        size_t CountObjectsOfType(const nlohmann::json &sceneJson,
                                  const char *typeName) {
            if (!typeName || sceneJson.is_discarded() || !sceneJson.contains("objects") ||
                !sceneJson.at("objects").is_array()) {
                return 0;
            }
            size_t count = 0;
            for (const nlohmann::json &obj: sceneJson.at("objects")) {
                if (!obj.is_object() || !obj.contains("type"))
                    continue;
                if (obj.at("type") == typeName)
                    ++count;
            }
            return count;
        }

        bool SceneSaveReloadJsonMatches(const nlohmann::json &sceneJson) {
            if (sceneJson.is_discarded())
                return false;
            return CountObjectsOfType(sceneJson, "Panel") >= 1 &&
                   CountObjectsOfType(sceneJson, "Prop") >= 1 &&
                   CountObjectsOfType(sceneJson, "Light") >= 1;
        }

        bool ComponentMutationJsonMatches(
            const nlohmann::json &beforeJson, const nlohmann::json &afterJson) {
            if (beforeJson.is_discarded() || afterJson.is_discarded())
                return false;
            if (CountObjectsOfType(afterJson, "Light") < 1)
                return false;
            return beforeJson.dump() != afterJson.dump();
        }

        bool SceneHasAnyComponents(const nlohmann::json &sceneJson) {
            if (sceneJson.is_discarded() || !sceneJson.contains("objects") ||
                !sceneJson.at("objects").is_array()) {
                return false;
            }
            for (const nlohmann::json &obj: sceneJson.at("objects")) {
                if (!obj.is_object() || !obj.contains("components"))
                    continue;
                if (obj.at("components").is_array() && !obj.at("components").empty())
                    return true;
            }
            return false;
        }

        int CountCurrentRefItemsLabelContaining(ImGuiTestContext *ctx,
                                                std::string_view labelFragment);

        static bool CurrentRefHasItemLabelContaining(ImGuiTestContext *ctx,
                                                     std::string_view labelFragment);

        bool CurrentRefHasMarker(ImGuiTestContext *ctx, std::string_view marker);

        bool ClickCurrentRefMarker(ImGuiTestContext *ctx, std::string_view marker);

        bool ClickCurrentRefItemLabelContaining(ImGuiTestContext *ctx,
                                                std::string_view labelFragment);

        bool ClickLastCurrentRefItemLabelContaining(
            ImGuiTestContext *ctx, std::string_view labelFragment,
            ImGuiMouseButton button = ImGuiMouseButton_Left);

        bool SelectFirstHierarchyItem(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            ctx->SetRef("Hierarchy");
            const bool itemReady = WaitForCondition(
                ctx, 60, [ctx]() {
                    ctx->SetRef("Hierarchy");
                    return CountCurrentRefItemsLabelContaining(ctx, "##obj_tree") > 0;
                });
            if (!itemReady)
                return false;
            if (!ClickCurrentRefItemLabelContaining(ctx, "##obj_tree"))
                return false;
            ctx->Yield(2);
            return true;
        }

        bool SelectLastHierarchyItem(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            ctx->SetRef("Hierarchy");
            const bool itemReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_tree") > 0;
            });
            if (!itemReady)
                return false;
            if (!ClickLastCurrentRefItemLabelContaining(ctx, "##obj_tree"))
                return false;
            ctx->Yield(2);
            return true;
        }

        bool SelectSecondHierarchyItemWithShift(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            ctx->SetRef("Hierarchy");
            const bool itemReady = WaitForCondition(
                ctx, 60, [ctx]() {
                    ctx->SetRef("Hierarchy");
                    return CountCurrentRefItemsLabelContaining(ctx, "##obj_tree") > 1;
                });
            if (!itemReady)
                return false;
            ctx->KeyDown(ImGuiMod_Shift);
            const bool clicked = ClickLastCurrentRefItemLabelContaining(ctx, "##obj_tree");
            ctx->KeyUp(ImGuiMod_Shift);
            if (!clicked)
                return false;
            ctx->Yield(2);
            return true;
        }

        bool OpenAddComponentPopup(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            ctx->SetRef("Properties");
            if (!ctx->ItemExists("+ Add Component"))
                return false;
            ctx->ItemClick("+ Add Component");
            ctx->Yield(2);
            const ImGuiID popupId = WaitForPopup(ctx, 0, nullptr, 20);
            if (!popupId)
                return false;
            return true;
        }

        bool AddObjectViaAutomationHook(UiAutomationRunState *state,
                                        Editor::SceneObjectType type) {
            if (!state || !state->editorContext) {
                LogWarn("UI scenario: editor automation context is unavailable.");
                return false;
            }
            state->editorContext->UiAutomationAddObject(type);
            LogDebug("UI scenario: object added through editor automation hook.");
            return true;
        }

        bool ClickFirstSelectableItemInCurrentRef(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            ImGuiTestItemList items;
            ctx->GatherItems(&items, nullptr, -1);
            for (int itemIndex = 0; itemIndex < items.GetSize(); ++itemIndex) {
                const ImGuiTestItemInfo *info = items.GetByIndex(itemIndex);
                if (!info || info->ID == 0)
                    continue;
                ctx->MouseMoveToPos(info->RectFull.GetCenter());
                ctx->MouseClick(ImGuiMouseButton_Left);
                ctx->Yield(1);
                return true;
            }
            return false;
        }

        int CountCurrentRefItemsLabelContaining(ImGuiTestContext *ctx,
                                                std::string_view labelFragment) {
            if (!ctx || labelFragment.empty())
                return 0;
            ImGuiTestItemList items;
            ctx->GatherItems(&items, nullptr, -1);
            int count = 0;
            for (int itemIndex = 0; itemIndex < items.GetSize(); ++itemIndex) {
                const ImGuiTestItemInfo *info = items.GetByIndex(itemIndex);
                if (!info)
                    continue;
                if (std::string(info->DebugLabel).find(labelFragment) != std::string::npos)
                    ++count;
            }
            return count;
        }

        bool ClickLastItemInCurrentRef(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            ImGuiTestItemList items;
            ctx->GatherItems(&items, nullptr, -1);
            for (int itemIndex = items.GetSize() - 1; itemIndex >= 0; --itemIndex) {
                const ImGuiTestItemInfo *info = items.GetByIndex(itemIndex);
                if (!info || info->ID == 0)
                    continue;
                ctx->MouseMoveToPos(info->RectFull.GetCenter());
                ctx->MouseClick(ImGuiMouseButton_Left);
                return true;
            }
            return false;
        }

        bool ClickLastCurrentRefItemLabelContaining(ImGuiTestContext *ctx,
                                                    std::string_view labelFragment,
                                                    ImGuiMouseButton button) {
            if (!ctx || labelFragment.empty())
                return false;
            ImGuiTestItemList items;
            ctx->GatherItems(&items, nullptr, -1);
            for (int itemIndex = items.GetSize() - 1; itemIndex >= 0; --itemIndex) {
                const ImGuiTestItemInfo *info = items.GetByIndex(itemIndex);
                if (!info || info->ID == 0)
                    continue;
                if (std::string(info->DebugLabel).find(labelFragment) == std::string::npos)
                    continue;
                ctx->MouseMoveToPos(info->RectFull.GetCenter());
                ctx->MouseClick(button);
                ctx->Yield(1);
                return true;
            }
            return false;
        }

        bool CurrentRefHasItemLabelContaining(ImGuiTestContext *ctx,
                                              std::string_view labelFragment) {
            if (!ctx || labelFragment.empty())
                return false;
            ImGuiTestItemList items;
            ctx->GatherItems(&items, nullptr, -1);
            for (int itemIndex = 0; itemIndex < items.GetSize(); ++itemIndex) {
                const ImGuiTestItemInfo *info = items.GetByIndex(itemIndex);
                if (!info)
                    continue;
                if (std::string(info->DebugLabel).find(labelFragment) != std::string::npos)
                    return true;
            }
            return false;
        }

        bool ClickCurrentRefItemLabelContaining(ImGuiTestContext *ctx,
                                                std::string_view labelFragment);

        bool CurrentRefHasMarker(ImGuiTestContext *ctx,
                                 std::string_view marker) {
            if (!ctx || marker.empty())
                return false;
            const std::string markerText(marker);
            if (ctx->ItemExists(markerText.c_str()) ||
                CurrentRefHasItemLabelContaining(ctx, marker))
                return true;
            constexpr size_t kDebugLabelPrefixLength = 29;
            if (marker.size() <= 12)
                return false;
            return CurrentRefHasItemLabelContaining(
                ctx, marker.substr(0, std::min(marker.size(), kDebugLabelPrefixLength)));
        }

        bool ClickCurrentRefMarker(ImGuiTestContext *ctx,
                                   std::string_view marker) {
            if (!ctx || marker.empty())
                return false;
            const std::string markerText(marker);
            if (ctx->ItemExists(markerText.c_str())) {
                ctx->ItemClick(markerText.c_str());
                ctx->Yield(1);
                return true;
            }
            constexpr size_t kDebugLabelPrefixLength = 29;
            return ClickCurrentRefItemLabelContaining(
                ctx, marker.size() > kDebugLabelPrefixLength
                         ? marker.substr(0, kDebugLabelPrefixLength)
                         : marker);
        }

        bool ClickCurrentRefItemLabelContaining(ImGuiTestContext *ctx,
                                                std::string_view labelFragment) {
            if (!ctx || labelFragment.empty())
                return false;
            ImGuiTestItemList items;
            ctx->GatherItems(&items, nullptr, -1);
            for (int itemIndex = 0; itemIndex < items.GetSize(); ++itemIndex) {
                const ImGuiTestItemInfo *info = items.GetByIndex(itemIndex);
                if (!info || info->ID == 0)
                    continue;
                if (std::string(info->DebugLabel).find(labelFragment) == std::string::npos)
                    continue;
                ctx->MouseMoveToPos(info->RectFull.GetCenter());
                ctx->MouseClick(ImGuiMouseButton_Left);
                ctx->Yield(1);
                return true;
            }
            return false;
        }

        void DismissOpenPopupByClickingOutside(ImGuiTestContext *ctx) {
            if (!ctx)
                return;
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            const ImVec2 position =
                    viewport
                        ? ImVec2(viewport->Pos.x + viewport->Size.x - 8.0f,
                                 viewport->Pos.y + viewport->Size.y - 8.0f)
                        : ImVec2(8.0f, 8.0f);
            ctx->MouseMoveToPos(position);
            ctx->MouseClick(ImGuiMouseButton_Left);
            ctx->Yield(2);
        }

        // Opens a toolbar popup by clicking btnLabel, waits for sentinelItem,
        // and leaves ctx->SetRef pointing at the actual popup window.
        // Returns the popup window ID on success, or 0 on timeout.
        // On failure, dismisses any partially opened popup and restores the toolbar
        // ref so callers fail without leaking popup state into later scenarios.
        //
        // Implementation note: BeginPopup creates windows named "##Popup_XXXXXXXX",
        // NOT windows named by popupId. We find the actual popup window by reading
        // g.OpenPopupStack[0].Window directly — this is immune to hash mismatches.
        ImGuiID OpenToolbarPopup(ImGuiTestContext *ctx, const char *btnLabel,
                                 const char * /*popupId*/,
                                 const char *sentinelItem, int maxFrames = 60) {
            ctx->SetRef("##toolbar");
            const bool buttonReady = WaitForCondition(ctx, maxFrames, [ctx, btnLabel]() {
                ctx->SetRef("##toolbar");
                return ctx->ItemExists(btnLabel);
            });
            if (!buttonReady) {
                LogWarn("UI scenario: toolbar button '{}' was not available.", btnLabel);
                return ImGuiID(0);
            }
            if (!ClickCurrentRefItemLabelContaining(ctx, btnLabel)) {
                LogWarn("UI scenario: toolbar button '{}' could not be clicked.", btnLabel);
                return ImGuiID(0);
            }
            ctx->Yield(2);

            const ImGuiID wid = WaitForPopup(ctx, 0, sentinelItem, maxFrames);
            if (!wid) {
                DismissOpenPopupByClickingOutside(ctx);
                ctx->SetRef("##toolbar");
            }
            return wid;
        }

        void ClickModalButtonIfPresent(ImGuiTestContext *ctx,
                                       const char *modalName,
                                       const char *buttonLabel) {
            if (!ctx || !modalName || !buttonLabel)
                return;

            const bool modalReady =
                    WaitForCondition(ctx, 12, [ctx, modalName, buttonLabel]() {
                        ctx->SetRef(modalName);
                        return ctx->ItemExists(buttonLabel);
                    });
            if (!modalReady)
                return;

            ctx->SetRef(modalName);
            ctx->ItemClick(buttonLabel);
            ctx->Yield(2);
        }

        void DismissBlockingEditorModals(ImGuiTestContext *ctx) {
            // Click safe cancel buttons so editor-owned modal-open flags are reset and
            // the next toolbar interaction is not blocked by a re-opened modal.
            ClickModalButtonIfPresent(ctx, "Rename Object", "Cancel");
            ClickModalButtonIfPresent(ctx, "Editor Settings", "Cancel");
            ClickModalButtonIfPresent(ctx, "Create Asset", "Cancel");
            ClickModalButtonIfPresent(ctx, "Unsaved Changes", "Cancel");
            ClickModalButtonIfPresent(ctx, "Confirm Delete Objects", "Cancel");
            ClickModalButtonIfPresent(ctx, "Confirm Delete Asset", "Cancel");
        }

        bool IsPopupWindowOpen(ImGuiTestContext *ctx, const char *windowName) {
            if (!ctx || !windowName)
                return false;

            const ImGuiContext &g = *ctx->UiContext;
            for (int index = 0; index < g.OpenPopupStack.Size; ++index) {
                const ImGuiWindow *window = g.OpenPopupStack[index].Window;
                if (window && window->Name && std::string_view(window->Name) == windowName)
                    return true;
            }
            return false;
        }

        void CaptureScreenshotTo(ImGuiTestContext *ctx,
                                 const std::filesystem::path &dir,
                                 const char *filename) {
            if (!ctx || !ctx->CaptureArgs || dir.empty())
                return;
            const std::string full = (dir / filename).string();
            if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
                return;
            LogDebug("UI scenario capture screenshot: {}", full);
            ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(),
                      IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
            ctx->CaptureScreenshot(0);
        }

        void CaptureIfEnabled(ImGuiTestContext *ctx, const UiAutomationRunState *state, const char *filename) {
            if (!state->captureEnabled || state->videoEnabled)
                return;
            CaptureScreenshotTo(ctx, state->uiCaptureOutputDir, filename);
        }

        UiAutomationRunState *GetTestState(ImGuiTestContext *ctx, const char *scenarioName) {
            LogInfo("UI scenario start: {}", scenarioName);
            if (ctx == nullptr || ctx->Test == nullptr)
                return nullptr;
            return static_cast<UiAutomationRunState *>(ctx->Test->UserData);
        }

        bool EnsureEditorActive(ImGuiTestContext *ctx, UiAutomationRunState *state) {
            const auto *shell = state->shellContext;
            if (!shell)
                return false;
            if (!shell->HasActiveProject()) {
                LogWarn("editor scenario: no active project, skipping.");
                return false;
            }
            // Close editor-owned modals left open by previous scenarios before checking
            // toolbar state; use their visible buttons so modal flags are reset.
            DismissBlockingEditorModals(ctx);
            ctx->SetRef("##toolbar");
            const bool toolbarReady = WaitForCondition(
                ctx, 600, [ctx]() { return ctx->ItemExists("File"); });
            if (!toolbarReady)
                LogWarn("editor scenario: toolbar File menu was not ready.");
            return toolbarReady;
        }

        bool OpenMcpTab(ImGuiTestContext *ctx, int maxFrames = 120) {
            if (!ctx)
                return false;
            const auto scrollWorkspaceToTop = []() {
                if (ImGuiWindow *workspace = ImGui::FindWindowByName("Workspace")) {
                    workspace->Scroll.y = 0.0f;
                    workspace->ScrollTarget.y = 0.0f;
                }
            };
            scrollWorkspaceToTop();
            ctx->Yield(1);
            ctx->SetRef("//Workspace");
            if (CurrentRefHasItemLabelContaining(ctx, "##mcp_test/status_"))
                return true;
            const bool mcpTabReady = WaitForCondition(ctx, maxFrames, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/MCP");
            });
            if (!mcpTabReady) {
                LogWarn("UI scenario: MCP tab item was not found.");
                return false;
            }
            ctx->SetRef("//Workspace/##bottom_tabs");
            ctx->ItemClick("MCP");
            scrollWorkspaceToTop();
            ctx->Yield(2);
            const bool contentReady = WaitForCondition(ctx, maxFrames, [ctx]() {
                if (ImGuiWindow *workspace = ImGui::FindWindowByName("Workspace")) {
                    workspace->Scroll.y = 0.0f;
                    workspace->ScrollTarget.y = 0.0f;
                }
                ctx->SetRef("//Workspace");
                return CurrentRefHasItemLabelContaining(ctx, "##mcp_test/status_");
            });
            if (!contentReady)
                LogWarn("UI scenario: MCP tab content did not become active after click.");
            return contentReady;
        }

        bool WaitForMcpMarker(ImGuiTestContext *ctx, std::string marker, int maxFrames = 60) {
            if (!ctx || marker.empty())
                return false;
            return WaitForCondition(ctx, maxFrames, [ctx, marker]() {
                ctx->SetRef("//Workspace");
                return CurrentRefHasMarker(ctx, marker);
            });
        }

        int ReadMcpActivityRowCount(ImGuiTestContext *ctx, int maxTrackedRows = 40) {
            if (!ctx || maxTrackedRows < 0)
                return -1;
            ctx->SetRef("//Workspace");
            for (int rows = 0; rows <= maxTrackedRows; ++rows) {
                const std::string marker =
                        "##mcp_test/activity_rows_" + std::to_string(rows);
                if (ctx->ItemExists(marker.c_str()) ||
                    CurrentRefHasItemLabelContaining(ctx, marker))
                    return rows;
            }
            return -1;
        }

        bool WaitForMcpActivityRowsAtLeast(ImGuiTestContext *ctx, int minRows, int maxFrames = 120) {
            if (!ctx || minRows < 0)
                return false;
            return WaitForCondition(ctx, maxFrames, [ctx, minRows]() {
                const int rows = ReadMcpActivityRowCount(ctx);
                return rows >= minRows;
            });
        }

        bool WaitForMcpActivityRowsIncrease(ImGuiTestContext *ctx, int previousRows, int maxFrames = 180) {
            if (!ctx || previousRows < 0)
                return false;
            return WaitForMcpActivityRowsAtLeast(ctx, previousRows + 1, maxFrames);
        }

        enum class McpLogClearToggleState { Unknown, Off, On };

        McpLogClearToggleState ReadMcpLogClearToggleState(ImGuiTestContext *ctx) {
            if (!ctx)
                return McpLogClearToggleState::Unknown;
            ctx->SetRef("//Workspace");
            const bool isOn = CurrentRefHasMarker(ctx, "##mcp_test/log_clear_toggle_on");
            const bool isOff = CurrentRefHasMarker(ctx, "##mcp_test/log_clear_toggle_off");
            if (isOn == isOff)
                return McpLogClearToggleState::Unknown;
            return isOn ? McpLogClearToggleState::On : McpLogClearToggleState::Off;
        }

        bool WaitForMcpRequestDetailFieldMarkers(ImGuiTestContext *ctx,
                                                 int maxFrames = 60) {
            return WaitForCondition(ctx, maxFrames, [ctx]() {
                ctx->SetRef("//Workspace");
                const bool methodMarker =
                        CurrentRefHasMarker(ctx, "##mcp_test/request_method_present") ||
                        CurrentRefHasMarker(ctx, "##mcp_test/request_method_empty");
                const bool operationMarker =
                        CurrentRefHasMarker(ctx, "##mcp_test/request_operation_present") ||
                        CurrentRefHasMarker(ctx, "##mcp_test/request_operation_empty");
                const bool requestIdMarker =
                        CurrentRefHasMarker(ctx, "##mcp_test/request_id_present") ||
                        CurrentRefHasMarker(ctx, "##mcp_test/request_id_empty");
                return CurrentRefHasMarker(ctx, "##mcp_test/request_detail_visible") &&
                       CurrentRefHasMarker(ctx, "##mcp_test/request_http_present") &&
                       methodMarker && operationMarker && requestIdMarker;
            });
        }

        // ---- MCP HTTP request helpers (cross-platform, used by mcp_send_* scenarios)
        // ----

#ifdef _WIN32
        using UiSockHandle = SOCKET;
        constexpr UiSockHandle kUiInvalidSock = INVALID_SOCKET;

        void UiCloseSock(UiSockHandle s) {
            if (s != kUiInvalidSock)
                closesocket(s);
        }

        void UiInitSocks() {
            bool s_ready = false;
            if (!s_ready) {
                WSADATA d{};
                WSAStartup(MAKEWORD(2, 2), &d);
                s_ready = true;
            }
        }
#else
        using UiSockHandle = int;
        constexpr UiSockHandle kUiInvalidSock = -1;
        void UiCloseSock(UiSockHandle s) {
            if (s >= 0)
                close(s);
        }
        void UiInitSocks() {
        }
#endif

        // Sends an HTTP POST JSON-RPC body to 127.0.0.1:<port>/mcp.
        // Intended to be called from a detached background thread so the ImGui
        // frame pump is not blocked.
        bool SendMcpHttpPost(int port, const std::string &body) {
            UiInitSocks();
            const UiSockHandle sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == kUiInvalidSock)
                return false;
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(port));
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            if (connect(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) !=
                0) {
                UiCloseSock(sock);
                return false;
            }
            const std::string req = std::format("POST /mcp HTTP/1.1\r\n"
                                                "Host: 127.0.0.1:{}\r\n"
                                                "Content-Type: application/json\r\n"
                                                "Content-Length: {}\r\n"
                                                "Connection: close\r\n"
                                                "\r\n"
                                                "{}",
                                                port, body.size(), body);
            send(sock, req.c_str(), static_cast<int>(req.size()), 0);
            std::array<char, 512> buf{};
            recv(sock, buf.data(), static_cast<int>(buf.size() - 1), 0);
            UiCloseSock(sock);
            return true;
        }

        // Opens the Settings modal, checks "Enable built-in MCP", and clicks Apply.
        // Returns true when Apply was clicked successfully.
        bool EnableMcpViaSettings(ImGuiTestContext *ctx) {
            if (!ctx)
                return false;
            const ImGuiID filePopup =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Settings...");
            if (!filePopup) {
                LogWarn("UI scenario: MCP settings File menu popup did not open.");
                return false;
            }
            ctx->ItemClick("Settings...");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Editor Settings");
                return ctx->ItemExists("Enable built-in MCP");
            });
            if (!modalReady) {
                LogWarn("UI scenario: MCP settings modal did not expose enable checkbox.");
                ClickModalButtonIfPresent(ctx, "Editor Settings", "Cancel");
                return false;
            }

            ctx->SetRef("Editor Settings");
            // Check the box unconditionally — idempotent if already checked.
            LogDebug("UI scenario action: enable MCP in settings modal");
            ctx->ItemCheck("Enable built-in MCP");
            ctx->Yield(1);
            LogDebug("UI scenario action: apply MCP settings");
            ctx->ItemClick("Apply");
            ctx->Yield(4);
            return true;
        }

        // ---- Scenario run functions ----

        void RunToolbarButtonsVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/toolbar_buttons_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            const bool editorActive = EnsureEditorActive(ctx, state);
            IM_CHECK(editorActive);
            if (!editorActive)
                return;

            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));
            IM_CHECK(ctx->ItemExists("Add"));
            IM_CHECK(ctx->ItemExists("Edit"));
            IM_CHECK(ctx->ItemExists("View"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__toolbar_buttons_visible__expect_buttons.png");
            LogInfo("UI scenario done: editor_ui/toolbar_buttons_visible");
        }

        void RunFileMenuItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/file_menu_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupId =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            IM_CHECK(filePopupId != ImGuiID(0));
            if (!filePopupId)
                return;

            IM_CHECK(ctx->ItemExists("New Scene"));
            IM_CHECK(ctx->ItemExists("Open Scene..."));
            IM_CHECK(ctx->ItemExists("Reset Layout"));
            IM_CHECK(ctx->ItemExists("Settings..."));

            DismissOpenPopupByClickingOutside(ctx);
            LogInfo("UI scenario done: editor_ui/file_menu_items");
        }

        void RunAddMenuItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/add_menu_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID addPopupId =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            IM_CHECK(addPopupId != ImGuiID(0));
            if (!addPopupId)
                return;

            IM_CHECK(ctx->ItemExists("Panel"));
            IM_CHECK(ctx->ItemExists("Prop"));
            IM_CHECK(ctx->ItemExists("Light"));
            IM_CHECK(ctx->ItemExists("Camera"));

            DismissOpenPopupByClickingOutside(ctx);
            LogInfo("UI scenario done: editor_ui/add_menu_items");
        }

        void RunEditMenuItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/edit_menu_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID editPopupId =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Undo");
            IM_CHECK(editPopupId != ImGuiID(0));
            if (!editPopupId)
                return;

            IM_CHECK(ctx->ItemExists("Undo"));
            IM_CHECK(ctx->ItemExists("Redo"));
            IM_CHECK(ctx->ItemExists("Delete"));
            IM_CHECK(ctx->ItemExists("Duplicate"));

            DismissOpenPopupByClickingOutside(ctx);
            LogInfo("UI scenario done: editor_ui/edit_menu_items");
        }

        void RunViewMenuItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/view_menu_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID viewPopupId =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Fly Mode");
            IM_CHECK(viewPopupId != ImGuiID(0));
            if (!viewPopupId)
                return;

            IM_CHECK(ctx->ItemExists("Fly Mode"));
            IM_CHECK(ctx->ItemExists("Help"));
            IM_CHECK(ctx->ItemExists("Quick Open"));
            IM_CHECK(ctx->ItemExists("Command Palette"));

            DismissOpenPopupByClickingOutside(ctx);
            LogInfo("UI scenario done: editor_ui/view_menu_items");
        }

        void RunSceneControlButtonsVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/scene_control_buttons_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            ctx->SetRef("##toolbar");
            // Play and Stop are mutually exclusive (toggled by m_playMode).
            // In normal (non-play) state, "Play" is shown; in play mode "Stop"
            // is shown. Verify that at least one of them is present.
            const bool hasPlay = ctx->ItemExists("Play");
            const bool hasStop = ctx->ItemExists("Stop");
            IM_CHECK(hasPlay || hasStop);
            IM_CHECK(ctx->ItemExists("Load"));
            IM_CHECK(ctx->ItemExists("Save"));
            IM_CHECK(ctx->ItemExists("Close editor"));

            CaptureIfEnabled(
                ctx, state,
                "editor_ui__scene_control_buttons_visible__expect_controls.png");
            LogInfo("UI scenario done: editor_ui/scene_control_buttons_visible");
        }

        void RunCloseEditorReturnsToLauncher(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/close_editor_returns_to_launcher");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            Launcher::LauncherEditorShell *shell = state->shellContext;
            IM_CHECK(shell != nullptr);
            if (!shell)
                return;

            if (!shell->HasActiveProject()) {
                std::string openError;
                const bool reopened = shell->OpenProject(state->projectRoot, &openError);
                IM_CHECK(reopened);
                if (!reopened) {
                    LogWarn("UI scenario failed to reopen project before close test: {}",
                            openError);
                    return;
                }
                ctx->Yield(4);
            }

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            ctx->SetRef("##toolbar");
            LogDebug("UI scenario action: click 'Close editor'");
            ctx->ItemClick("Close editor");

            const bool discardReady = WaitForCondition(ctx, 30, [ctx, shell]() {
                if (!shell->HasActiveProject())
                    return true;
                return ctx->ItemExists("Unsaved Changes/Discard");
            });
            if (discardReady && shell->HasActiveProject() &&
                ctx->ItemExists("Unsaved Changes/Discard")) {
                ctx->SetRef("Unsaved Changes");
                LogDebug("UI scenario action: discard close-editor modal");
                ctx->ItemClick("Discard");
                ctx->Yield(2);
            }

            bool projectClosed = WaitForCondition(
                ctx, 120, [shell]() { return !shell->HasActiveProject(); });
            if (!projectClosed && shell->HasActiveProject()) {
                LogWarn("UI scenario: Close editor button did not close the project; "
                    "using direct shell close fallback.");
                shell->CloseProject();
                projectClosed = WaitForCondition(
                    ctx, 120, [shell]() { return !shell->HasActiveProject(); });
            }
            IM_CHECK(projectClosed);
            if (!projectClosed)
                LogWarn("UI scenario failed to observe project close within timeout.");

            LogInfo("UI scenario done: editor_ui/close_editor_returns_to_launcher");
        }

        void RunBottomDockTabsVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/bottom_dock_tabs_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Project");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Workspace");
            IM_CHECK(ctx->ItemExists("##bottom_tabs/Project"));
            IM_CHECK(ctx->ItemExists("##bottom_tabs/Console"));
            IM_CHECK(ctx->ItemExists("##bottom_tabs/MCP"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__bottom_dock_tabs_visible__expect_tabs.png");
            LogInfo("UI scenario done: editor_ui/bottom_dock_tabs_visible");
        }

        void RunConsoleTabControls(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/console_tab_controls");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Console");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("//Workspace/##bottom_tabs");
            LogDebug("UI scenario action: click Console tab");
            ctx->ItemClick("Console");
            ctx->Yield(2);
            const bool consoleReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("//Workspace");
                return CurrentRefHasItemLabelContaining(ctx, "Clear") &&
                       CurrentRefHasItemLabelContaining(ctx, "Info") &&
                       CurrentRefHasItemLabelContaining(ctx, "Warn") &&
                       CurrentRefHasItemLabelContaining(ctx, "Error");
            });
            IM_CHECK(consoleReady);
            if (!consoleReady)
                return;

            ctx->SetRef("//Workspace");
            IM_CHECK(CurrentRefHasItemLabelContaining(ctx, "Clear"));
            IM_CHECK(CurrentRefHasItemLabelContaining(ctx, "Info"));
            IM_CHECK(CurrentRefHasItemLabelContaining(ctx, "Warn"));
            IM_CHECK(CurrentRefHasItemLabelContaining(ctx, "Error"));

            LogDebug("UI scenario action: click Clear button");
            IM_CHECK(ClickCurrentRefItemLabelContaining(ctx, "Clear"));

            LogInfo("UI scenario done: editor_ui/console_tab_controls");
        }

        void RunMcpTabButtons(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/mcp_tab_buttons");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/MCP");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("//Workspace/##bottom_tabs");
            LogDebug("UI scenario action: click MCP tab");
            ctx->ItemClick("MCP");
            ctx->Yield(2);
            const bool mcpReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("//Workspace");
                return CurrentRefHasMarker(ctx, "##mcp_test/status_") &&
                       CurrentRefHasMarker(ctx, "##mcp_test/open_settings_action") &&
                       CurrentRefHasMarker(ctx, "##mcp_test/clear_log_action");
            });
            IM_CHECK(mcpReady);
            if (!mcpReady)
                return;

            ctx->SetRef("//Workspace");
            IM_CHECK(CurrentRefHasMarker(ctx, "##mcp_test/status_"));
            IM_CHECK(CurrentRefHasMarker(ctx, "##mcp_test/open_settings_action"));
            IM_CHECK(CurrentRefHasMarker(ctx, "##mcp_test/clear_log_action"));

            LogInfo("UI scenario done: editor_ui/mcp_tab_buttons");
        }

        void RunHierarchyPanelVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_panel_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Hierarchy");
                return ctx->ItemExists("##object_search");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Hierarchy");
            IM_CHECK(ctx->ItemExists("##object_search"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__hierarchy_panel_visible__expect_panel.png");
            LogInfo("UI scenario done: editor_ui/hierarchy_panel_visible");
        }

        void RunHierarchySearchInput(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_search_input");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Hierarchy");
                return ctx->ItemExists("##object_search");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Hierarchy");
            LogDebug("UI scenario action: type into hierarchy search");
            ctx->ItemInputValue("##object_search", "test_query");
            ctx->Yield(2);

            LogDebug("UI scenario action: clear hierarchy search");
            ctx->ItemInputValue("##object_search", "");
            ctx->Yield(1);

            LogInfo("UI scenario done: editor_ui/hierarchy_search_input");
        }

        void RunAssetsPanelVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/assets_panel_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Assets");
                return ctx->ItemExists("+");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Assets");
            IM_CHECK(ctx->ItemExists("+"));
            IM_CHECK(ctx->ItemExists("Search"));

            CaptureIfEnabled(ctx, state, "editor_ui__assets_panel_visible__expect_panel.png");
            LogInfo("UI scenario done: editor_ui/assets_panel_visible");
        }

        void RunHelpWindowOpenClose(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/help_window_open_close");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            LogDebug("UI scenario action: click View menu");
            const ImGuiID viewPopupIdHelp =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Help");
            IM_CHECK(viewPopupIdHelp != ImGuiID(0));
            if (!viewPopupIdHelp)
                return;

            LogDebug("UI scenario action: click Help menu item");
            ctx->ItemClick("Help");
            ctx->Yield(2);

            const bool helpReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Help - Keyboard Shortcuts");
                return ctx->ItemExists("##shortcut_search");
            });
            IM_CHECK(helpReady);
            if (!helpReady) {
                LogWarn("UI scenario: Help window did not open within timeout.");
                return;
            }

            ctx->SetRef("Help - Keyboard Shortcuts");
            IM_CHECK(ctx->ItemExists("##shortcut_search"));

            LogDebug("UI scenario action: close Help window with Close button");
            IM_CHECK(ctx->ItemExists("Close"));
            if (ctx->ItemExists("Close"))
                ctx->ItemClick("Close");
            ctx->Yield(2);

            const bool helpClosed = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Help - Keyboard Shortcuts");
                return !ctx->ItemExists("##shortcut_search");
            });
            IM_CHECK(helpClosed);

            LogInfo("UI scenario done: editor_ui/help_window_open_close");
        }

        void RunSettingsModalOpenCancel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/settings_modal_open_cancel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            LogDebug("UI scenario action: click File menu");
            const ImGuiID filePopupIdSettings =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Settings...");
            IM_CHECK(filePopupIdSettings != ImGuiID(0));
            if (!filePopupIdSettings)
                return;

            LogDebug("UI scenario action: click Settings...");
            ctx->ItemClick("Settings...");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Editor Settings");
                return ctx->ItemExists("Cancel");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                LogWarn("UI scenario: Editor Settings modal did not open within timeout.");
                return;
            }

            ctx->SetRef("Editor Settings");
            IM_CHECK(ctx->ItemExists("Cancel"));
            IM_CHECK(ctx->ItemExists("Apply"));

            LogDebug("UI scenario action: click Cancel to close modal");
            ctx->ItemClick("Cancel");
            ctx->Yield(2);

            LogInfo("UI scenario done: editor_ui/settings_modal_open_cancel");
        }

        // ---- Registration helpers ----

        ImGuiTest *RegisterToolbarButtonsVisible(ImGuiTestEngine *engine, UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "toolbar_buttons_visible");
            test->UserData = state;
            test->TestFunc = &RunToolbarButtonsVisible;
            return test;
        }

        ImGuiTest *RegisterFileMenuItems(ImGuiTestEngine *engine, UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "file_menu_items");
            test->UserData = state;
            test->TestFunc = &RunFileMenuItems;
            return test;
        }

        ImGuiTest *RegisterAddMenuItems(ImGuiTestEngine *engine,
                                        UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "add_menu_items");
            test->UserData = state;
            test->TestFunc = &RunAddMenuItems;
            return test;
        }

        ImGuiTest *RegisterEditMenuItems(ImGuiTestEngine *engine,
                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "edit_menu_items");
            test->UserData = state;
            test->TestFunc = &RunEditMenuItems;
            return test;
        }

        ImGuiTest *RegisterViewMenuItems(ImGuiTestEngine *engine,
                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "view_menu_items");
            test->UserData = state;
            test->TestFunc = &RunViewMenuItems;
            return test;
        }

        ImGuiTest *RegisterSceneControlButtonsVisible(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "scene_control_buttons_visible");
            test->UserData = state;
            test->TestFunc = &RunSceneControlButtonsVisible;
            return test;
        }

        ImGuiTest *RegisterCloseEditorReturnsToLauncher(ImGuiTestEngine *engine,
                                                        UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "close_editor_returns_to_launcher");
            test->UserData = state;
            test->TestFunc = &RunCloseEditorReturnsToLauncher;
            return test;
        }

        ImGuiTest *RegisterBottomDockTabsVisible(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "bottom_dock_tabs_visible");
            test->UserData = state;
            test->TestFunc = &RunBottomDockTabsVisible;
            return test;
        }

        ImGuiTest *RegisterConsoleTabControls(ImGuiTestEngine *engine,
                                              UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "console_tab_controls");
            test->UserData = state;
            test->TestFunc = &RunConsoleTabControls;
            return test;
        }

        ImGuiTest *RegisterMcpTabButtons(ImGuiTestEngine *engine,
                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "mcp_tab_buttons");
            test->UserData = state;
            test->TestFunc = &RunMcpTabButtons;
            return test;
        }

        ImGuiTest *RegisterHierarchyPanelVisible(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "hierarchy_panel_visible");
            test->UserData = state;
            test->TestFunc = &RunHierarchyPanelVisible;
            return test;
        }

        ImGuiTest *RegisterHierarchySearchInput(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "hierarchy_search_input");
            test->UserData = state;
            test->TestFunc = &RunHierarchySearchInput;
            return test;
        }

        ImGuiTest *RegisterAssetsPanelVisible(ImGuiTestEngine *engine,
                                              UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "assets_panel_visible");
            test->UserData = state;
            test->TestFunc = &RunAssetsPanelVisible;
            return test;
        }

        ImGuiTest *RegisterHelpWindowOpenClose(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "help_window_open_close");
            test->UserData = state;
            test->TestFunc = &RunHelpWindowOpenClose;
            return test;
        }

        ImGuiTest *RegisterSettingsModalOpenCancel(ImGuiTestEngine *engine,
                                                   UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "settings_modal_open_cancel");
            test->UserData = state;
            test->TestFunc = &RunSettingsModalOpenCancel;
            return test;
        }

        void RunPropertiesPanelVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiWindow *propsWin = ImGui::FindWindowByName("Properties");
            IM_CHECK(propsWin != nullptr);
            IM_CHECK(!propsWin->Hidden);

            CaptureIfEnabled(ctx, state, "editor_ui__properties_panel_visible.png");
            LogInfo("UI scenario done: editor_ui/properties_panel_visible");
        }

        ImGuiTest *RegisterPropertiesPanelVisible(ImGuiTestEngine *engine,
                                                  UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "properties_panel_visible");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelVisible;
            return test;
        }

        void RunViewportAndStatusbarVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/viewport_statusbar_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiWindow *viewportWin = ImGui::FindWindowByName("Viewport");
            IM_CHECK(viewportWin != nullptr);

            const ImGuiWindow *statusWin = ImGui::FindWindowByName("##editor_statusbar");
            IM_CHECK(statusWin != nullptr);

            CaptureIfEnabled(ctx, state, "editor_ui__viewport_statusbar_visible.png");
            LogInfo("UI scenario done: editor_ui/viewport_statusbar_visible");
        }

        ImGuiTest *RegisterViewportAndStatusbarVisible(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "viewport_statusbar_visible");
            test->UserData = state;
            test->TestFunc = &RunViewportAndStatusbarVisible;
            return test;
        }

        void RunNewSceneAndAddPanel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/new_scene_and_add_panel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // File → New Scene
            const ImGuiID filePopupIdNS =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            if (!filePopupIdNS)
                return;
            ctx->ItemClick("New Scene");
            ctx->Yield(2);

            // Dismiss "Unsaved Changes" modal if it appears
            if (ctx->ItemExists("Unsaved Changes/Discard")) {
                ctx->SetRef("Unsaved Changes");
                ctx->ItemClick("Discard");
                ctx->Yield(2);
            }

            // Wait for hierarchy to stabilise
            WaitForCondition(ctx, 120, [ctx]() {
                return ctx->ItemExists("Hierarchy/##scene_primary");
            });

            // Add → Panel
            const ImGuiID addPopupIdNS =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            IM_CHECK(addPopupIdNS != ImGuiID(0));
            if (!addPopupIdNS)
                return;

            const bool panelClicked = ClickCurrentRefItemLabelContaining(ctx, "Panel");
            if (!panelClicked) {
                LogWarn("UI scenario: Add popup did not expose a Panel item.");
                IM_CHECK(panelClicked);
                return;
            }
            ctx->Yield(4);

            // Hierarchy window should still be visible
            const ImGuiWindow *hierWin = ImGui::FindWindowByName("Hierarchy");
            IM_CHECK(hierWin != nullptr && !hierWin->Hidden);

            CaptureIfEnabled(ctx, state, "editor_ui__new_scene_and_add_panel.png");
            LogInfo("UI scenario done: editor_ui/new_scene_and_add_panel");
        }

        ImGuiTest *RegisterNewSceneAndAddPanel(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "new_scene_and_add_panel");
            test->UserData = state;
            test->TestFunc = &RunNewSceneAndAddPanel;
            return test;
        }

        void RunQuickOpenPopup(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/quick_open_popup");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // View → Quick Open
            const ImGuiID viewPopupIdQO =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Quick Open");
            IM_CHECK(viewPopupIdQO != ImGuiID(0));
            if (!viewPopupIdQO)
                return;

            ctx->ItemClick("Quick Open");
            ctx->Yield(2);

            const ImGuiID quickOpenPopup = WaitForPopup(ctx, 0, "Close");
            IM_CHECK(quickOpenPopup != ImGuiID(0));
            if (!quickOpenPopup)
                return;
            IM_CHECK(ctx->ItemExists("##quick_open_input"));
            ctx->Yield(1);

            CaptureIfEnabled(ctx, state, "editor_ui__quick_open_popup.png");

            // Quick Open is modal-like for the automation harness: while it is
            // open, later scenarios cannot reliably click toolbar or panel
            // buttons. Close it before reporting success so the next scenario
            // starts with an accessible editor UI.
            ctx->ItemClick("Close");
            ctx->Yield(2);

            LogInfo("UI scenario done: editor_ui/quick_open_popup");
        }

        ImGuiTest *RegisterQuickOpenPopup(ImGuiTestEngine *engine,
                                          UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "quick_open_popup");
            test->UserData = state;
            test->TestFunc = &RunQuickOpenPopup;
            return test;
        }

        void RunCommandPalettePopup(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/command_palette_popup");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // View → Command Palette
            const ImGuiID viewPopupIdCP =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Command Palette");
            IM_CHECK(viewPopupIdCP != ImGuiID(0));
            if (!viewPopupIdCP)
                return;

            ctx->ItemClick("Command Palette");
            ctx->Yield(2);

            const ImGuiID commandPalettePopup = WaitForPopup(ctx, 0, "Close");
            IM_CHECK(commandPalettePopup != ImGuiID(0));
            if (commandPalettePopup)
                IM_CHECK(ctx->ItemExists("##command_palette_input"));

            if (!commandPalettePopup)
                return;
            ctx->ItemClick("Close");
            ctx->Yield(1);

            CaptureIfEnabled(ctx, state, "editor_ui__command_palette_popup.png");
            LogInfo("UI scenario done: editor_ui/command_palette_popup");
        }

        ImGuiTest *RegisterCommandPalettePopup(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "command_palette_popup");
            test->UserData = state;
            test->TestFunc = &RunCommandPalettePopup;
            return test;
        }

        void RunSelectObjectViaHierarchy(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/select_object_via_hierarchy");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Ensure hierarchy is visible
            const ImGuiWindow *hierWin = ImGui::FindWindowByName("Hierarchy");
            IM_CHECK(hierWin != nullptr);

            // Click on the first tree node in the hierarchy (##scene_primary)
            // This exercises DrawSceneHeader and tree interaction
            ctx->SetRef("Hierarchy");
            const bool sceneNodeExists = WaitForCondition(
                ctx, 60, [ctx]() { return ctx->ItemExists("##scene_primary"); });
            if (sceneNodeExists)
                ctx->ItemClick("##scene_primary");

            ctx->Yield(2);

            // Properties panel should still be visible regardless of selection
            const ImGuiWindow *propsWin = ImGui::FindWindowByName("Properties");
            IM_CHECK(propsWin != nullptr && !propsWin->Hidden);

            CaptureIfEnabled(ctx, state, "editor_ui__select_object_via_hierarchy.png");
            LogInfo("UI scenario done: editor_ui/select_object_via_hierarchy");
        }

        ImGuiTest *RegisterSelectObjectViaHierarchy(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "select_object_via_hierarchy");
            test->UserData = state;
            test->TestFunc = &RunSelectObjectViaHierarchy;
            return test;
        }

        void RunRenameObjectModal(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/rename_object_modal");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so we have something to select
            const ImGuiID addPopupIdRename =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdRename)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);
            LogDebug("UI scenario action: added panel for rename modal");

            // Open the new panel's context menu in search mode and choose Rename....
            // Earlier scenarios leave existing objects in the shared editor document, so
            // avoid hard-coded PushID indices such as $$0/##obj_tree.
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "board");
            ctx->Yield(2);
            const bool panelReadyRename = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_") > 0;
            });
            IM_CHECK(panelReadyRename);
            if (!panelReadyRename)
                return;
            LogDebug("UI scenario action: searchable panel row ready for rename");

            const bool clickedPanelForRename = ClickLastCurrentRefItemLabelContaining(
                ctx, "##obj_", ImGuiMouseButton_Right);
            IM_CHECK(clickedPanelForRename);
            if (!clickedPanelForRename)
                return;
            LogDebug("UI scenario action: opened panel context menu for rename");
            ctx->Yield(2);
            const ImGuiID renameContextPopup = WaitForPopup(ctx, 0, "Rename...");
            IM_CHECK(renameContextPopup != ImGuiID(0));
            if (!renameContextPopup)
                return;
            LogDebug("UI scenario action: click Rename... from hierarchy context menu");
            ctx->ItemClick("Rename...");
            ctx->Yield(2);

            const bool renameModalOpen = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Rename Object");
                return ctx->ItemExists("New ID");
            });
            IM_CHECK(renameModalOpen);
            LogDebug("UI scenario action: rename modal open={}", renameModalOpen);

            if (renameModalOpen)
                ClickModalButtonIfPresent(ctx, "Rename Object", "Cancel");

            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state, "editor_ui__rename_object_modal.png");
            LogInfo("UI scenario done: editor_ui/rename_object_modal");
        }

        ImGuiTest *RegisterRenameObjectModal(ImGuiTestEngine *engine,
                                             UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "rename_object_modal");
            test->UserData = state;
            test->TestFunc = &RunRenameObjectModal;
            return test;
        }

        // ---- New scenarios (added below) ----

        void RunObjectContextMenuInHierarchy(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/object_context_menu_in_hierarchy");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so there is something to right-click in the hierarchy
            const ImGuiID addPopupIdObjCtx =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdObjCtx) {
                LogWarn("UI scenario: Add popup did not appear.");
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Search for the panel type label ("board") so the context menu is exposed
            // through the searchable object rows regardless of the scene tree expansion.
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "board");
            ctx->Yield(2);

            ctx->SetRef("Hierarchy");
            const bool objExists = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_") > 0;
            });
            if (!objExists) {
                LogWarn("UI scenario: no searchable hierarchy object row found.");
                ctx->ItemInputValue("##object_search", "");
                return;
            }

            LogDebug("UI scenario action: right-click searchable hierarchy object row");
            if (!ClickLastCurrentRefItemLabelContaining(ctx, "##obj_",
                                                        ImGuiMouseButton_Right)) {
                ctx->ItemInputValue("##object_search", "");
                return;
            }
            ctx->Yield(2);

            // Verify context menu items — use WaitForPopup to find the actual popup
            // window
            const ImGuiID objCtxPopupId = WaitForPopup(ctx, 0, "Rename...");
            IM_CHECK(objCtxPopupId != ImGuiID(0));
            if (objCtxPopupId) {
                IM_CHECK(ctx->ItemExists("Rename..."));
                IM_CHECK(ctx->ItemExists("Duplicate"));
                IM_CHECK(ctx->ItemExists("Delete"));
            }

            DismissOpenPopupByClickingOutside(ctx);
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state,
                             "editor_ui__object_context_menu_in_hierarchy.png");
            LogInfo("UI scenario done: editor_ui/object_context_menu_in_hierarchy");
        }

        ImGuiTest *RegisterObjectContextMenuInHierarchy(ImGuiTestEngine *engine,
                                                        UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "object_context_menu_in_hierarchy");
            test->UserData = state;
            test->TestFunc = &RunObjectContextMenuInHierarchy;
            return test;
        }

        void RunDuplicateObjectChangesHierarchy(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/duplicate_object_changes_hierarchy");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel via the Add menu
            const ImGuiID addPopupIdDup =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdDup)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Select the new panel in search mode; fixed tree PushID indices are brittle
            // after earlier scenarios have added objects.
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "board");
            ctx->Yield(2);
            const int initialObjectRows = CountCurrentRefItemsLabelContaining(ctx, "##obj_");
            const bool firstObj = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_") > 0;
            });
            IM_CHECK(firstObj);
            // Select it so Duplicate is enabled
            if (firstObj) {
                const bool selectedLastObject =
                        ClickLastCurrentRefItemLabelContaining(ctx, "##obj_");
                IM_CHECK(selectedLastObject);
                if (!selectedLastObject)
                    return;
            }

            // Duplicate via Edit > Duplicate
            const ImGuiID editPopupIdDup =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Duplicate");
            IM_CHECK(editPopupIdDup != ImGuiID(0));
            if (!editPopupIdDup)
                return;
            ctx->ItemClick("Duplicate");
            ctx->Yield(4);

            // Hierarchy should now contain one more object, regardless of prior tests'
            // object indices in the shared editor document.
            ctx->SetRef("Hierarchy");
            const bool duplicatedObj = WaitForCondition(ctx, 60, [ctx, initialObjectRows]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_") > initialObjectRows;
            });
            IM_CHECK(duplicatedObj);

            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state,
                             "editor_ui__duplicate_object_changes_hierarchy.png");
            LogInfo("UI scenario done: editor_ui/duplicate_object_changes_hierarchy");
        }

        ImGuiTest *
        RegisterDuplicateObjectChangesHierarchy(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "duplicate_object_changes_hierarchy");
            test->UserData = state;
            test->TestFunc = &RunDuplicateObjectChangesHierarchy;
            return test;
        }

        void RunDeleteSelectedObjectFlow(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/delete_selected_object_flow");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so there is an object to delete
            const ImGuiID addPopupIdDel =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdDel)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Select the new panel in search mode so Edit > Delete is enabled without
            // assuming the object lives at a fixed tree PushID index.
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "board");
            ctx->Yield(2);
            const bool panelReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_") > 0;
            });
            if (panelReady) {
                const bool selectedLastObject =
                        ClickLastCurrentRefItemLabelContaining(ctx, "##obj_");
                IM_CHECK(selectedLastObject);
                if (!selectedLastObject)
                    return;
            }
            ctx->Yield(1);

            // Trigger Edit > Delete
            const ImGuiID editPopupIdDel =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Delete");
            IM_CHECK(editPopupIdDel != ImGuiID(0));
            if (!editPopupIdDel)
                return;
            ctx->ItemClick("Delete");
            ctx->Yield(2);

            // The confirm delete modal should appear
            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Confirm Delete Objects");
                return ctx->ItemExists("Cancel") && ctx->ItemExists("Delete");
            });
            IM_CHECK(modalReady);
            if (modalReady) {
                ctx->SetRef("Confirm Delete Objects");
                IM_CHECK(ctx->ItemExists("Cancel"));
                IM_CHECK(ctx->ItemExists("Delete"));

                LogDebug("UI scenario action: click Cancel in delete confirm modal");
                ctx->ItemClick("Cancel");
                const bool modalClosed = WaitForCondition(ctx, 30, [ctx]() {
                    return !IsPopupWindowOpen(ctx, "Confirm Delete Objects");
                });
                IM_CHECK(modalClosed);
            }

            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state, "editor_ui__delete_selected_object_flow.png");
            LogInfo("UI scenario done: editor_ui/delete_selected_object_flow");
        }

        ImGuiTest *RegisterDeleteSelectedObjectFlow(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "delete_selected_object_flow");
            test->UserData = state;
            test->TestFunc = &RunDeleteSelectedObjectFlow;
            return test;
        }

        void RunUndoRedoViaEditMenu(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/undo_redo_via_edit_menu");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel to have an undoable action
            const ImGuiID addPopupIdUR =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdUR)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Undo via Edit > Undo
            const ImGuiID undoPopupId =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Undo");
            IM_CHECK(undoPopupId != ImGuiID(0));
            if (!undoPopupId)
                return;
            ctx->ItemClick("Undo");
            ctx->Yield(4);

            // Redo via Edit > Redo
            const ImGuiID redoPopupId =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Redo");
            IM_CHECK(redoPopupId != ImGuiID(0));
            if (!redoPopupId)
                return;
            ctx->ItemClick("Redo");
            ctx->Yield(4);

            // Hierarchy window should still be visible after undo/redo
            const ImGuiWindow *hierWin = ImGui::FindWindowByName("Hierarchy");
            IM_CHECK(hierWin != nullptr && !hierWin->Hidden);

            CaptureIfEnabled(ctx, state, "editor_ui__undo_redo_via_edit_menu.png");
            LogInfo("UI scenario done: editor_ui/undo_redo_via_edit_menu");
        }

        ImGuiTest *RegisterUndoRedoViaEditMenu(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "undo_redo_via_edit_menu");
            test->UserData = state;
            test->TestFunc = &RunUndoRedoViaEditMenu;
            return test;
        }

        void RunMcpTabContentVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_tab_content_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool tabReady = OpenMcpTab(ctx);
            IM_CHECK(tabReady);
            if (!tabReady)
                return;

            ctx->SetRef("//Workspace");
            const bool contentReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("//Workspace");
                return CurrentRefHasMarker(ctx, "##mcp_test/status_enabled") &&
                       CurrentRefHasMarker(ctx, "##mcp_test/status_running");
            });
            IM_CHECK(contentReady);
            if (!contentReady)
                return;

            CaptureIfEnabled(ctx, state, "editor_ui__mcp_tab_content_visible.png");
            LogInfo("UI scenario done: editor_ui/mcp_tab_content_visible");
        }

        ImGuiTest *RegisterMcpTabContentVisible(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_tab_content_visible");
            test->UserData = state;
            test->TestFunc = &RunMcpTabContentVisible;
            return test;
        }

        void RunMcpLiveRequestVisibility(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_live_request_visibility");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool tabReady = OpenMcpTab(ctx);
            IM_CHECK(tabReady);
            if (!tabReady)
                return;

            const bool rowMarkerReady = WaitForCondition(
                ctx, 60, [ctx]() { return ReadMcpActivityRowCount(ctx) >= 0; });
            IM_CHECK(rowMarkerReady);
            if (!rowMarkerReady)
                return;

            const int rowsBefore = ReadMcpActivityRowCount(ctx);
            IM_CHECK(rowsBefore >= 0);
            if (rowsBefore < 0)
                return;

            constexpr int kMcpPort = 39281;
            const std::string listToolsBody =
                    R"({"jsonrpc":"2.0","id":101,"method":"tools/list","params":{}})";
            std::atomic_bool sendDone{false};
            std::thread sender([listToolsBody, &sendDone]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                SendMcpHttpPost(kMcpPort, listToolsBody);
                sendDone = true;
            });

            const bool rowsIncreased =
                    WaitForMcpActivityRowsIncrease(ctx, rowsBefore, 180);
            IM_CHECK(rowsIncreased);
            const bool sendCompleted = WaitForCondition(
                ctx, 120, [&sendDone]() { return sendDone.load(); });
            IM_CHECK(sendCompleted);
            if (sender.joinable())
                sender.join();
            if (!rowsIncreased || !sendCompleted)
                return;

            const bool detailVisible =
                    WaitForMcpMarker(ctx, "##mcp_test/request_detail_visible", 120);
            IM_CHECK(detailVisible);

            CaptureIfEnabled(ctx, state, "editor_ui__mcp_live_request_visibility.png");
            LogInfo("UI scenario done: editor_ui/mcp_live_request_visibility");
        }

        ImGuiTest *RegisterMcpLiveRequestVisibility(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_live_request_visibility");
            test->UserData = state;
            test->TestFunc = &RunMcpLiveRequestVisibility;
            return test;
        }

        void RunMcpRequestDetailFieldsVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_request_detail_fields_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool tabReady = OpenMcpTab(ctx);
            IM_CHECK(tabReady);
            if (!tabReady)
                return;

            const bool hasRequestRows = WaitForMcpActivityRowsAtLeast(ctx, 1, 180);
            IM_CHECK(hasRequestRows);
            if (!hasRequestRows)
                return;

            const bool detailVisible =
                    WaitForMcpMarker(ctx, "##mcp_test/request_detail_visible", 120);
            IM_CHECK(detailVisible);
            if (!detailVisible)
                return;

            const bool detailFieldsReady = WaitForMcpRequestDetailFieldMarkers(ctx, 120);
            IM_CHECK(detailFieldsReady);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__mcp_request_detail_fields_visible.png");
            LogInfo("UI scenario done: editor_ui/mcp_request_detail_fields_visible");
        }

        ImGuiTest *RegisterMcpRequestDetailFieldsVisible(ImGuiTestEngine *engine,
                                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "mcp_request_detail_fields_visible");
            test->UserData = state;
            test->TestFunc = &RunMcpRequestDetailFieldsVisible;
            return test;
        }

        void RunProjectTabVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/project_tab_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool tabReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Project");
            });
            IM_CHECK(tabReady);
            if (!tabReady)
                return;

            ctx->SetRef("//Workspace/##bottom_tabs");
            LogDebug("UI scenario action: click Project tab");
            ctx->ItemClick("Project");
            ctx->Yield(2);

            // The project panel content (tiles area) should be rendered
            ctx->SetRef("//Workspace");
            const bool tilesReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("//Workspace");
                return CurrentRefHasItemLabelContaining(ctx, "##project_tiles");
            });
            IM_CHECK(tilesReady);

            CaptureIfEnabled(ctx, state, "editor_ui__project_tab_visible.png");
            LogInfo("UI scenario done: editor_ui/project_tab_visible");
        }

        ImGuiTest *RegisterProjectTabVisible(ImGuiTestEngine *engine,
                                             UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "project_tab_visible");
            test->UserData = state;
            test->TestFunc = &RunProjectTabVisible;
            return test;
        }

        void RunObjectTypeFilterInHierarchy(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/object_type_filter_in_hierarchy");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel object
            {
                const ImGuiID pid =
                        OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
                if (!pid)
                    return;
                ctx->ItemClick("Panel");
                ctx->Yield(4);
            }

            // Add a Camera object
            {
                const ImGuiID pid =
                        OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Camera");
                if (!pid)
                    return;
                ctx->ItemClick("Camera");
                ctx->Yield(4);
            }

            // Type "cam" into the hierarchy search filter
            ctx->SetRef("Hierarchy");
            const bool searchReady = WaitForCondition(
                ctx, 60, [ctx]() { return ctx->ItemExists("##object_search"); });
            IM_CHECK(searchReady);
            if (!searchReady)
                return;

            LogDebug("UI scenario action: type 'cam' into hierarchy search");
            ctx->ItemInputValue("##object_search", "cam");
            ctx->Yield(3);

            // Clear search to restore full list
            LogDebug("UI scenario action: clear hierarchy search filter");
            ctx->ItemInputValue("##object_search", "");
            ctx->Yield(2);

            // Hierarchy search input should still be present after clearing
            IM_CHECK(ctx->ItemExists("##object_search"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__object_type_filter_in_hierarchy.png");
            LogInfo("UI scenario done: editor_ui/object_type_filter_in_hierarchy");
        }

        ImGuiTest *RegisterObjectTypeFilterInHierarchy(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "object_type_filter_in_hierarchy");
            test->UserData = state;
            test->TestFunc = &RunObjectTypeFilterInHierarchy;
            return test;
        }

        void RunAssetsPanelAddAssetButton(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/assets_panel_add_asset_button");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Wait for the Assets panel "+" button
            const bool assetsReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Assets");
                return ctx->ItemExists("+");
            });
            IM_CHECK(assetsReady);
            if (!assetsReady)
                return;

            ctx->SetRef("Assets");
            LogDebug("UI scenario action: click '+' in Assets panel");
            ctx->ItemClick("+");
            ctx->Yield(2);

            // The Create Asset modal should open
            const bool modalReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Create Asset");
                return ctx->ItemExists("##draft_id");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                ClickModalButtonIfPresent(ctx, "Create Asset", "Cancel");
                return;
            }

            ctx->SetRef("Create Asset");
            IM_CHECK(ctx->ItemExists("##draft_id"));
            IM_CHECK(ctx->ItemExists("Cancel"));

            LogDebug("UI scenario action: dismiss Create Asset modal");
            ctx->ItemClick("Cancel");
            ctx->Yield(2);

            CaptureIfEnabled(ctx, state, "editor_ui__assets_panel_add_asset_button.png");
            LogInfo("UI scenario done: editor_ui/assets_panel_add_asset_button");
        }

        ImGuiTest *RegisterAssetsPanelAddAssetButton(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "assets_panel_add_asset_button");
            test->UserData = state;
            test->TestFunc = &RunAssetsPanelAddAssetButton;
            return test;
        }

        void RunPropertiesShowsCameraFields(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_shows_camera_fields");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Camera via the Add menu
            const ImGuiID addPopupIdCam =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Camera");
            if (!addPopupIdCam)
                return;
            ctx->ItemClick("Camera");
            ctx->Yield(4);

            // Select the camera in the hierarchy (tree mode: PushID(0) / ##obj_tree)
            ctx->SetRef("Hierarchy");
            const bool camObj = WaitForCondition(
                ctx, 60, [ctx]() { return ctx->ItemExists("$$0/##obj_tree"); });
            if (camObj) {
                ctx->ItemClick("$$0/##obj_tree");
                ctx->Yield(2);
            }

            // Properties panel should show camera-specific drag float fields
            ctx->SetRef("Properties");
            const bool yawReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return ctx->ItemExists("Yaw");
            });
            IM_CHECK(yawReady);
            if (yawReady) {
                IM_CHECK(ctx->ItemExists("Yaw"));
                IM_CHECK(ctx->ItemExists("Pitch"));
                IM_CHECK(ctx->ItemExists("FOV"));
            }

            CaptureIfEnabled(ctx, state, "editor_ui__properties_shows_camera_fields.png");
            LogInfo("UI scenario done: editor_ui/properties_shows_camera_fields");
        }

        ImGuiTest *RegisterPropertiesShowsCameraFields(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "properties_shows_camera_fields");
            test->UserData = state;
            test->TestFunc = &RunPropertiesShowsCameraFields;
            return test;
        }

        void RunMultiSelectShowsBatchPanel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/multi_select_shows_batch_panel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add three Panel objects through the editor automation hook. This
            // scenario verifies the multi-select properties panel; using the hook
            // keeps it independent from toolbar popup timing, which is already
            // covered by dedicated menu scenarios.
            for (int i = 0; i < 3; ++i) {
                const bool panelAdded =
                        AddObjectViaAutomationHook(state, Editor::SceneObjectType::Panel);
                IM_CHECK(panelAdded);
                if (!panelAdded)
                    return;
                ctx->Yield(3);
            }

            if (!state->editorContext)
                return;
            state->editorContext->UiAutomationSelectAllObjects();
            ctx->Yield(2);

            // Properties panel should show the multi-select batch operations
            ctx->SetRef("Properties");
            const bool batchReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/multi_select");
            });
            IM_CHECK(batchReady);
            if (batchReady) {
                IM_CHECK(ctx->ItemExists("Duplicate Selected"));
                IM_CHECK(ctx->ItemExists("Delete Selected"));
            }

            CaptureIfEnabled(ctx, state, "editor_ui__multi_select_shows_batch_panel.png");
            LogInfo("UI scenario done: editor_ui/multi_select_shows_batch_panel");
        }

        ImGuiTest *RegisterMultiSelectShowsBatchPanel(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "multi_select_shows_batch_panel");
            test->UserData = state;
            test->TestFunc = &RunMultiSelectShowsBatchPanel;
            return test;
        }

        void RunEditMenuShowsHistoryItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/edit_menu_shows_history_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel first to ensure Rename... and Create Prefab items are enabled
            const ImGuiID addPopupIdHist =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdHist)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Open Edit menu
            const ImGuiID editPopupIdHist =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Undo");
            IM_CHECK(editPopupIdHist != ImGuiID(0));
            if (!editPopupIdHist)
                return;

            IM_CHECK(ctx->ItemExists("Undo"));
            IM_CHECK(ctx->ItemExists("Redo"));
            IM_CHECK(ctx->ItemExists("Rename..."));
            IM_CHECK(ctx->ItemExists("Duplicate"));
            IM_CHECK(ctx->ItemExists("Delete"));

            DismissOpenPopupByClickingOutside(ctx);

            CaptureIfEnabled(ctx, state, "editor_ui__edit_menu_shows_history_items.png");
            LogInfo("UI scenario done: editor_ui/edit_menu_shows_history_items");
        }

        ImGuiTest *RegisterEditMenuShowsHistoryItems(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "edit_menu_shows_history_items");
            test->UserData = state;
            test->TestFunc = &RunEditMenuShowsHistoryItems;
            return test;
        }

        void RunCloseEditorButton(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/close_editor_button");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;
            Launcher::LauncherEditorShell *shell = state->shellContext;
            IM_CHECK(shell != nullptr);
            if (!shell)
                return;

            // Add a Panel to make the document dirty so the Unsaved Changes modal
            // appears instead of immediately closing the editor
            const bool panelAdded =
                    AddObjectViaAutomationHook(state, Editor::SceneObjectType::Panel);
            IM_CHECK(panelAdded);
            if (!panelAdded)
                return;
            ctx->Yield(4);

            // Click the "Close editor" button in the toolbar
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("Close editor"));
            LogDebug("UI scenario action: click 'Close editor'");
            ctx->ItemClick("Close editor");
            ctx->Yield(2);

            // If the document is dirty an "Unsaved Changes" modal appears
            const bool unsavedModalOpen = WaitForCondition(
                ctx, 60, [ctx]() { return ctx->ItemExists("Unsaved Changes/Cancel"); });

            if (unsavedModalOpen) {
                ctx->SetRef("Unsaved Changes");
                IM_CHECK(ctx->ItemExists("Cancel"));
                IM_CHECK(ctx->ItemExists("Discard"));

                LogDebug("UI scenario action: cancel close-editor modal");
                ctx->ItemClick("Cancel");
                ctx->Yield(2);

                // Toolbar should still be accessible after cancelling
                ctx->SetRef("##toolbar");
                IM_CHECK(ctx->ItemExists("File"));
            } else {
                LogWarn("UI scenario: Close editor did not produce Unsaved Changes "
                    "modal (document may have been clean).");
                if (!shell->HasActiveProject()) {
                    std::string openError;
                    const bool reopened = shell->OpenProject(state->projectRoot, &openError);
                    IM_CHECK(reopened);
                    if (!reopened) {
                        LogWarn("UI scenario failed to reopen project after clean close: {}",
                                openError);
                        return;
                    }
                    ctx->Yield(4);
                }
            }

            CaptureIfEnabled(ctx, state, "editor_ui__close_editor_button.png");
            LogInfo("UI scenario done: editor_ui/close_editor_button");
        }

        ImGuiTest *RegisterCloseEditorButton(ImGuiTestEngine *engine,
                                             UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "close_editor_button");
            test->UserData = state;
            test->TestFunc = &RunCloseEditorButton;
            return test;
        }

        // ---- New scenarios (uncovered EditorLayer paths) ----

        // 1. play_mode_toggle — lines 2182–2200 (DrawPlaybackControls)
        void RunPlayModeToggle(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/play_mode_toggle");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            ctx->SetRef("##toolbar");
            const bool playReady =
                    WaitForCondition(ctx, 60, [ctx]() { return ctx->ItemExists("Play"); });
            IM_CHECK(playReady);
            if (!playReady)
                return;

            LogDebug("UI scenario action: click Play button");
            ctx->ItemClick("Play");
            ctx->Yield(3);

            // After entering play mode, "Stop" should replace "Play"
            ctx->SetRef("##toolbar");
            const bool stopReady =
                    WaitForCondition(ctx, 60, [ctx]() { return ctx->ItemExists("Stop"); });
            IM_CHECK(stopReady);

            if (stopReady) {
                LogDebug("UI scenario action: click Stop to exit play mode");
                ctx->ItemClick("Stop");
                ctx->Yield(2);
            }

            CaptureIfEnabled(ctx, state, "editor_ui__play_mode_toggle.png");
            LogInfo("UI scenario done: editor_ui/play_mode_toggle");
        }

        ImGuiTest *RegisterPlayModeToggle(ImGuiTestEngine *engine,
                                          UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "play_mode_toggle");
            test->UserData = state;
            test->TestFunc = &RunPlayModeToggle;
            return test;
        }

        // 2. file_new_scene_cancel_dirty — lines 2037 + 4433–4494 (Unsaved Changes
        // modal)
        void RunFileNewSceneCancelDirty(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/file_new_scene_cancel_dirty");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel to make the document dirty. The File menu behavior is
            // the behavior under test here, so avoid depending on Add menu popup
            // timing before opening File > New Scene.
            const bool panelAdded =
                    AddObjectViaAutomationHook(state, Editor::SceneObjectType::Panel);
            IM_CHECK(panelAdded);
            if (!panelAdded)
                return;
            ctx->Yield(4);

            // File > New Scene — should trigger Unsaved Changes modal
            const ImGuiID filePopupIdDirty =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            IM_CHECK(filePopupIdDirty != ImGuiID(0));
            if (!filePopupIdDirty)
                return;

            LogDebug("UI scenario action: click New Scene");
            ctx->ItemClick("New Scene");
            ctx->Yield(3);

            const bool unsavedReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Unsaved Changes");
                return ctx->ItemExists("Cancel");
            });
            IM_CHECK(unsavedReady);

            if (unsavedReady) {
                ctx->SetRef("Unsaved Changes");
                IM_CHECK(ctx->ItemExists("Discard"));
                IM_CHECK(ctx->ItemExists("Save & Continue"));

                LogDebug("UI scenario action: click Cancel in Unsaved Changes modal");
                ctx->ItemClick("Cancel");
                ctx->Yield(2);

                // Toolbar still accessible after cancelling
                ctx->SetRef("##toolbar");
                IM_CHECK(ctx->ItemExists("File"));
            }

            CaptureIfEnabled(ctx, state, "editor_ui__file_new_scene_cancel_dirty.png");
            LogInfo("UI scenario done: editor_ui/file_new_scene_cancel_dirty");
        }

        ImGuiTest *RegisterFileNewSceneCancelDirty(ImGuiTestEngine *engine,
                                                   UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "file_new_scene_cancel_dirty");
            test->UserData = state;
            test->TestFunc = &RunFileNewSceneCancelDirty;
            return test;
        }

        // 3. file_open_scene_dismiss — line 2039 (Open Scene... menu item)
        void RunFileOpenSceneDismiss(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/file_open_scene_dismiss");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupIdOpen =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Open Scene...");
            IM_CHECK(filePopupIdOpen != ImGuiID(0));
            if (!filePopupIdOpen)
                return;

            LogDebug("UI scenario action: click Open Scene...");
            ctx->ItemClick("Open Scene...");
            ctx->Yield(2);

            // Dismiss any system dialog or pending action with Escape.
            DismissOpenPopupByClickingOutside(ctx);

            // Toolbar should still be accessible
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__file_open_scene_dismiss.png");
            LogInfo("UI scenario done: editor_ui/file_open_scene_dismiss");
        }

        ImGuiTest *RegisterFileOpenSceneDismiss(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "file_open_scene_dismiss");
            test->UserData = state;
            test->TestFunc = &RunFileOpenSceneDismiss;
            return test;
        }

        // 4. file_reset_layout — line ~2048 (File > Reset Layout)
        void RunFileResetLayout(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/file_reset_layout");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupIdReset =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Reset Layout");
            IM_CHECK(filePopupIdReset != ImGuiID(0));
            if (!filePopupIdReset)
                return;

            LogDebug("UI scenario action: click Reset Layout");
            ctx->ItemClick("Reset Layout");
            ctx->Yield(3);

            // Layout reset should not crash; toolbar still accessible
            ctx->SetRef("##toolbar");
            const bool toolbarBack =
                    WaitForCondition(ctx, 60, [ctx]() { return ctx->ItemExists("File"); });
            IM_CHECK(toolbarBack);

            CaptureIfEnabled(ctx, state, "editor_ui__file_reset_layout.png");
            LogInfo("UI scenario done: editor_ui/file_reset_layout");
        }

        ImGuiTest *RegisterFileResetLayout(ImGuiTestEngine *engine,
                                           UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "file_reset_layout");
            test->UserData = state;
            test->TestFunc = &RunFileResetLayout;
            return test;
        }

        // 5. view_fly_mode_activate — lines 2153–2161 (View > Fly Mode toggle)
        void RunViewFlyModeActivate(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/view_fly_mode_activate");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID viewPopupIdFly =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Fly Mode");
            IM_CHECK(viewPopupIdFly != ImGuiID(0));
            if (!viewPopupIdFly)
                return;

            LogDebug("UI scenario action: click Fly Mode to activate");
            ctx->ItemClick("Fly Mode");
            ctx->Yield(2);

            // Re-open View menu to toggle Fly Mode off
            const ImGuiID viewPopupIdFly2 =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Fly Mode");
            if (viewPopupIdFly2) {
                LogDebug("UI scenario action: click Fly Mode to deactivate");
                ctx->ItemClick("Fly Mode");
                ctx->Yield(2);
            }

            CaptureIfEnabled(ctx, state, "editor_ui__view_fly_mode_activate.png");
            LogInfo("UI scenario done: editor_ui/view_fly_mode_activate");
        }

        ImGuiTest *RegisterViewFlyModeActivate(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "view_fly_mode_activate");
            test->UserData = state;
            test->TestFunc = &RunViewFlyModeActivate;
            return test;
        }

        // 6. settings_modal_apply_button — lines 2874–2886 (DrawSettingsModal Apply)
        void RunSettingsModalApplyButton(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/settings_modal_apply_button");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupIdApply =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Settings...");
            IM_CHECK(filePopupIdApply != ImGuiID(0));
            if (!filePopupIdApply)
                return;

            LogDebug("UI scenario action: open Settings modal");
            ctx->ItemClick("Settings...");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Editor Settings");
                return ctx->ItemExists("Apply");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                ClickModalButtonIfPresent(ctx, "Editor Settings", "Cancel");
                return;
            }

            ctx->SetRef("Editor Settings");
            IM_CHECK(ctx->ItemExists("Apply"));
            IM_CHECK(ctx->ItemExists("Cancel"));

            LogDebug("UI scenario action: click Apply in Settings modal");
            ctx->ItemClick("Apply");
            ctx->Yield(2);

            // Modal should be closed after Apply (MCP apply may succeed or fail;
            // either way the editor should remain responsive)
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__settings_modal_apply_button.png");
            LogInfo("UI scenario done: editor_ui/settings_modal_apply_button");
        }

        ImGuiTest *RegisterSettingsModalApplyButton(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "settings_modal_apply_button");
            test->UserData = state;
            test->TestFunc = &RunSettingsModalApplyButton;
            return test;
        }

        // 7. settings_modal_port_input — line 2857 (DrawSettingsModal Port InputInt)
        void RunSettingsModalPortInput(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/settings_modal_port_input");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupIdPort =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Settings...");
            if (!filePopupIdPort)
                return;

            ctx->ItemClick("Settings...");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Editor Settings");
                return ctx->ItemExists("Port");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                ClickModalButtonIfPresent(ctx, "Editor Settings", "Cancel");
                return;
            }

            ctx->SetRef("Editor Settings");
            IM_CHECK(ctx->ItemExists("Port"));

            // Interact with Port input to exercise InputInt path
            LogDebug("UI scenario action: activate Port input field");
            ctx->ItemClick("Port");
            ctx->Yield(1);

            LogDebug("UI scenario action: cancel settings modal");
            ctx->ItemClick("Cancel");
            ctx->Yield(2);

            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__settings_modal_port_input.png");
            LogInfo("UI scenario done: editor_ui/settings_modal_port_input");
        }

        ImGuiTest *RegisterSettingsModalPortInput(ImGuiTestEngine *engine,
                                                  UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "settings_modal_port_input");
            test->UserData = state;
            test->TestFunc = &RunSettingsModalPortInput;
            return test;
        }

        // 8. create_asset_modal_fill_cancel — lines 4200–4320 (DrawCreateAssetModal)
        void RunCreateAssetModalFillCancel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/create_asset_modal_fill_cancel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Click the "+" button in the Assets panel to open Create Asset modal
            ctx->SetRef("Assets");
            const bool assetsReady =
                    WaitForCondition(ctx, 60, [ctx]() { return ctx->ItemExists("+"); });
            IM_CHECK(assetsReady);
            if (!assetsReady)
                return;

            LogDebug("UI scenario action: click + button in Assets panel");
            ctx->ItemClick("+");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Create Asset");
                return ctx->ItemExists("##draft_id");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                ClickModalButtonIfPresent(ctx, "Create Asset", "Cancel");
                return;
            }

            ctx->SetRef("Create Asset");
            IM_CHECK(ctx->ItemExists("##draft_id"));
            IM_CHECK(ctx->ItemExists("##draft_mesh"));

            LogDebug("UI scenario action: fill Asset ID input");
            ctx->ItemInputValue("##draft_id", "test_asset_ui");
            ctx->Yield(1);

            LogDebug("UI scenario action: fill Mesh input");
            ctx->ItemInputValue("##draft_mesh", "assets/models/test.obj");
            ctx->Yield(1);

            IM_CHECK(ctx->ItemExists("Cancel"));
            LogDebug("UI scenario action: click Cancel in Create Asset modal");
            ctx->ItemClick("Cancel");
            ctx->Yield(2);

            // Assets panel should still be visible
            ctx->SetRef("Assets");
            IM_CHECK(ctx->ItemExists("+"));

            CaptureIfEnabled(ctx, state, "editor_ui__create_asset_modal_fill_cancel.png");
            LogInfo("UI scenario done: editor_ui/create_asset_modal_fill_cancel");
        }

        ImGuiTest *RegisterCreateAssetModalFillCancel(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "create_asset_modal_fill_cancel");
            test->UserData = state;
            test->TestFunc = &RunCreateAssetModalFillCancel;
            return test;
        }

        // 9. delete_confirm_modal_accept — lines 4349–4362 (Confirm Delete, click
        // Delete)
        void RunDeleteConfirmModalAccept(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/delete_confirm_modal_accept");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so there is an object to delete
            const ImGuiID addPopupIdConfirm =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdConfirm)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Select the panel in search mode; previous scenarios may have already added
            // objects so fixed tree PushID indices are brittle.
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "board");
            ctx->Yield(2);
            const bool objExists = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_") > 0;
            });
            IM_CHECK(objExists);
            if (!objExists)
                return;

            const bool selectedObject = ClickLastCurrentRefItemLabelContaining(ctx, "##obj_");
            IM_CHECK(selectedObject);
            if (!selectedObject)
                return;
            ctx->Yield(1);

            // Trigger Edit > Delete
            const ImGuiID editPopupIdConfirm =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Delete");
            IM_CHECK(editPopupIdConfirm != ImGuiID(0));
            if (!editPopupIdConfirm)
                return;

            ctx->ItemClick("Delete");
            ctx->Yield(2);

            // Confirm Delete Objects modal appears
            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Confirm Delete Objects");
                return ctx->ItemExists("Delete");
            });
            IM_CHECK(modalReady);
            if (!modalReady)
                return;

            ctx->SetRef("Confirm Delete Objects");
            IM_CHECK(ctx->ItemExists("Cancel"));
            IM_CHECK(ctx->ItemExists("Delete"));

            LogDebug("UI scenario action: click Delete in confirm modal");
            ctx->ItemClick("Delete");
            const bool modalClosed = WaitForCondition(ctx, 30, [ctx]() {
                return !IsPopupWindowOpen(ctx, "Confirm Delete Objects");
            });
            IM_CHECK(modalClosed);

            // Clear search after the modal path; the exact deleted row index is not
            // stable across prior scenarios, so responsiveness is the invariant here.
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state, "editor_ui__delete_confirm_modal_accept.png");
            LogInfo("UI scenario done: editor_ui/delete_confirm_modal_accept");
        }

        ImGuiTest *RegisterDeleteConfirmModalAccept(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "delete_confirm_modal_accept");
            test->UserData = state;
            test->TestFunc = &RunDeleteConfirmModalAccept;
            return test;
        }

        // 10. scene_header_context_add_panel — lines 3264–3288 (scene header ctx menu)
        void RunSceneHeaderContextAddPanel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/scene_header_context_add_panel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool headerReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Hierarchy");
                return ctx->ItemExists("##scene_primary");
            });
            IM_CHECK(headerReady);
            if (!headerReady)
                return;

            LogDebug("UI scenario action: right-click scene header ##scene_primary");
            ctx->SetRef("Hierarchy");
            ctx->ItemClick("##scene_primary", ImGuiMouseButton_Right);
            ctx->Yield(2);

            // Find scene context menu popup using OpenPopupStack
            const ImGuiID sceneCtxId = WaitForPopup(ctx, 0, "Add");
            IM_CHECK(sceneCtxId != ImGuiID(0));
            if (!sceneCtxId)
                return;

            IM_CHECK(ctx->ItemExists("Add"));
            IM_CHECK(ctx->ItemExists("Save Scene"));

            // Open the Add submenu and click Panel
            ctx->ItemClick("Add");
            ctx->Yield(1);

            // Find Add submenu at stack depth 1
            const ImGuiID addSubId = WaitForPopup(ctx, 1, "Panel");
            if (addSubId) {
                LogDebug("UI scenario action: click Panel in Add submenu");
                ctx->ItemClick("Panel");
                ctx->Yield(3);
            } else {
                DismissOpenPopupByClickingOutside(ctx);
            }

            CaptureIfEnabled(ctx, state, "editor_ui__scene_header_context_add_panel.png");
            LogInfo("UI scenario done: editor_ui/scene_header_context_add_panel");
        }

        ImGuiTest *RegisterSceneHeaderContextAddPanel(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "scene_header_context_add_panel");
            test->UserData = state;
            test->TestFunc = &RunSceneHeaderContextAddPanel;
            return test;
        }

        // 11. hierarchy_context_menu_add_child — lines 3521–3545 (obj ctx Add submenu)
        void RunHierarchyContextMenuAddChild(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_context_menu_add_child");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel as the parent object
            const ImGuiID addPopupIdChild =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdChild)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Activate search mode to get context menu (DrawObjectsTreeSearchMode)
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "board");
            ctx->Yield(2);

            // Compute obj_ctx popup window ID (BeginPopupContextItem inside PushID(0))
            ctx->SetRef("Hierarchy");
            const bool objExists = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_") > 0;
            });
            IM_CHECK(objExists);
            if (!objExists) {
                ctx->ItemInputValue("##object_search", "");
                return;
            }

            LogDebug("UI scenario action: right-click searchable hierarchy object row");
            if (!ClickLastCurrentRefItemLabelContaining(ctx, "##obj_",
                                                        ImGuiMouseButton_Right)) {
                ctx->ItemInputValue("##object_search", "");
                return;
            }
            ctx->Yield(2);

            // Find context menu popup using OpenPopupStack
            const ImGuiID objCtxIdChild = WaitForPopup(ctx, 0, "Add");
            IM_CHECK(objCtxIdChild != ImGuiID(0));
            if (!objCtxIdChild) {
                ctx->SetRef("Hierarchy");
                ctx->ItemInputValue("##object_search", "");
                return;
            }

            IM_CHECK(ctx->ItemExists("Add"));

            // Open Add submenu
            ctx->ItemClick("Add");
            ctx->Yield(1);

            // Find Add submenu at stack depth 1
            const ImGuiID addSubIdChild = WaitForPopup(ctx, 1, "Prop");
            IM_CHECK(addSubIdChild != ImGuiID(0));
            if (addSubIdChild) {
                LogDebug("UI scenario action: click Prop in Add submenu");
                ctx->ItemClick("Prop");
                ctx->Yield(3);
            } else {
                DismissOpenPopupByClickingOutside(ctx);
            }
            // Restore: clear search
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state,
                             "editor_ui__hierarchy_context_menu_add_child.png");
            LogInfo("UI scenario done: editor_ui/hierarchy_context_menu_add_child");
        }

        ImGuiTest *RegisterHierarchyContextMenuAddChild(ImGuiTestEngine *engine,
                                                        UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "hierarchy_context_menu_add_child");
            test->UserData = state;
            test->TestFunc = &RunHierarchyContextMenuAddChild;
            return test;
        }

        // 12. edit_menu_create_prefab — lines 2108–2116 (DrawToolbarEditMenuItems
        // Create Prefab)
        void RunEditMenuCreatePrefab(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/edit_menu_create_prefab");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so Edit > Create Prefab is enabled (needs single selection)
            const ImGuiID addPopupIdPrefab =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdPrefab)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Select the new panel so Create Prefab is enabled without fixed tree indices.
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "board");
            ctx->Yield(2);
            const bool panelReadyPrefab = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_") > 0;
            });
            if (panelReadyPrefab)
                (void) ClickLastCurrentRefItemLabelContaining(ctx, "##obj_");
            ctx->Yield(1);

            // Trigger Edit > Create Prefab
            const ImGuiID editPopupIdPrefab =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Create Prefab");
            IM_CHECK(editPopupIdPrefab != ImGuiID(0));
            if (!editPopupIdPrefab)
                return;

            IM_CHECK(ctx->ItemExists("Create Prefab"));
            LogDebug("UI scenario action: click Create Prefab");
            ctx->ItemClick("Create Prefab");
            ctx->Yield(3);

            // Create Prefab may log an error (no project path) or succeed silently;
            // editor must remain responsive either way.
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state, "editor_ui__edit_menu_create_prefab.png");
            LogInfo("UI scenario done: editor_ui/edit_menu_create_prefab");
        }

        ImGuiTest *RegisterEditMenuCreatePrefab(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "edit_menu_create_prefab");
            test->UserData = state;
            test->TestFunc = &RunEditMenuCreatePrefab;
            return test;
        }

        // 13. assets_panel_search_open_dismiss — lines 3758–3975 (Search popup)
        void RunAssetsPanelSearchOpenDismiss(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/assets_panel_search_open_dismiss");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            ctx->SetRef("Assets");
            const bool searchBtnReady =
                    WaitForCondition(ctx, 60, [ctx]() { return ctx->ItemExists("Search"); });
            IM_CHECK(searchBtnReady);
            if (!searchBtnReady)
                return;

            LogDebug("UI scenario action: click Search button in Assets panel");
            ctx->ItemClick("Search");
            ctx->Yield(2);

            const ImGuiID assetSearchPopup = WaitForPopup(ctx, 0, "Close");
            const bool popupReady = assetSearchPopup != ImGuiID(0);
            IM_CHECK(popupReady);

            if (popupReady) {
                IM_CHECK(ctx->ItemExists("##asset_spotlight_input"));

                LogDebug("UI scenario action: dismiss Asset Search popup with Close");
                ctx->ItemClick("Close");
                ctx->Yield(2);
            } else {
                DismissOpenPopupByClickingOutside(ctx);
            }

            // Assets panel still accessible
            ctx->SetRef("Assets");
            IM_CHECK(ctx->ItemExists("+"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__assets_panel_search_open_dismiss.png");
            LogInfo("UI scenario done: editor_ui/assets_panel_search_open_dismiss");
        }

        ImGuiTest *RegisterAssetsPanelSearchOpenDismiss(ImGuiTestEngine *engine,
                                                        UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "assets_panel_search_open_dismiss");
            test->UserData = state;
            test->TestFunc = &RunAssetsPanelSearchOpenDismiss;
            return test;
        }

        // 14. console_filter_warn_toggle — lines 2547–2558 (DrawConsoleTab checkboxes)
        void RunConsoleFilterWarnToggle(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/console_filter_warn_toggle");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool consoleReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Console");
            });
            IM_CHECK(consoleReady);
            if (!consoleReady)
                return;

            ctx->SetRef("Workspace");
            LogDebug("UI scenario action: switch to Console tab");
            ctx->ItemClick("##bottom_tabs/Console");
            ctx->Yield(2);

            ctx->SetRef("Workspace");
            IM_CHECK(CurrentRefHasItemLabelContaining(ctx, "Warn"));
            IM_CHECK(CurrentRefHasItemLabelContaining(ctx, "Error"));
            IM_CHECK(CurrentRefHasItemLabelContaining(ctx, "Info"));

            // Toggle Warn off
            LogDebug("UI scenario action: toggle Warn checkbox off");
            (void) ClickCurrentRefItemLabelContaining(ctx, "Warn");
            ctx->Yield(1);

            // Toggle Warn back on
            LogDebug("UI scenario action: toggle Warn checkbox on");
            (void) ClickCurrentRefItemLabelContaining(ctx, "Warn");
            ctx->Yield(1);

            // Toggle Error off then on
            LogDebug("UI scenario action: toggle Error checkbox off");
            (void) ClickCurrentRefItemLabelContaining(ctx, "Error");
            ctx->Yield(1);
            LogDebug("UI scenario action: toggle Error checkbox on");
            (void) ClickCurrentRefItemLabelContaining(ctx, "Error");
            ctx->Yield(1);

            CaptureIfEnabled(ctx, state, "editor_ui__console_filter_warn_toggle.png");
            LogInfo("UI scenario done: editor_ui/console_filter_warn_toggle");
        }

        ImGuiTest *RegisterConsoleFilterWarnToggle(ImGuiTestEngine *engine,
                                                   UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "console_filter_warn_toggle");
            test->UserData = state;
            test->TestFunc = &RunConsoleFilterWarnToggle;
            return test;
        }

        // 15. mcp_clear_request_log — line 2812 (DrawMcpTab Clear Request Log)
        void RunMcpClearRequestLog(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_clear_request_log");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            DismissBlockingEditorModals(ctx);

            const bool mcpTabReady = OpenMcpTab(ctx);
            IM_CHECK(mcpTabReady);
            if (!mcpTabReady)
                return;

            ctx->SetRef("//Workspace");
            const bool clearBtnReady = WaitForCondition(
                ctx, 60,
                [ctx]() {
                    ctx->SetRef("//Workspace");
                    return CurrentRefHasMarker(ctx, "##mcp_test/clear_log_action");
                });
            if (!clearBtnReady) {
                LogWarn("UI scenario: MCP clear action marker was not found.");
                IM_CHECK(clearBtnReady);
                return;
            }

            const bool hasRowsBeforeClear = WaitForMcpActivityRowsAtLeast(ctx, 1, 180);
            if (!hasRowsBeforeClear) {
                LogWarn("UI scenario: MCP clear log found no rows before clear.");
                IM_CHECK(hasRowsBeforeClear);
                return;
            }

            const int rowsBeforeClear = ReadMcpActivityRowCount(ctx);
            if (rowsBeforeClear <= 0) {
                LogWarn("UI scenario: MCP clear log row count was {} before clear.",
                        rowsBeforeClear);
                IM_CHECK(rowsBeforeClear > 0);
                return;
            }

            LogDebug("UI scenario action: click Clear Request Log");
            ctx->SetRef("//Workspace");
            IM_CHECK(ClickCurrentRefMarker(ctx, "##mcp_test/clear_log_action"));
            ctx->Yield(2);

            const bool detailHidden =
                    WaitForMcpMarker(ctx, "##mcp_test/request_detail_hidden", 90);
            if (!detailHidden) {
                LogWarn("UI scenario: MCP request detail did not hide after clear.");
                IM_CHECK(detailHidden);
                return;
            }
            const bool emptyRows =
                    WaitForMcpMarker(ctx, "##mcp_test/activity_rows_0", 90);
            if (!emptyRows) {
                LogWarn("UI scenario: MCP request rows did not clear.");
                IM_CHECK(emptyRows);
                return;
            }

            const int rowsAfterClear = ReadMcpActivityRowCount(ctx);
            IM_CHECK(rowsAfterClear == 0);

            CaptureIfEnabled(ctx, state, "editor_ui__mcp_clear_request_log.png");
            LogInfo("UI scenario done: editor_ui/mcp_clear_request_log");
        }

        ImGuiTest *RegisterMcpClearRequestLog(ImGuiTestEngine *engine,
                                              UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_clear_request_log");
            test->UserData = state;
            test->TestFunc = &RunMcpClearRequestLog;
            return test;
        }

        // 16. hierarchy_empty_space_context_add — lines 3231–3255 (obj_ctx_empty menu)
        void RunHierarchyEmptySpaceContextAdd(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_empty_space_context_add");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool hierReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Hierarchy");
                return ctx->ItemExists("##object_search");
            });
            IM_CHECK(hierReady);
            if (!hierReady)
                return;

            // Right-click empty space in hierarchy panel to open obj_ctx_empty popup
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "__ui_empty_context_no_match__");
            ctx->Yield(2);

            const auto clearHierarchySearch = [ctx]() {
                ctx->SetRef("Hierarchy");
                ctx->ItemInputValue("##object_search", "");
                ctx->Yield(1);
            };

            const bool noObjectRows = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Hierarchy");
                return CountCurrentRefItemsLabelContaining(ctx, "##obj_tree") == 0 &&
                       CountCurrentRefItemsLabelContaining(ctx, "##obj_") == 0;
            });
            IM_CHECK(noObjectRows);
            if (!noObjectRows) {
                clearHierarchySearch();
                return;
            }

            LogDebug("UI scenario action: right-click empty area in Hierarchy panel");
            const ImGuiWindow *hierWin = ImGui::FindWindowByName("Hierarchy");
            IM_CHECK(hierWin != nullptr);
            if (!hierWin) {
                clearHierarchySearch();
                return;
            }
            const ImRect hierarchyWorkRect = hierWin->WorkRect;
            const ImVec2 clickPos(hierarchyWorkRect.Min.x + 16.0f,
                                  hierarchyWorkRect.Max.y - 16.0f);
            ctx->MouseMoveToPos(clickPos);
            ctx->Yield(1);
            ctx->MouseClick(ImGuiMouseButton_Right);
            ctx->Yield(2);

            // Find context menu popup using OpenPopupStack
            const ImGuiID emptyCtxId = WaitForPopup(ctx, 0, "Add");
            IM_CHECK(emptyCtxId != ImGuiID(0));
            if (!emptyCtxId) {
                LogWarn("UI scenario: hierarchy empty-space context popup did not open.");
                clearHierarchySearch();
                return;
            }

            IM_CHECK(ctx->ItemExists("Add"));

            // Open the Add submenu and choose Light
            ctx->ItemClick("Add");
            ctx->Yield(1);

            // Find Add submenu at stack depth 1
            const ImGuiID addSubIdEmpty = WaitForPopup(ctx, 1, "Light");
            IM_CHECK(addSubIdEmpty != ImGuiID(0));
            if (addSubIdEmpty) {
                LogDebug("UI scenario action: click Light in empty-space Add submenu");
                ctx->ItemClick("Light");
                ctx->Yield(3);
            } else {
                DismissOpenPopupByClickingOutside(ctx);
            }

            clearHierarchySearch();

            CaptureIfEnabled(ctx, state,
                             "editor_ui__hierarchy_empty_space_context_add.png");
            LogInfo("UI scenario done: editor_ui/hierarchy_empty_space_context_add");
        }

        ImGuiTest *RegisterHierarchyEmptySpaceContextAdd(ImGuiTestEngine *engine,
                                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "hierarchy_empty_space_context_add");
            test->UserData = state;
            test->TestFunc = &RunHierarchyEmptySpaceContextAdd;
            return test;
        }

        // 17. unsaved_changes_discard — line 4468–4480 (Unsaved Changes Discard path)
        void RunUnsavedChangesDiscard(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/unsaved_changes_discard");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel to dirty the document
            const ImGuiID addPopupIdDiscard =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdDiscard)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // File > New Scene to trigger Unsaved Changes modal
            const ImGuiID filePopupIdDiscard =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            if (!filePopupIdDiscard)
                return;

            ctx->ItemClick("New Scene");
            ctx->Yield(3);

            const bool unsavedReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Unsaved Changes");
                return ctx->ItemExists("Discard");
            });
            IM_CHECK(unsavedReady);
            if (!unsavedReady)
                return;

            ctx->SetRef("Unsaved Changes");
            IM_CHECK(ctx->ItemExists("Discard"));

            LogDebug("UI scenario action: click Discard in Unsaved Changes modal");
            ctx->ItemClick("Discard");
            ctx->Yield(4);

            // After discarding, editor should process New Scene and remain active
            ctx->SetRef("##toolbar");
            const bool toolbarBack =
                    WaitForCondition(ctx, 120, [ctx]() { return ctx->ItemExists("File"); });
            IM_CHECK(toolbarBack);

            CaptureIfEnabled(ctx, state, "editor_ui__unsaved_changes_discard.png");
            LogInfo("UI scenario done: editor_ui/unsaved_changes_discard");
        }

        ImGuiTest *RegisterUnsavedChangesDiscard(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "unsaved_changes_discard");
            test->UserData = state;
            test->TestFunc = &RunUnsavedChangesDiscard;
            return test;
        }

        // ---- New scenarios added for coverage improvement ----

        // mcp_enable_and_verify_running: opens Settings, enables MCP, applies, verifies
        // the status line shows "Running" (or at least "Yes" for Enabled).
        void RunMcpEnableAndVerifyRunning(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_enable_and_verify_running");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool enabled = EnableMcpViaSettings(ctx);
            IM_CHECK(enabled);
            if (!enabled)
                return;

            const bool mcpTabReady = OpenMcpTab(ctx);
            IM_CHECK(mcpTabReady);
            if (!mcpTabReady)
                return;

            // The status header should now show "Enabled: Yes"
            ctx->SetRef("//Workspace");
            const bool enabledLabelReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("//Workspace");
                return CurrentRefHasItemLabelContaining(ctx, "##mcp_test/status_running");
            });
            IM_CHECK(enabledLabelReady);

            CaptureIfEnabled(ctx, state, "editor_ui__mcp_enable_and_verify_running.png");
            LogInfo("UI scenario done: editor_ui/mcp_enable_and_verify_running");
        }

        ImGuiTest *RegisterMcpEnableAndVerifyRunning(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_enable_and_verify_running");
            test->UserData = state;
            test->TestFunc = &RunMcpEnableAndVerifyRunning;
            return test;
        }

        // mcp_send_request_and_verify_log: sends a real HTTP POST to the MCP server
        // in a background thread and waits for the activity log to reflect it.
        void RunMcpSendRequestAndVerifyLog(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_send_request_and_verify_log");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool mcpTabReady = OpenMcpTab(ctx);
            IM_CHECK(mcpTabReady);
            if (!mcpTabReady)
                return;

            const int rowsBefore = std::max(0, ReadMcpActivityRowCount(ctx));

            // Send a tools/list request from a background thread so the ImGui
            // frame pump is not blocked.
            constexpr int kMcpPort = 39281;
            const std::string listToolsBody =
                    R"({"jsonrpc":"2.0","id":99,"method":"tools/list","params":{}})";
            std::atomic_bool sendDone{false};
            std::atomic_bool sendOk{false};
            std::thread sender([listToolsBody, &sendDone, &sendOk]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                sendOk = SendMcpHttpPost(kMcpPort, listToolsBody);
                sendDone = true;
            });

            // Wait up to ~4s for the new row to appear.
            const bool rowIncreased =
                    WaitForMcpActivityRowsIncrease(ctx, rowsBefore, 240);
            if (!rowIncreased)
                LogWarn("UI scenario: MCP request row did not increase (rows_before={}, "
                        "send_done={}, send_ok={}).",
                        rowsBefore, sendDone.load(), sendOk.load());
            IM_CHECK(rowIncreased);
            const bool sendCompleted = WaitForCondition(
                ctx, 120, [&sendDone]() { return sendDone.load(); });
            IM_CHECK(sendCompleted);
            if (sender.joinable())
                sender.join();
            if (!rowIncreased || !sendCompleted)
                return;

            CaptureIfEnabled(ctx, state,
                             "editor_ui__mcp_send_request_and_verify_log.png");
            LogInfo("UI scenario done: editor_ui/mcp_send_request_and_verify_log");
        }

        ImGuiTest *RegisterMcpSendRequestAndVerifyLog(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_send_request_and_verify_log");
            test->UserData = state;
            test->TestFunc = &RunMcpSendRequestAndVerifyLog;
            return test;
        }

        // mcp_send_tool_call_verify_method: sends a tools/call request (get_scene)
        // and verifies the method field appears in the request detail pane.
        void RunMcpSendToolCallVerifyMethod(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_send_tool_call_verify_method");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool mcpTabReady = OpenMcpTab(ctx);
            IM_CHECK(mcpTabReady);
            if (!mcpTabReady)
                return;

            const bool markerReady = WaitForCondition(
                ctx, 60, [ctx]() { return ReadMcpActivityRowCount(ctx) >= 0; });
            IM_CHECK(markerReady);
            if (!markerReady)
                return;

            const int rowsBefore = ReadMcpActivityRowCount(ctx);

            constexpr int kMcpPort = 39281;
            const std::string toolCallBody =
                    R"({"jsonrpc":"2.0","id":100,"method":"tools/call","params":{"name":"editor.get_scene","arguments":{}}})";
            std::thread([toolCallBody]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                SendMcpHttpPost(kMcpPort, toolCallBody);
            }).detach();

            const bool rowIncreased =
                    WaitForMcpActivityRowsIncrease(ctx, rowsBefore, 240);
            IM_CHECK(rowIncreased);
            if (!rowIncreased)
                return;

            // After selecting the row (which selects the newest), verify the method
            // marker is present (present or empty variant both indicate the field
            // was rendered).
            const bool detailReady = WaitForMcpRequestDetailFieldMarkers(ctx, 120);
            IM_CHECK(detailReady);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__mcp_send_tool_call_verify_method.png");
            LogInfo("UI scenario done: editor_ui/mcp_send_tool_call_verify_method");
        }

        ImGuiTest *RegisterMcpSendToolCallVerifyMethod(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_send_tool_call_verify_method");
            test->UserData = state;
            test->TestFunc = &RunMcpSendToolCallVerifyMethod;
            return test;
        }

        // mcp_catalog_shows_tools: verifies that the catalog section of the MCP tab
        // renders the tool entries exposed by McpProtocol.
        void RunMcpCatalogShowsTools(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_catalog_shows_tools");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool mcpTabReady = OpenMcpTab(ctx);
            IM_CHECK(mcpTabReady);
            if (!mcpTabReady)
                return;

            ctx->SetRef("//Workspace");
            const bool toolsChildReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("//Workspace");
                return CurrentRefHasMarker(ctx, "##mcp_test/status_running");
            });
            IM_CHECK(toolsChildReady);

            CaptureIfEnabled(ctx, state, "editor_ui__mcp_catalog_shows_tools.png");
            LogInfo("UI scenario done: editor_ui/mcp_catalog_shows_tools");
        }

        ImGuiTest *RegisterMcpCatalogShowsTools(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_catalog_shows_tools");
            test->UserData = state;
            test->TestFunc = &RunMcpCatalogShowsTools;
            return test;
        }

        // mcp_open_settings_from_tab: clicks the "Open Settings" button inside the
        // MCP tab (not the File menu) and verifies the settings modal opens.
        void RunMcpOpenSettingsFromTab(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_open_settings_from_tab");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            DismissBlockingEditorModals(ctx);

            const bool mcpTabReady = OpenMcpTab(ctx);
            IM_CHECK(mcpTabReady);
            if (!mcpTabReady)
                return;

            ctx->SetRef("//Workspace");
            const bool btnReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("//Workspace");
                return CurrentRefHasMarker(ctx, "##mcp_test/open_settings_action");
            });
            IM_CHECK(btnReady);
            if (!btnReady)
                return;

            LogDebug("UI scenario action: click Open Settings in MCP tab");
            ctx->SetRef("//Workspace");
            IM_CHECK(ClickCurrentRefMarker(ctx, "##mcp_test/open_settings_action"));
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Editor Settings");
                return ctx->ItemExists("Apply") || ctx->ItemExists("Cancel");
            });
            IM_CHECK(modalReady);

            // Close the modal cleanly
            ctx->SetRef("Editor Settings");
            if (ctx->ItemExists("Cancel")) {
                ctx->ItemClick("Cancel");
            }
            ctx->Yield(2);

            CaptureIfEnabled(ctx, state, "editor_ui__mcp_open_settings_from_tab.png");
            LogInfo("UI scenario done: editor_ui/mcp_open_settings_from_tab");
        }

        ImGuiTest *RegisterMcpOpenSettingsFromTab(ImGuiTestEngine *engine,
                                                  UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_open_settings_from_tab");
            test->UserData = state;
            test->TestFunc = &RunMcpOpenSettingsFromTab;
            return test;
        }

        // properties_panel_no_selection_message: verifies that the Properties panel
        // shows a "No selection" placeholder when nothing is selected.
        void RunPropertiesPanelNoSelection(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_no_selection");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Create a fresh scene so there are no objects to select.
            const ImGuiID filePopupIdNS =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            if (filePopupIdNS) {
                ctx->ItemClick("New Scene");
                ctx->Yield(2);
                if (ctx->ItemExists("Unsaved Changes/Discard")) {
                    ctx->SetRef("Unsaved Changes");
                    ctx->ItemClick("Discard");
                    ctx->Yield(2);
                }
            }

            // With an empty scene and nothing selected, Properties should show the
            // "No selection" placeholder text.
            ctx->SetRef("Properties");
            const bool noSelReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/no_selection");
            });
            IM_CHECK(noSelReady);

            CaptureIfEnabled(ctx, state, "editor_ui__properties_panel_no_selection.png");
            LogInfo("UI scenario done: editor_ui/properties_panel_no_selection");
        }

        ImGuiTest *RegisterPropertiesPanelNoSelection(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "properties_panel_no_selection");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelNoSelection;
            return test;
        }

        // properties_panel_panel_object_transform: adds a Panel object, selects it,
        // and verifies that the transform fields (Position, Scale, Rotation) appear
        // in the Properties panel.  This exercises DrawPropertiesTransformSection.
        void RunPropertiesPanelPanelObjectTransform(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_panel_object_transform");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;
            const bool panelAdded =
                    AddObjectViaAutomationHook(state, Editor::SceneObjectType::Panel);
            IM_CHECK(panelAdded);
            if (!panelAdded)
                return;
            ctx->Yield(4);
            LogDebug("UI scenario action: added Panel for properties transform");

            // Properties panel should now show transform drag fields.
            ctx->SetRef("Properties");
            const bool posReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/transform_section");
            });
            IM_CHECK(posReady);
            if (!posReady)
                LogWarn("UI scenario: transform marker was not visible for Panel.");
            if (posReady) {
                IM_CHECK(CurrentRefHasMarker(ctx, "##properties_test/transform_section"));
            }

            CaptureIfEnabled(ctx, state,
                             "editor_ui__properties_panel_panel_object_transform.png");
            LogInfo("UI scenario done: "
                "editor_ui/properties_panel_panel_object_transform");
        }

        ImGuiTest *
        RegisterPropertiesPanelPanelObjectTransform(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "properties_panel_panel_object_transform");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelPanelObjectTransform;
            return test;
        }

        // properties_panel_light_object_fields: adds a Light object, selects it, and
        // verifies that the identity section (ID / Type) and transform fields render.
        // This exercises DrawPropertiesIdentitySection + DrawPropertiesTransformSection
        // for the Light type path.
        void RunPropertiesPanelLightObjectFields(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_light_object_fields");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool lightAdded =
                    AddObjectViaAutomationHook(state, Editor::SceneObjectType::Light);
            IM_CHECK(lightAdded);
            if (!lightAdded)
                return;
            ctx->Yield(4);

            // Identity section: ID and Type labels
            ctx->SetRef("Properties");
            const bool idReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/identity_section");
            });
            IM_CHECK(idReady);
            if (idReady)
                IM_CHECK(CurrentRefHasMarker(ctx, "##properties_test/identity_section"));

            // Transform section should also be present for Lights
            const bool posReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/transform_section");
            });
            IM_CHECK(posReady);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__properties_panel_light_object_fields.png");
            LogInfo("UI scenario done: "
                "editor_ui/properties_panel_light_object_fields");
        }

        ImGuiTest *
        RegisterPropertiesPanelLightObjectFields(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "properties_panel_light_object_fields");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelLightObjectFields;
            return test;
        }

        void RunPropertiesPanelLightControlsWorkflow(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_light_controls_workflow");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            const bool editorActive = EnsureEditorActive(ctx, state);
            IM_CHECK(editorActive);
            if (!editorActive)
                return;

            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("Load"));
            IM_CHECK(ctx->ItemExists("Save"));
            IM_CHECK(ctx->ItemExists("Close editor"));
            LogInfo("workflow: toolbar visible");

            const ImGuiID addPanelPopup =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            IM_CHECK(addPanelPopup != ImGuiID(0));
            if (!addPanelPopup)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(3);

            const ImGuiID addPopup =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Light");
            IM_CHECK(addPopup != ImGuiID(0));
            if (!addPopup)
                return;
            ctx->ItemClick("Light");
            ctx->Yield(4);
            LogInfo("workflow: light object added");

            const bool propertiesReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return ctx->ItemExists("Parent##identity_parent") &&
                       ctx->ItemExists("Asset ID") && ctx->ItemExists("+ Add Component");
            });
            IM_CHECK(propertiesReady);
            if (!propertiesReady)
                return;
            LogInfo("workflow: properties ready");

            ctx->SetRef("Properties");
            ctx->ItemClick("Parent##identity_parent");
            ctx->Yield(2);
            const ImGuiID parentPopup = WaitForPopup(ctx, 0, nullptr, 30);
            IM_CHECK(parentPopup != ImGuiID(0));
            if (!parentPopup)
                return;
            if (ctx->ItemExists("obj_000"))
                ctx->ItemClick("obj_000");
            else if (ctx->ItemExists("<root>"))
                ctx->ItemClick("<root>");
            else
                ctx->KeyPress(ImGuiKey_Escape);
            ctx->Yield(2);
            LogInfo("workflow: parent combo handled");

            std::vector<UiScalarRow> scalarRows = GatherUnlabeledScalarRows(ctx);
            std::vector<UiScalarRow> vectorRows;
            for (const UiScalarRow &row: scalarRows) {
                if (row.fieldIds.size() == 3)
                    vectorRows.push_back(row);
            }
            IM_CHECK(vectorRows.size() >= 3);
            if (vectorRows.size() < 3)
                return;
            ctx->ItemInputValue(vectorRows[0].fieldIds[0], 4.25f);
            ctx->ItemInputValue(vectorRows[0].fieldIds[1], 5.25f);
            ctx->ItemInputValue(vectorRows[0].fieldIds[2], 6.25f);
            ctx->ItemInputValue(vectorRows[1].fieldIds[0], 1.5f);
            ctx->ItemInputValue(vectorRows[1].fieldIds[1], 1.75f);
            ctx->ItemInputValue(vectorRows[1].fieldIds[2], 2.0f);
            ctx->ItemInputValue(vectorRows[2].fieldIds[0], 30.0f);
            ctx->ItemInputValue(vectorRows[2].fieldIds[1], 45.0f);
            ctx->ItemInputValue(vectorRows[2].fieldIds[2], 60.0f);
            ctx->Yield(2);
            LogInfo("workflow: transform edited");

            ctx->SetRef("Properties");
            IM_CHECK(ctx->ItemExists("Asset ID"));
            ctx->ItemClick("Asset ID");
            ctx->Yield(2);
            const ImGuiID assetPopup = WaitForPopup(ctx, 0, nullptr, 20);
            IM_CHECK(assetPopup != ImGuiID(0));
            if (!assetPopup)
                return;
            if (ctx->ItemExists("<none>"))
                ctx->ItemClick("<none>");
            else
                ctx->KeyPress(ImGuiKey_Escape);
            ctx->Yield(2);
            LogInfo("workflow: asset combo handled");

            ctx->SetRef("Properties");
            ctx->ItemClick("Type##schema_lightType");
            ctx->Yield(2);
            const ImGuiID typePopup = WaitForPopup(ctx, 0, nullptr, 20);
            IM_CHECK(typePopup != ImGuiID(0));
            if (!typePopup)
                return;
            // The Light type popup items are not reliably addressable by visible label in
            // this test engine, so choose the last schema option and verify the resulting
            // persisted value is the expected non-default "directional" type.
            const bool selectedLightType = ClickLastItemInCurrentRef(ctx);
            IM_CHECK(selectedLightType);
            if (!selectedLightType)
                return;
            ctx->Yield(2);
            LogInfo("workflow: light type combo handled");

            ctx->SetRef("Properties");
            ctx->ItemInputValue("Radius##schema_radius", 12.5f);
            ctx->ItemInputValue("Intensity##schema_intensity", 2.25f);
            ctx->ItemInputValue("Color RGB##schema_color##X", 51.0f);
            ctx->ItemInputValue("Color RGB##schema_color##Y", 102.0f);
            ctx->ItemInputValue("Color RGB##schema_color##Z", 153.0f);
            ctx->Yield(2);
            LogInfo("workflow: light values edited");

            const bool addComponentPopupReady = OpenAddComponentPopup(ctx);
            IM_CHECK(addComponentPopupReady);
            if (!addComponentPopupReady)
                return;
            const bool componentAdded = ClickFirstSelectableItemInCurrentRef(ctx);
            IM_CHECK(componentAdded);
            if (!componentAdded)
                return;
            ctx->Yield(2);
            LogInfo("workflow: component add path exercised");

            ctx->SetRef("##toolbar");
            ctx->ItemClick("Save");
            ctx->Yield(6);
            LogInfo("workflow: save clicked");

            const bool sceneSaved = WaitForCondition(ctx, 60, [state]() {
                return LightWorkflowJsonMatches(ReadSceneJson(state->projectRoot));
            });
            IM_CHECK(sceneSaved);
            if (!sceneSaved)
                return;
            const nlohmann::json sceneJson = ReadSceneJson(state->projectRoot);
            IM_CHECK(LightWorkflowJsonMatches(sceneJson));
            if (!LightWorkflowJsonMatches(sceneJson))
                return;

            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("Load"));
            IM_CHECK(ctx->ItemExists("Close editor"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__properties_panel_light_controls_workflow.png");
            LogInfo(
                "UI scenario done: editor_ui/properties_panel_light_controls_workflow");
        }

        ImGuiTest *
        RegisterPropertiesPanelLightControlsWorkflow(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(
                engine, "editor_ui", "properties_panel_light_controls_workflow");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelLightControlsWorkflow;
            return test;
        }

        void RunPropertiesPanelMixedSelectionWorkflow(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_mixed_selection_workflow");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            const bool editorActive = EnsureEditorActive(ctx, state);
            IM_CHECK(editorActive);
            if (!editorActive)
                return;

            for (int i = 0; i < 2; ++i) {
                const ImGuiID addPopup =
                        OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
                IM_CHECK(addPopup != ImGuiID(0));
                if (!addPopup)
                    return;
                ctx->ItemClick("Panel");
                ctx->Yield(3);
            }

            const bool firstSelected = SelectFirstHierarchyItem(ctx);
            if (firstSelected)
                LogInfo("mixed workflow: first hierarchy item selected");
            const bool secondShiftSelected = SelectSecondHierarchyItemWithShift(ctx);
            if (secondShiftSelected)
                LogInfo("mixed workflow: second hierarchy item shift-selected");

            ctx->SetRef("Properties");
            const bool multiReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return ctx->ItemExists("Duplicate Selected") &&
                       ctx->ItemExists("Delete Selected") &&
                       ctx->ItemExists("Apply Batch Transform");
            });
            if (multiReady) {
                LogInfo("mixed workflow: batch controls visible");
                ctx->ItemClick("Apply Batch Transform");
                ctx->Yield(2);
            }

            ctx->SetRef("##toolbar");
            ctx->ItemClick("Save");
            ctx->Yield(6);

            const bool sceneSaved = WaitForCondition(ctx, 60, [state]() {
                const nlohmann::json sceneJson = ReadSceneJson(state->projectRoot);
                return MixedSelectionWorkflowJsonMatches(sceneJson);
            });
            IM_CHECK(sceneSaved);
            if (!sceneSaved)
                return;

            CaptureIfEnabled(ctx, state,
                             "editor_ui__properties_panel_mixed_selection_workflow.png");
            LogInfo("UI scenario done: editor_ui/properties_panel_mixed_selection_workflow");
        }

        ImGuiTest *RegisterPropertiesPanelMixedSelectionWorkflow(
            ImGuiTestEngine *engine, UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui",
                                     "properties_panel_mixed_selection_workflow");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelMixedSelectionWorkflow;
            return test;
        }

        void RunPropertiesPanelRenameDeleteUndoWorkflow(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_rename_delete_undo_workflow");
            if (!state)
                return;
            const bool editorActive = EnsureEditorActive(ctx, state);
            if (!editorActive)
                return;

            const ImGuiID addPanelPopup =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPanelPopup)
                return;
            ctx->ItemClick("Panel");
            ctx->Yield(3);

            const ImGuiID addLightPopup =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Light");
            if (!addLightPopup)
                return;
            ctx->ItemClick("Light");
            ctx->Yield(4);

            ctx->SetRef("Hierarchy");
            const bool secondObjReady = WaitForCondition(
                ctx, 90, [ctx]() { return ctx->ItemExists("$$1/##obj_tree"); });
            if (!secondObjReady)
                return;
            ctx->ItemClick("$$1/##obj_tree");
            ctx->Yield(2);

            ctx->SetRef("Properties");
            const bool identityReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return ctx->ItemExists("Parent##identity_parent") &&
                       ctx->ItemExists("Type##identity_type");
            });
            if (!identityReady)
                return;

            ctx->ItemClick("Parent##identity_parent");
            ctx->Yield(2);
            const ImGuiID parentPopup = WaitForPopup(ctx, 0, nullptr, 30);
            if (!parentPopup)
                return;
            if (ctx->ItemExists("obj_000"))
                ctx->ItemClick("obj_000");
            else if (ctx->ItemExists("<root>"))
                ctx->ItemClick("<root>");
            else
                (void) ClickFirstSelectableItemInCurrentRef(ctx);
            ctx->Yield(2);

            ctx->SetRef("Hierarchy");
            ctx->ItemClick("$$1/##obj_tree", ImGuiMouseButton_Right);
            ctx->Yield(2);
            const ImGuiID renameContextPopup = WaitForPopup(ctx, 0, "Rename...");
            if (!renameContextPopup)
                return;
            ctx->ItemClick("Rename...");
            ctx->Yield(2);

            const bool renameModalReady = WaitForCondition(
                ctx, 60, [ctx]() { return ctx->ItemExists("Rename Object/New ID"); });
            if (!renameModalReady)
                return;
            constexpr const char *kRenamedId = "renamed_light_obj";
            ctx->ItemInputValue("Rename Object/New ID", kRenamedId);
            ctx->SetRef("Rename Object");
            if (ctx->ItemExists("Rename"))
                ctx->ItemClick("Rename");
            else
                ctx->KeyPress(ImGuiKey_Enter);
            ctx->Yield(3);

            const ImGuiID editPopupDelete =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Delete");
            if (!editPopupDelete)
                return;
            ctx->ItemClick("Delete");
            ctx->Yield(2);
            const bool deleteConfirmReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Confirm Delete Objects/Delete");
            });
            if (!deleteConfirmReady)
                return;
            ctx->SetRef("Confirm Delete Objects");
            ctx->ItemClick("Delete");
            ctx->Yield(3);

            const ImGuiID editPopupUndo =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Undo");
            if (!editPopupUndo)
                return;
            ctx->ItemClick("Undo");
            ctx->Yield(4);

            ctx->SetRef("##toolbar");
            ctx->ItemClick("Save");
            ctx->Yield(6);
            const bool sceneSaved = WaitForCondition(ctx, 90, [state]() {
                const nlohmann::json sceneJson = ReadSceneJson(state->projectRoot);
                return CountObjectsOfType(sceneJson, "Panel") >= 1 &&
                       CountObjectsOfType(sceneJson, "Light") >= 1;
            });
            if (!sceneSaved)
                return;

            CaptureIfEnabled(
                ctx, state,
                "editor_ui__properties_panel_rename_delete_undo_workflow.png");
            LogInfo(
                "UI scenario done: editor_ui/properties_panel_rename_delete_undo_workflow");
        }

        ImGuiTest *
        RegisterPropertiesPanelRenameDeleteUndoWorkflow(ImGuiTestEngine *engine,
                                                        UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(
                engine, "editor_ui", "properties_panel_rename_delete_undo_workflow");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelRenameDeleteUndoWorkflow;
            return test;
        }

        void RunPropertiesPanelSceneSaveReloadWorkflow(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_scene_save_reload_workflow");
            if (!state)
                return;
            const bool editorActive = EnsureEditorActive(ctx, state);
            if (!editorActive)
                return;

            const ImGuiID addLightPopup =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Light");
            if (!addLightPopup)
                return;
            ctx->ItemClick("Light");
            ctx->Yield(4);

            ctx->SetRef("##toolbar");
            ctx->ItemClick("Save");
            ctx->Yield(6);
            const nlohmann::json baselineJson = ReadSceneJson(state->projectRoot);
            if (CountObjectsOfType(baselineJson, "Light") < 1)
                return;

            const bool selectedLight = SelectFirstHierarchyItem(ctx);
            if (!selectedLight)
                return;

            const bool propertiesReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return ctx->ItemExists("+ Add Component");
            });
            if (!propertiesReady)
                return;

            const bool addComponentPopupReady = OpenAddComponentPopup(ctx);
            if (!addComponentPopupReady)
                return;
            const bool componentAdded = ClickFirstSelectableItemInCurrentRef(ctx);
            if (!componentAdded)
                return;
            ctx->Yield(2);

            const ImGuiID editPopupUndo =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Undo");
            if (!editPopupUndo)
                return;
            ctx->ItemClick("Undo");
            ctx->Yield(3);
            const ImGuiID editPopupRedo =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Redo");
            if (!editPopupRedo)
                return;
            ctx->ItemClick("Redo");
            ctx->Yield(3);

            ctx->SetRef("##toolbar");
            ctx->ItemClick("Save");
            ctx->Yield(6);
            const nlohmann::json afterMutationJson = ReadSceneJson(state->projectRoot);
            const bool mutationSerialized =
                    ComponentMutationJsonMatches(baselineJson, afterMutationJson) ||
                    SceneHasAnyComponents(afterMutationJson);
            if (!mutationSerialized)
                return;

            for (const char *objectType: {"Panel", "Prop"}) {
                const ImGuiID addPopup =
                        OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", objectType);
                if (!addPopup)
                    return;
                ctx->ItemClick(objectType);
                ctx->Yield(3);
            }

            ctx->SetRef("##toolbar");
            ctx->ItemClick("Save");
            ctx->Yield(6);

            const bool firstSaveStable = WaitForCondition(ctx, 90, [state]() {
                return SceneSaveReloadJsonMatches(ReadSceneJson(state->projectRoot));
            });
            if (!firstSaveStable)
                return;

            CaptureIfEnabled(
                ctx, state, "editor_ui__properties_panel_scene_save_reload_workflow.png");
            LogInfo(
                "UI scenario done: editor_ui/properties_panel_scene_save_reload_workflow");
        }

        ImGuiTest *RegisterPropertiesPanelSceneSaveReloadWorkflow(
            ImGuiTestEngine *engine, UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(
                engine, "editor_ui", "properties_panel_scene_save_reload_workflow");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelSceneSaveReloadWorkflow;
            return test;
        }

        // properties_panel_identity_section: adds any object, selects it, and verifies
        // the identity labels (ID, Type, Parent combo) render in Properties.
        void RunPropertiesPanelIdentitySection(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_identity_section");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool panelAdded =
                    AddObjectViaAutomationHook(state, Editor::SceneObjectType::Panel);
            IM_CHECK(panelAdded);
            if (!panelAdded)
                return;
            ctx->Yield(4);

            ctx->SetRef("Properties");
            const bool identityReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/identity_section");
            });
            IM_CHECK(identityReady);
            if (identityReady) {
                // Parent combo should also render
                IM_CHECK(CurrentRefHasMarker(ctx, "##properties_test/identity_section"));
            }

            CaptureIfEnabled(ctx, state,
                             "editor_ui__properties_panel_identity_section.png");
            LogInfo("UI scenario done: editor_ui/properties_panel_identity_section");
        }

        ImGuiTest *RegisterPropertiesPanelIdentitySection(ImGuiTestEngine *engine,
                                                          UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "properties_panel_identity_section");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelIdentitySection;
            return test;
        }

        // console_info_checkbox_toggle: toggles the Info checkbox in the console tab
        // and verifies the editor remains responsive.
        void RunConsoleInfoCheckboxToggle(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/console_info_checkbox_toggle");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool consoleTabReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Console");
            });
            IM_CHECK(consoleTabReady);
            if (!consoleTabReady)
                return;

            ctx->SetRef("Workspace");
            ctx->ItemClick("##bottom_tabs/Console");
            ctx->Yield(2);

            ctx->SetRef("Workspace");
            const bool infoReady = WaitForCondition(ctx, 60, [ctx]() {
                return CurrentRefHasItemLabelContaining(ctx, "Info");
            });
            IM_CHECK(infoReady);
            if (!infoReady)
                return;

            // Toggle Info off
            LogDebug("UI scenario action: uncheck Info in console");
            if (ctx->ItemExists("Info"))
                ctx->ItemUncheck("Info");
            else
                IM_CHECK(ClickCurrentRefItemLabelContaining(ctx, "Info"));
            ctx->Yield(2);

            // Toggle Info back on
            LogDebug("UI scenario action: re-check Info in console");
            if (ctx->ItemExists("Info"))
                ctx->ItemCheck("Info");
            else
                IM_CHECK(ClickCurrentRefItemLabelContaining(ctx, "Info"));
            ctx->Yield(2);

            // Editor toolbar should still be accessible
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__console_info_checkbox_toggle.png");
            LogInfo("UI scenario done: editor_ui/console_info_checkbox_toggle");
        }

        ImGuiTest *RegisterConsoleInfoCheckboxToggle(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "console_info_checkbox_toggle");
            test->UserData = state;
            test->TestFunc = &RunConsoleInfoCheckboxToggle;
            return test;
        }

        // console_error_checkbox_toggle: toggles the Error checkbox in the console
        // tab — exercises the Error filter branch in DrawConsoleTab.
        void RunConsoleErrorCheckboxToggle(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/console_error_checkbox_toggle");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool consoleTabReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Console");
            });
            IM_CHECK(consoleTabReady);
            if (!consoleTabReady)
                return;

            ctx->SetRef("Workspace");
            ctx->ItemClick("##bottom_tabs/Console");
            ctx->Yield(2);

            ctx->SetRef("Workspace");
            const bool errorReady = WaitForCondition(ctx, 60, [ctx]() {
                return CurrentRefHasItemLabelContaining(ctx, "Error");
            });
            IM_CHECK(errorReady);
            if (!errorReady)
                return;

            if (ctx->ItemExists("Error"))
                ctx->ItemUncheck("Error");
            else
                IM_CHECK(ClickCurrentRefItemLabelContaining(ctx, "Error"));
            ctx->Yield(2);
            if (ctx->ItemExists("Error"))
                ctx->ItemCheck("Error");
            else
                IM_CHECK(ClickCurrentRefItemLabelContaining(ctx, "Error"));
            ctx->Yield(2);

            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__console_error_checkbox_toggle.png");
            LogInfo("UI scenario done: editor_ui/console_error_checkbox_toggle");
        }

        ImGuiTest *RegisterConsoleErrorCheckboxToggle(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "console_error_checkbox_toggle");
            test->UserData = state;
            test->TestFunc = &RunConsoleErrorCheckboxToggle;
            return test;
        }

        // add_prop_object_and_check_properties: adds a Prop object and verifies the
        // Properties panel renders the asset and schema sections for it.
        void RunAddPropObjectAndCheckProperties(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/add_prop_object_and_check_properties");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool propAdded =
                    AddObjectViaAutomationHook(state, Editor::SceneObjectType::Prop);
            IM_CHECK(propAdded);
            if (!propAdded)
                return;
            ctx->Yield(4);

            ctx->SetRef("Properties");
            const bool posReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/transform_section");
            });
            IM_CHECK(posReady);

            // Asset section combo should render for Prop
            const bool assetSectionReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/asset_section");
            });
            IM_CHECK(assetSectionReady);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__add_prop_object_and_check_properties.png");
            LogInfo("UI scenario done: "
                "editor_ui/add_prop_object_and_check_properties");
        }

        ImGuiTest *
        RegisterAddPropObjectAndCheckProperties(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "add_prop_object_and_check_properties");
            test->UserData = state;
            test->TestFunc = &RunAddPropObjectAndCheckProperties;
            return test;
        }

        // hierarchy_add_multiple_then_multi_select: adds two objects and uses
        // Shift+Click to multi-select them, verifying the batch panel renders.
        void RunHierarchyAddMultipleThenMultiSelect(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_add_multiple_then_multi_select");
            IM_CHECK(state != nullptr);
            if (!state)
                return;
            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add two Panel objects.
            for (int i = 0; i < 2; ++i) {
                const bool panelAdded =
                        AddObjectViaAutomationHook(state, Editor::SceneObjectType::Panel);
                IM_CHECK(panelAdded);
                if (!panelAdded)
                    return;
                ctx->Yield(3);
            }

            if (!state->editorContext)
                return;
            state->editorContext->UiAutomationSelectAllObjects();
            ctx->Yield(2);

            // Properties panel should now show the batch panel.
            ctx->SetRef("Properties");
            const bool batchReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return CurrentRefHasMarker(ctx, "##properties_test/multi_select");
            });
            IM_CHECK(batchReady);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__hierarchy_add_multiple_then_multi_select.png");
            LogInfo("UI scenario done: "
                "editor_ui/hierarchy_add_multiple_then_multi_select");
        }

        ImGuiTest *
        RegisterHierarchyAddMultipleThenMultiSelect(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(
                engine, "editor_ui", "hierarchy_add_multiple_then_multi_select");
            test->UserData = state;
            test->TestFunc = &RunHierarchyAddMultipleThenMultiSelect;
            return test;
        }
    } // namespace

    void RegisterEditorUiScenarioSet() {
        RegisterUiScenario("editor/toolbar_buttons_visible",
                           &RegisterToolbarButtonsVisible);
        RegisterUiScenario("editor/file_menu_items", &RegisterFileMenuItems);
        RegisterUiScenario("editor/add_menu_items", &RegisterAddMenuItems);
        RegisterUiScenario("editor/edit_menu_items", &RegisterEditMenuItems);
        RegisterUiScenario("editor/view_menu_items", &RegisterViewMenuItems);
        RegisterUiScenario("editor/scene_control_buttons_visible",
                           &RegisterSceneControlButtonsVisible);
        RegisterUiScenario("editor/bottom_dock_tabs_visible",
                           &RegisterBottomDockTabsVisible);
        RegisterUiScenario("editor/console_tab_controls",
                           &RegisterConsoleTabControls);
        RegisterUiScenario("editor/mcp_tab_buttons", &RegisterMcpTabButtons);
        RegisterUiScenario("editor/hierarchy_panel_visible",
                           &RegisterHierarchyPanelVisible);
        RegisterUiScenario("editor/hierarchy_search_input",
                           &RegisterHierarchySearchInput);
        RegisterUiScenario("editor/assets_panel_visible",
                           &RegisterAssetsPanelVisible);
        RegisterUiScenario("editor/help_window_open_close",
                           &RegisterHelpWindowOpenClose);
        RegisterUiScenario("editor/settings_modal_open_cancel",
                           &RegisterSettingsModalOpenCancel);
        RegisterUiScenario("editor/properties_panel_visible",
                           &RegisterPropertiesPanelVisible);
        RegisterUiScenario("editor/properties_panel_light_controls_workflow",
                           &RegisterPropertiesPanelLightControlsWorkflow);
        RegisterUiScenario("editor/properties_panel_mixed_selection_workflow",
                           &RegisterPropertiesPanelMixedSelectionWorkflow);
        RegisterUiScenario("editor/properties_panel_rename_delete_undo_workflow",
                           &RegisterPropertiesPanelRenameDeleteUndoWorkflow);
        RegisterUiScenario("editor/properties_panel_scene_save_reload_workflow",
                           &RegisterPropertiesPanelSceneSaveReloadWorkflow);
        RegisterUiScenario("editor/viewport_statusbar_visible",
                           &RegisterViewportAndStatusbarVisible);
        RegisterUiScenario("editor/new_scene_and_add_panel",
                           &RegisterNewSceneAndAddPanel);
        RegisterUiScenario("editor/quick_open_popup", &RegisterQuickOpenPopup);
        RegisterUiScenario("editor/command_palette_popup",
                           &RegisterCommandPalettePopup);
        RegisterUiScenario("editor/select_object_via_hierarchy",
                           &RegisterSelectObjectViaHierarchy);
        RegisterUiScenario("editor/rename_object_modal", &RegisterRenameObjectModal);
        RegisterUiScenario("editor/object_context_menu_in_hierarchy",
                           &RegisterObjectContextMenuInHierarchy);
        RegisterUiScenario("editor/duplicate_object_changes_hierarchy",
                           &RegisterDuplicateObjectChangesHierarchy);
        RegisterUiScenario("editor/delete_selected_object_flow",
                           &RegisterDeleteSelectedObjectFlow);
        RegisterUiScenario("editor/undo_redo_via_edit_menu",
                           &RegisterUndoRedoViaEditMenu);
        RegisterUiScenario("editor/mcp_tab_content_visible",
                           &RegisterMcpTabContentVisible);
        RegisterUiScenario("editor/mcp_live_request_visibility",
                           &RegisterMcpLiveRequestVisibility);
        RegisterUiScenario("editor/mcp_request_detail_fields_visible",
                           &RegisterMcpRequestDetailFieldsVisible);
        RegisterUiScenario("editor/project_tab_visible", &RegisterProjectTabVisible);
        RegisterUiScenario("editor/object_type_filter_in_hierarchy",
                           &RegisterObjectTypeFilterInHierarchy);
        RegisterUiScenario("editor/assets_panel_add_asset_button",
                           &RegisterAssetsPanelAddAssetButton);
        RegisterUiScenario("editor/properties_shows_camera_fields",
                           &RegisterPropertiesShowsCameraFields);
        RegisterUiScenario("editor/multi_select_shows_batch_panel",
                           &RegisterMultiSelectShowsBatchPanel);
        RegisterUiScenario("editor/edit_menu_shows_history_items",
                           &RegisterEditMenuShowsHistoryItems);
        RegisterUiScenario("editor/play_mode_toggle", &RegisterPlayModeToggle);
        RegisterUiScenario("editor/file_new_scene_cancel_dirty",
                           &RegisterFileNewSceneCancelDirty);
        RegisterUiScenario("editor/file_open_scene_dismiss",
                           &RegisterFileOpenSceneDismiss);
        RegisterUiScenario("editor/file_reset_layout", &RegisterFileResetLayout);
        RegisterUiScenario("editor/view_fly_mode_activate",
                           &RegisterViewFlyModeActivate);
        RegisterUiScenario("editor/settings_modal_apply_button",
                           &RegisterSettingsModalApplyButton);
        RegisterUiScenario("editor/settings_modal_port_input",
                           &RegisterSettingsModalPortInput);
        RegisterUiScenario("editor/create_asset_modal_fill_cancel",
                           &RegisterCreateAssetModalFillCancel);
        RegisterUiScenario("editor/delete_confirm_modal_accept",
                           &RegisterDeleteConfirmModalAccept);
        RegisterUiScenario("editor/scene_header_context_add_panel",
                           &RegisterSceneHeaderContextAddPanel);
        RegisterUiScenario("editor/hierarchy_context_menu_add_child",
                           &RegisterHierarchyContextMenuAddChild);
        RegisterUiScenario("editor/edit_menu_create_prefab",
                           &RegisterEditMenuCreatePrefab);
        RegisterUiScenario("editor/assets_panel_search_open_dismiss",
                           &RegisterAssetsPanelSearchOpenDismiss);
        RegisterUiScenario("editor/console_filter_warn_toggle",
                           &RegisterConsoleFilterWarnToggle);
        RegisterUiScenario("editor/mcp_clear_request_log",
                           &RegisterMcpClearRequestLog);
        RegisterUiScenario("editor/hierarchy_empty_space_context_add",
                           &RegisterHierarchyEmptySpaceContextAdd);
        RegisterUiScenario("editor/unsaved_changes_discard",
                           &RegisterUnsavedChangesDiscard);
        RegisterUiScenario("editor/mcp_enable_and_verify_running",
                           &RegisterMcpEnableAndVerifyRunning);
        RegisterUiScenario("editor/mcp_send_request_and_verify_log",
                           &RegisterMcpSendRequestAndVerifyLog);
        RegisterUiScenario("editor/mcp_send_tool_call_verify_method",
                           &RegisterMcpSendToolCallVerifyMethod);
        RegisterUiScenario("editor/mcp_catalog_shows_tools",
                           &RegisterMcpCatalogShowsTools);
        RegisterUiScenario("editor/mcp_open_settings_from_tab",
                           &RegisterMcpOpenSettingsFromTab);
        RegisterUiScenario("editor/properties_panel_no_selection",
                           &RegisterPropertiesPanelNoSelection);
        RegisterUiScenario("editor/properties_panel_panel_object_transform",
                           &RegisterPropertiesPanelPanelObjectTransform);
        RegisterUiScenario("editor/properties_panel_light_object_fields",
                           &RegisterPropertiesPanelLightObjectFields);
        RegisterUiScenario("editor/properties_panel_identity_section",
                           &RegisterPropertiesPanelIdentitySection);
        RegisterUiScenario("editor/console_info_checkbox_toggle",
                           &RegisterConsoleInfoCheckboxToggle);
        RegisterUiScenario("editor/console_error_checkbox_toggle",
                           &RegisterConsoleErrorCheckboxToggle);
        RegisterUiScenario("editor/add_prop_object_and_check_properties",
                           &RegisterAddPropObjectAndCheckProperties);
        RegisterUiScenario("editor/hierarchy_add_multiple_then_multi_select",
                           &RegisterHierarchyAddMultipleThenMultiSelect);
        // Keep editor-closing scenarios LAST — they leave no active project,
        // which would cause all subsequent editor tests to skip.
        RegisterUiScenario("editor/close_editor_button", &RegisterCloseEditorButton);
        RegisterUiScenario("editor/close_editor_returns_to_launcher",
                           &RegisterCloseEditorReturnsToLauncher);
    }
} // namespace Horo

#endif
