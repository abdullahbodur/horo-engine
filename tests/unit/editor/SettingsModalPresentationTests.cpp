#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Editor/SettingsModal.h"
#include "Horo/Foundation/DataBus.h"
#include "helpers/editor_ui/HeadlessEditorGuiFixture.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Settings presentation renders every settings category without mutating the authority", "[unit][editor][gui][settings]") {
    using namespace Horo;
    using namespace Horo::Editor;

    Tests::HeadlessEditorGuiFixture imgui;
    EngineDataBus engineEvents;
    EditorDataBus editorEvents;
    LocalizationService localization{LocaleTag{"en-US"}};
    ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings());
    EditorSettingsService settings{DefaultEditorSettings(), configuration, editorEvents, localization};
    const ThemeContext theme{imgui.Fonts()};
    const EditorSettingsSnapshot snapshot = settings.Snapshot();
    const EditorGuiContext context{engineEvents, editorEvents, localization, theme, snapshot};
    SettingsModal modal{context, settings, 0};
    LoadSettingsForModal(modal.Draft(), settings);
    const EditorSettings committed = modal.Draft().committed;

    for (int activeTab = 0; activeTab < 8; ++activeTab) {
        DYNAMIC_SECTION("settings category " << activeTab) {
            modal.Draft().activeTab = activeTab;
            imgui.BeginFrame();
            const ModalFrameResult result = modal.Draw();
            imgui.EndFrame();

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

    Tests::HeadlessEditorGuiFixture imgui;
    EngineDataBus engineEvents;
    EditorDataBus editorEvents;
    LocalizationService localization{LocaleTag{"en-US"}};
    ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings());
    EditorSettingsService settings{DefaultEditorSettings(), configuration, editorEvents, localization};
    const ThemeContext theme{imgui.Fonts()};
    const EditorSettingsSnapshot snapshot = settings.Snapshot();
    const EditorGuiContext context{engineEvents, editorEvents, localization, theme, snapshot};
    SettingsModal modal{context, settings, 0};
    LoadSettingsForModal(modal.Draft(), settings);
    modal.Draft().appearance.pendingThemeIndex = 0;
    modal.Draft().activeTab = 1;

    imgui.BeginFrame();
    static_cast<void>(modal.Draw());
    imgui.EndFrame();

    REQUIRE(modal.Draft().appearance.pendingThemeIndex == -1);
}
