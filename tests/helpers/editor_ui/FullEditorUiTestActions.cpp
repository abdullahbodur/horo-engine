#include "FullEditorUiTestActions.h"

#include "FullEditorUiTestHost.h"
#include "Horo/Editor/EditorMenuModel.h"
#include "Horo/Editor/GuiScreenHost.h"

#include <imgui_test_engine/imgui_te_context.h>
#include <string>

namespace Horo::Tests::FullEditorActions {
    void CreateRootBox(UiScenarioPipe &pipeline) {
        pipeline.Step("Create a Box from the hierarchy root menu", [](ImGuiTestContext &ui) {
            ui.ItemClick("**/##HierarchyRootDrop", ImGuiMouseButton_Right);
            ui.SetRef("//$FOCUSED");
            ui.MenuClick("###hierarchy_create_root/"
                         "###hierarchy_create_workspace.create.group.objects_3d/"
                         "###hierarchy_create_primitive.mesh.box");
        });
        pipeline.Step("Observe the created object across the workspace", [](ImGuiTestContext &ui) {
            IM_CHECK(ui.ItemInfo("//**/##hierarchy_object_row").ID != 0);
        });
    }

    void ExerciseHierarchyEdits(UiScenarioPipe &pipeline) {
        pipeline.Step("Duplicate the authored hierarchy object", [](ImGuiTestContext &ui) {
            ui.ItemClick("//**/##hierarchy_object_row", ImGuiMouseButton_Right);
            ui.SetRef("//$FOCUSED");
            ui.MenuClick("Duplicate");
            ui.Yield();
        });
        pipeline.Step("Rename a hierarchy object", [](ImGuiTestContext &ui) {
            ui.ItemClick("//**/##hierarchy_object_row", ImGuiMouseButton_Right);
            ui.SetRef("//$FOCUSED");
            ui.MenuClick("Rename");
            ui.Yield();
            ui.ItemInputValue("//**/##Rename", "RenamedBox");
            ui.KeyPress(ImGuiKey_Enter);
            ui.Yield();
        });
        pipeline.Step("Delete a hierarchy object", [](ImGuiTestContext &ui) {
            ui.ItemClick("//**/##hierarchy_object_row", ImGuiMouseButton_Right);
            ui.SetRef("//$FOCUSED");
            ui.MenuClick("Delete");
            ui.Yield();
        });
    }

    void SelectOrthographicProjection(UiScenarioPipe &pipeline, FullEditorUiTestHost &editor) {
        pipeline.Step("Choose Orthographic projection", [](ImGuiTestContext &ui) {
            ui.ItemClick("//**/Combo###viewport_projection");
            ui.ItemClick("//**/###combo_option_1");
        });
        pipeline.Step("Observe projection through the shared viewport handoff", [&editor](ImGuiTestContext &) {
            IM_CHECK(editor.ViewportProjection() == Runtime::CameraProjection::Orthographic);
        });
    }

    namespace {
        void AddInputMappingStep(UiScenarioPipe &pipeline) {
            pipeline.Step("Exercise input mapping pages and profile actions", [](ImGuiTestContext &ui) {
                ui.ItemClick("//**/horo.input_mapping/##ActivityItem");
                ui.Yield();
                IM_CHECK(ui.ItemExists("//**/Action Maps"));
                IM_CHECK(ui.ItemExists("//**/Rebind"));
                ui.ItemClick("//**/Rebind");
                ui.Yield();
                ui.KeyPress(ImGuiKey_K);
                ui.Yield();
                ui.ItemClick("//**/Devices");
                ui.Yield();
                ui.ItemClick("//**/Profiles");
                ui.Yield();
                IM_CHECK(ui.ItemExists("//**/Save Profile"));
                ui.ItemClick("//**/Save Profile");
                ui.ItemClick("//**/Save Project Override");
            });
        }

        void AddGlobalDockSteps(UiScenarioPipe &pipeline) {
            pipeline.Step("Exercise every global dock pane", [](ImGuiTestContext &ui) {
                if (!ui.ItemExists("//**/Assets")) {
                    ui.ItemClick("//**/horo.global_dock/##ActivityItem");
                    ui.Yield();
                }
                constexpr const char *tabs[]{"Console", "Build", "Ops", "MCP", "Perf", "Physics", "Audio", "Net", "L10n", "Assets"};
                for (const char *const tab : tabs) {
                    ui.ItemClick(("//**/" + std::string{tab}).c_str());
                    ui.Yield();
                }
            });
            pipeline.Step("Create an asset-browser folder", [](ImGuiTestContext &ui) {
                ImGuiTestItemInfo dock = ui.WindowInfo("//##DockBottom", ImGuiTestOpFlags_NoError);
                if (dock.Window == nullptr)
                    dock = ui.WindowInfo("//##DockBottomLeft", ImGuiTestOpFlags_NoError);
                if (dock.Window == nullptr)
                    dock = ui.WindowInfo("//##DockBottomRight", ImGuiTestOpFlags_NoError);
                IM_CHECK(dock.Window != nullptr);
                ui.MouseMoveToPos({dock.RectClipped.GetCenter().x, dock.RectClipped.Max.y - 8.0F});
                ui.MouseClick(ImGuiMouseButton_Right);
                ui.ItemClick("//**/Create Folder");
                ui.Yield();
                ui.ItemInputValue("//**/##ContentBrowserCreateFolderInput", "CoverageFolder");
                ui.ItemClick("//**/Create Folder");
                ui.Yield();
            });
        }

        void AddSettingsStep(UiScenarioPipe &pipeline, FullEditorUiTestHost &editor) {
            pipeline.Step("Exercise editor settings sections", [&editor](ImGuiTestContext &ui) {
                editor.Screens().DispatchMenuInvocation(
                    Editor::EditorMenuInvocation{Editor::EditorMenuAction::OpenEditorSettings, std::nullopt});
                ui.Yield();
                IM_CHECK(ui.ItemExists("//**/###settings_apply"));
                constexpr const char *sections[]{"Appearance", "Input", "Rendering", "Audio", "Network", "Diagnostics", "Extensions"};
                for (const char *const section : sections) {
                    ui.ItemClick(("//**/" + std::string{section} + "/nav").c_str());
                    ui.Yield();
                }
                ui.ItemClick("//**/###settings_cancel");
            });
        }
    }  // namespace

    void ExerciseWorkspacePanels(UiScenarioPipe &pipeline, FullEditorUiTestHost &editor) {
        AddInputMappingStep(pipeline);
        AddGlobalDockSteps(pipeline);
        AddSettingsStep(pipeline, editor);
    }
}  // namespace Horo::Tests::FullEditorActions
