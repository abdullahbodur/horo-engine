#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Foundation/DataBus.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    void SetHomeForTest(const std::filesystem::path& home)
    {
#if defined(_WIN32)
        _putenv_s("USERPROFILE", home.string().c_str());
#else
        setenv("HOME", home.string().c_str(), 1);
#endif
    }

    TEST_CASE("Save And Load Round Trip", "[unit][editor]")
    {
        using namespace Horo::Editor;

        const std::filesystem::path home = std::filesystem::temp_directory_path() /
            "horo_editor_settings_store_roundtrip";
        std::filesystem::remove_all(home);
        SetHomeForTest(home);

        EditorSettingsDocument doc;
        doc.settings = DefaultEditorSettings();
        doc.settings.startupBehavior = EditorStartupBehavior::LastProject;
        doc.settings.themePreset = EditorThemePreset::Midnight;
        doc.settings.accentColorHex = "#112233";
        doc.settings.uiScalePercent = 125;
        doc.settings.codeFontSizePx = 16;
        doc.settings.uiFontFamily = "Avenir Next";
        doc.settings.codeFontFamily = "SF Mono";
        doc.settings.consoleLogLevel = EditorConsoleLogLevel::Debug;
        doc.settings.pluginDiscoveryPath = "{project}/addons";

        std::string error;
        REQUIRE((SaveEditorSettingsDocument(&doc, &error)));
        REQUIRE((error.empty()));
        REQUIRE((doc.path == home / ".horo" / "editor_settings.json"));
        REQUIRE((std::filesystem::is_regular_file(doc.path)));

        const EditorSettingsDocument loaded = LoadEditorSettingsDocument();
        REQUIRE((loaded.loadedFromDisk));
        REQUIRE((!loaded.parseError));
        REQUIRE((loaded.settings.startupBehavior == EditorStartupBehavior::LastProject));
        REQUIRE((loaded.settings.themePreset == EditorThemePreset::Midnight));
        REQUIRE((loaded.settings.accentColorHex == "#112233"));
        REQUIRE((loaded.settings.uiScalePercent == 125));
        REQUIRE((loaded.settings.codeFontSizePx == 16));
        REQUIRE((loaded.settings.uiFontFamily == "Avenir Next"));
        REQUIRE((loaded.settings.codeFontFamily == "SF Mono"));
        REQUIRE((loaded.settings.consoleLogLevel == EditorConsoleLogLevel::Debug));
        REQUIRE((loaded.settings.pluginDiscoveryPath == "{project}/addons"));
    }

    TEST_CASE("Invalid Values Are Rejected On Save", "[unit][editor]")
    {
        using namespace Horo::Editor;

        const std::filesystem::path home = std::filesystem::temp_directory_path() /
            "horo_editor_settings_store_invalid";
        std::filesystem::remove_all(home);
        SetHomeForTest(home);

        EditorSettingsDocument doc;
        doc.settings = DefaultEditorSettings();
        doc.settings.accentColorHex = "not-a-color";

        std::string error;
        REQUIRE((!SaveEditorSettingsDocument(&doc, &error)));
        REQUIRE((!error.empty()));
        REQUIRE((doc.settings.accentColorHex == "#04A5FC"));
    }

    TEST_CASE("Malformed File Falls Back To Defaults", "[unit][editor]")
    {
        using namespace Horo::Editor;

        const std::filesystem::path home = std::filesystem::temp_directory_path() /
            "horo_editor_settings_store_malformed";
        std::filesystem::remove_all(home);
        SetHomeForTest(home);
        std::filesystem::create_directories(home / ".horo");
        {
            std::ofstream out(home / ".horo" / "editor_settings.json");
            out << "not json";
        }

        const EditorSettingsDocument loaded = LoadEditorSettingsDocument();
        REQUIRE((loaded.loadedFromDisk));
        REQUIRE((loaded.parseError));
        REQUIRE((loaded.settings == DefaultEditorSettings()));
    }

    TEST_CASE("Persisted Appearance Apply Commits One Atomic Configuration Revision", "[unit][editor]")
    {
        using namespace Horo;
        using namespace Horo::Editor;

        const std::filesystem::path home = std::filesystem::temp_directory_path() / "horo_editor_configuration_apply";
        std::filesystem::remove_all(home);
        SetHomeForTest(home);

        EditorSettingsDocument persisted;
        persisted.settings = DefaultEditorSettings();
        persisted.settings.themePreset = EditorThemePreset::Light;
        persisted.settings.accentColorHex = "#112233";
        persisted.settings.uiScalePercent = 125;
        persisted.settings.codeFontSizePx = 16;
        std::string error;
        REQUIRE((SaveEditorSettingsDocument(&persisted, &error)));

        const EditorSettingsDocument applied = LoadEditorSettingsDocument();
        EngineDataBus events{EngineDataBusConfig{.traceDispatch = false}};
        ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings(), &events);
        const ConfigurationSnapshot before = configuration.Snapshot();
        ConfigurationChangedEvent observed{};
        const Subscription subscription = events.Subscribe<ConfigurationChangedEvent>(
            [&observed](const ConfigurationChangedEvent& event) { observed = event; });

        const ConfigurationDraft draft = MakeEditorAppearanceConfigurationDraft(before, applied.settings);
        REQUIRE((configuration.Commit(draft).HasValue()));

        const ConfigurationSnapshot after = configuration.Snapshot();
        REQUIRE((before.Revision() == 0));
        REQUIRE((after.Revision() == 1));
        REQUIRE((std::get<std::string>(before.Get(SettingKey{"editor.theme.active"})) == "horo_dark"));
        REQUIRE((std::get<std::string>(before.Get(SettingKey{"editor.appearance.accent_color"})) == "#04A5FC"));
        REQUIRE((std::get<std::int64_t>(before.Get(SettingKey{"editor.appearance.ui_scale_percent"})) == 100));
        REQUIRE((std::get<std::int64_t>(before.Get(SettingKey{"editor.appearance.code_font_size_px"})) == 13));
        REQUIRE((std::get<std::string>(after.Get(SettingKey{"editor.theme.active"})) == "light"));
        REQUIRE((std::get<std::string>(after.Get(SettingKey{"editor.appearance.accent_color"})) == "#112233"));
        REQUIRE((std::get<std::int64_t>(after.Get(SettingKey{"editor.appearance.ui_scale_percent"})) == 125));
        REQUIRE((std::get<std::int64_t>(after.Get(SettingKey{"editor.appearance.code_font_size_px"})) == 16));
        REQUIRE((observed.revision == after.Revision()));
        REQUIRE((observed.changedKeys.size() == 4));
        REQUIRE((std::any_of(observed.changedKeys.begin(), observed.changedKeys.end(),
            [](const SettingKey &key) { return key.Value() == "editor.theme.active"; })));
        REQUIRE((std::any_of(observed.changedKeys.begin(), observed.changedKeys.end(),
            [](const SettingKey &key) { return key.Value() == "editor.appearance.accent_color"; })));
        REQUIRE((std::any_of(observed.changedKeys.begin(), observed.changedKeys.end(),
            [](const SettingKey &key) { return key.Value() == "editor.appearance.ui_scale_percent"; })));
        REQUIRE((std::any_of(observed.changedKeys.begin(), observed.changedKeys.end(),
            [](const SettingKey &key) { return key.Value() == "editor.appearance.code_font_size_px"; })));
    }

    TEST_CASE("Failed Or Stale Authority Commit Retains Committed Snapshot And Does Not Publish", "[unit][editor]")
    {
        using namespace Horo;
        using namespace Horo::Editor;

        const std::filesystem::path home =
            std::filesystem::temp_directory_path() / "horo_editor_settings_authority_failure";
        std::filesystem::remove_all(home);
        SetHomeForTest(home);

        const EditorSettings initial = DefaultEditorSettings();
        ConfigurationService configuration = CreateEditorConfigurationService(initial);
        EditorDataBus events;
        LocalizationService localization{LocaleTag{"en-US"}};
        EditorSettingsService settings{initial, configuration, events, localization};
        int committedEventCount = 0;
        const Subscription subscription =
            events.Subscribe<EditorSettingsChangedEvent>([&committedEventCount](const EditorSettingsChangedEvent& event)
            {
                if (event.phase == SettingsChangePhase::Committed)
                {
                    ++committedEventCount;
                }
            });

        EditorSettings invalid = initial;
        invalid.accentColorHex = "invalid";
        REQUIRE((settings.Commit(EditorSettingsDraft{.baseRevision = 0, .settings = invalid}).HasError()));
        REQUIRE((settings.Snapshot().revision == 0));
        REQUIRE((settings.Snapshot().settings == initial));
        REQUIRE((committedEventCount == 0));

        EditorSettings next = initial;
        next.themePreset = EditorThemePreset::Midnight;
        REQUIRE((settings.Commit(EditorSettingsDraft{.baseRevision = 0, .settings = next}).HasValue()));
        const EditorSettingsSnapshot committed = settings.Snapshot();
        REQUIRE((committed.revision == 1));
        REQUIRE((committed.settings == next));
        REQUIRE((committedEventCount == 1));

        EditorSettings stale = initial;
        stale.themePreset = EditorThemePreset::Light;
        REQUIRE((settings.Commit(EditorSettingsDraft{.baseRevision = 0, .settings = stale}).HasError()));
        REQUIRE((settings.Snapshot().revision == committed.revision));
        REQUIRE((settings.Snapshot().settings == committed.settings));
        REQUIRE((committedEventCount == 1));
    }

    TEST_CASE("Successful Authority Commit Persists Activates And Publishes One Committed Event", "[unit][editor]")
    {
        using namespace Horo;
        using namespace Horo::Editor;

        const std::filesystem::path home =
            std::filesystem::temp_directory_path() / "horo_editor_settings_authority_success";
        std::filesystem::remove_all(home);
        SetHomeForTest(home);

        const EditorSettings initial = DefaultEditorSettings();
        ConfigurationService configuration = CreateEditorConfigurationService(initial);
        EditorDataBus events;
        LocalizationService localization{LocaleTag{"en-US"}};
        EditorSettingsService settings{initial, configuration, events, localization};
        int eventCount = 0;
        EditorSettingsChangedEvent observed{};
        const Subscription subscription =
            events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent& event)
            {
                ++eventCount;
                observed = event;
            });

        EditorSettings draftSettings = initial;
        draftSettings.themePreset = EditorThemePreset::Light;
        draftSettings.accentColorHex = "#112233";
        const auto result =
            settings.Commit(
                EditorSettingsDraft{.baseRevision = settings.Snapshot().revision, .settings = draftSettings});

        REQUIRE((result.HasValue()));
        REQUIRE((result.Value().revision == 1));
        REQUIRE((settings.Snapshot() == result.Value()));
        REQUIRE((configuration.Snapshot().Revision() == 1));
        REQUIRE((std::get<std::string>(configuration.Snapshot().Get(SettingKey{"editor.theme.active"})) == "light"));
        REQUIRE((LoadEditorSettingsDocument().settings == draftSettings));
        REQUIRE((eventCount == 1));
        REQUIRE((observed.revision == 1));
        REQUIRE((observed.phase == SettingsChangePhase::Committed));
        REQUIRE((observed.changedDomains == SettingsDomain::All));
    }
} // namespace
