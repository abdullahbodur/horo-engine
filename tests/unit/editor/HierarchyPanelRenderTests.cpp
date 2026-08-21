#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "Horo/Foundation/DataBus.h"
#include "editor/screens/workspace/panels/hierarchy/HierarchyPanel.h"

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {
    class TestLocalization final : public Horo::Editor::ILocalizationService {
    public:
        [[nodiscard]] const std::string &Get(const std::string_view, const std::string_view localKey) const override {
            const auto [entry, inserted] = values_.try_emplace(std::string(localKey), localKey);
            static_cast<void>(inserted);
            return entry->second;
        }

    private:
        mutable std::unordered_map<std::string, std::string> values_;
    };
}  // namespace

TEST_CASE("Hierarchy Panel Render Tests", "[unit][editor]") {
    using namespace Horo;
    using namespace Horo::Editor;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(640.0F, 480.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());

    EngineDataBus engineEvents;
    EditorDataBus editorEvents;
    TestLocalization localization;
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{.sans = defaultFont, .sansCompact = defaultFont, .sansEmphasis = defaultFont};
    const ThemeContext theme{.fonts = fonts};
    const EditorSettingsSnapshot settings{};
    const EditorGuiContext context{.engineEvents = engineEvents,
                                   .editorEvents = editorEvents,
                                   .localization = localization,
                                   .theme = theme,
                                   .settings = settings};
    EditorWorkspaceViewModel viewModel;
    viewModel.documentRevision = DocumentRevision{1};
    SceneObject root{.id = SceneObjectId{1}, .name = "Root"};
    root.editorState.locked = true;
    root.effectivelyLocked = true;
    SceneObject mesh{.id = SceneObjectId{2}, .parent = SceneObjectId{1}, .name = "Box", .kind = SceneObjectKind::Mesh};
    mesh.effectivelyLocked = true;
    mesh.lockedByParent = true;
    SceneObject directional{.id = SceneObjectId{3}, .name = "Directional Light"};
    directional.components.light = Runtime::LightComponent{.kind = Runtime::LightKind::Directional};
    SceneObject camera{.id = SceneObjectId{4}, .name = "Camera"};
    camera.components.camera = Runtime::CameraComponent{};
    SceneObject audio{.id = SceneObjectId{5}, .name = "Audio"};
    audio.components.audioSource = Runtime::AudioSourceComponent{};
    audio.editorState.visible = false;
    audio.effectivelyVisible = false;
    SceneObject longChild{.id = SceneObjectId{6},
                          .parent = SceneObjectId{1},
                          .name = "A very long nested hierarchy object name that must truncate before row actions"};
    SceneObject spot{.id = SceneObjectId{7}, .name = "Light"};
    spot.components.light = Runtime::LightComponent{.kind = Runtime::LightKind::Spot};
    SceneObject duplicateName{.id = SceneObjectId{8}, .parent = SceneObjectId{6}, .name = "Light"};
    duplicateName.components.light = Runtime::LightComponent{.kind = Runtime::LightKind::Point};
    viewModel.objects = {root, mesh, directional, camera, audio, longChild, spot, duplicateName};
    viewModel.primarySelection = SceneObjectId{3};
    viewModel.selectedObjects = {SceneObjectId{2}, SceneObjectId{3}};
    EditorWorkspaceViewCommandData command;
    HierarchyPanel panel;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(280.0F, 420.0F));
    ImGui::Begin("HierarchyRenderTest", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    panel.DrawPanel(ImGui::GetCursorScreenPos(), ImVec2(260.0F, 380.0F), viewModel, command, context);
    ImGui::End();
    ImGui::Render();

    ImGui::DestroyContext();
}
