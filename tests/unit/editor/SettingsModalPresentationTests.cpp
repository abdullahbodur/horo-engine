#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/SettingsModal.h"
#include "helpers/editor_ui/HeadlessEditorGuiFixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    struct SettingsPresentationFixture {
        SettingsPresentationFixture()
            : settings{Horo::Editor::DefaultEditorSettings(), configuration, gui.editorEvents, gui.localization},
              modal{gui.context, settings, 0} {
            Horo::Editor::LoadSettingsForModal(modal.Draft(), settings);
        }

        Horo::Editor::Tests::EditorGuiContextFixture gui;
        Horo::ConfigurationService configuration = Horo::Editor::CreateEditorConfigurationService(Horo::Editor::DefaultEditorSettings());
        Horo::Editor::EditorSettingsService settings;
        Horo::Editor::SettingsModal modal;
    };
}  // namespace

TEST_CASE("Settings presentation renders every settings category without mutating the authority", "[unit][editor][gui][settings]") {
    using namespace Horo;
    using namespace Horo::Editor;

    SettingsPresentationFixture fixture;
    const EditorSettings committed = fixture.modal.Draft().committed;

    for (int activeTab = 0; activeTab < 8; ++activeTab) {
        DYNAMIC_SECTION("settings category " << activeTab) {
            fixture.modal.Draft().activeTab = activeTab;
            fixture.gui.imgui.BeginFrame();
            const ModalFrameResult result = fixture.modal.Draw();
            fixture.gui.imgui.EndFrame();

            REQUIRE_FALSE(result.CloseRequest().has_value());
            REQUIRE(fixture.modal.Draft().activeTab == activeTab);
            REQUIRE(CollectDraftSettings(fixture.modal.Draft()) == committed);
            REQUIRE_FALSE(fixture.modal.Draft().dirty);
        }
    }
}

TEST_CASE("Settings presentation consumes a deferred theme selection before rendering", "[unit][editor][gui][settings]") {
    using namespace Horo;
    using namespace Horo::Editor;

    SettingsPresentationFixture fixture;
    fixture.modal.Draft().appearance.pendingThemeIndex = 0;
    fixture.modal.Draft().activeTab = 1;

    fixture.gui.imgui.BeginFrame();
    static_cast<void>(fixture.modal.Draw());
    fixture.gui.imgui.EndFrame();

    REQUIRE(fixture.modal.Draft().appearance.pendingThemeIndex == -1);
}
