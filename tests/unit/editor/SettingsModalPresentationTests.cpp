#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/SettingsModal.h"
#include "helpers/editor_ui/HeadlessEditorGuiFixture.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Settings presentation renders every settings category without mutating the authority", "[unit][editor][gui][settings]") {
    using namespace Horo;
    using namespace Horo::Editor;

    Tests::EditorGuiContextFixture fixture;
    ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings());
    EditorSettingsService settings{DefaultEditorSettings(), configuration, fixture.editorEvents, fixture.localization};
    SettingsModal modal{fixture.context, settings, 0};
    LoadSettingsForModal(modal.Draft(), settings);
    const EditorSettings committed = modal.Draft().committed;

    for (int activeTab = 0; activeTab < 8; ++activeTab) {
        DYNAMIC_SECTION("settings category " << activeTab) {
            modal.Draft().activeTab = activeTab;
            fixture.imgui.BeginFrame();
            const ModalFrameResult result = modal.Draw();
            fixture.imgui.EndFrame();

            REQUIRE_FALSE(result.CloseRequest().has_value());
            REQUIRE(modal.Draft().activeTab == activeTab);
            REQUIRE(CollectDraftSettings(modal.Draft()) == committed);
            REQUIRE_FALSE(modal.Draft().dirty);
        }
    }
}

TEST_CASE("Settings presentation consumes a deferred theme selection before rendering", "[unit][editor][gui][settings]") {
    using namespace Horo;
    using namespace Horo::Editor;

    Tests::EditorGuiContextFixture fixture;
    ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings());
    EditorSettingsService settings{DefaultEditorSettings(), configuration, fixture.editorEvents, fixture.localization};
    SettingsModal modal{fixture.context, settings, 0};
    LoadSettingsForModal(modal.Draft(), settings);
    modal.Draft().appearance.pendingThemeIndex = 0;
    modal.Draft().activeTab = 1;

    fixture.imgui.BeginFrame();
    static_cast<void>(modal.Draw());
    fixture.imgui.EndFrame();

    REQUIRE(modal.Draft().appearance.pendingThemeIndex == -1);
}
