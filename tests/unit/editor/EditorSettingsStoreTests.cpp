#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Foundation/DataBus.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

void SetHomeForTest(const std::filesystem::path &home)
{
#if defined(_WIN32)
    _putenv_s("USERPROFILE", home.string().c_str());
#else
    setenv("HOME", home.string().c_str(), 1);
#endif
}

void SaveAndLoadRoundTrip()
{
    using namespace Horo::Editor;

    const std::filesystem::path home = std::filesystem::temp_directory_path() / "horo_editor_settings_store_roundtrip";
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
    assert(SaveEditorSettingsDocument(&doc, &error));
    assert(error.empty());
    assert(doc.path == home / ".horo" / "editor_settings.json");
    assert(std::filesystem::is_regular_file(doc.path));

    const EditorSettingsDocument loaded = LoadEditorSettingsDocument();
    assert(loaded.loadedFromDisk);
    assert(!loaded.parseError);
    assert(loaded.settings.startupBehavior == EditorStartupBehavior::LastProject);
    assert(loaded.settings.themePreset == EditorThemePreset::Midnight);
    assert(loaded.settings.accentColorHex == "#112233");
    assert(loaded.settings.uiScalePercent == 125);
    assert(loaded.settings.codeFontSizePx == 16);
    assert(loaded.settings.uiFontFamily == "Avenir Next");
    assert(loaded.settings.codeFontFamily == "SF Mono");
    assert(loaded.settings.consoleLogLevel == EditorConsoleLogLevel::Debug);
    assert(loaded.settings.pluginDiscoveryPath == "{project}/addons");
}

void InvalidValuesAreRejectedOnSave()
{
    using namespace Horo::Editor;

    const std::filesystem::path home = std::filesystem::temp_directory_path() / "horo_editor_settings_store_invalid";
    std::filesystem::remove_all(home);
    SetHomeForTest(home);

    EditorSettingsDocument doc;
    doc.settings = DefaultEditorSettings();
    doc.settings.accentColorHex = "not-a-color";

    std::string error;
    assert(!SaveEditorSettingsDocument(&doc, &error));
    assert(!error.empty());
    assert(doc.settings.accentColorHex == "#04A5FC");
}

void MalformedFileFallsBackToDefaults()
{
    using namespace Horo::Editor;

    const std::filesystem::path home = std::filesystem::temp_directory_path() / "horo_editor_settings_store_malformed";
    std::filesystem::remove_all(home);
    SetHomeForTest(home);
    std::filesystem::create_directories(home / ".horo");
    {
        std::ofstream out(home / ".horo" / "editor_settings.json");
        out << "not json";
    }

    const EditorSettingsDocument loaded = LoadEditorSettingsDocument();
    assert(loaded.loadedFromDisk);
    assert(loaded.parseError);
    assert(loaded.settings == DefaultEditorSettings());
}

void PersistedAppearanceApplyCommitsOneAtomicConfigurationRevision()
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
    assert(SaveEditorSettingsDocument(&persisted, &error));

    const EditorSettingsDocument applied = LoadEditorSettingsDocument();
    EngineDataBus events{EngineDataBusConfig{.traceDispatch = false}};
    ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings(), &events);
    const ConfigurationSnapshot before = configuration.Snapshot();
    ConfigurationChangedEvent observed{};
    const Subscription subscription = events.Subscribe<ConfigurationChangedEvent>([&observed](const ConfigurationChangedEvent &event) { observed = event; });

    const ConfigurationDraft draft = MakeEditorAppearanceConfigurationDraft(before, applied.settings);
    assert(configuration.Commit(draft).HasValue());

    const ConfigurationSnapshot after = configuration.Snapshot();
    assert(before.Revision() == 0);
    assert(after.Revision() == 1);
    assert(std::get<std::string>(before.Get(SettingKey{"editor.theme.active"})) == "horo_dark");
    assert(std::get<std::string>(before.Get(SettingKey{"editor.appearance.accent_color"})) == "#04A5FC");
    assert(std::get<std::int64_t>(before.Get(SettingKey{"editor.appearance.ui_scale_percent"})) == 100);
    assert(std::get<std::int64_t>(before.Get(SettingKey{"editor.appearance.code_font_size_px"})) == 13);
    assert(std::get<std::string>(after.Get(SettingKey{"editor.theme.active"})) == "light");
    assert(std::get<std::string>(after.Get(SettingKey{"editor.appearance.accent_color"})) == "#112233");
    assert(std::get<std::int64_t>(after.Get(SettingKey{"editor.appearance.ui_scale_percent"})) == 125);
    assert(std::get<std::int64_t>(after.Get(SettingKey{"editor.appearance.code_font_size_px"})) == 16);
    assert(observed.revision == after.Revision());
    assert(observed.changedKeys.size() == 4);
    assert(std::any_of(observed.changedKeys.begin(), observed.changedKeys.end(), [](const SettingKey &key) { return key.Value() == "editor.theme.active"; }));
    assert(std::any_of(observed.changedKeys.begin(), observed.changedKeys.end(), [](const SettingKey &key) { return key.Value() == "editor.appearance.accent_color"; }));
    assert(std::any_of(observed.changedKeys.begin(), observed.changedKeys.end(), [](const SettingKey &key) { return key.Value() == "editor.appearance.ui_scale_percent"; }));
    assert(std::any_of(observed.changedKeys.begin(), observed.changedKeys.end(), [](const SettingKey &key) { return key.Value() == "editor.appearance.code_font_size_px"; }));
}

void FailedOrStaleAuthorityCommitRetainsCommittedSnapshotAndDoesNotPublish()
{
    using namespace Horo;
    using namespace Horo::Editor;

    const std::filesystem::path home = std::filesystem::temp_directory_path() / "horo_editor_settings_authority_failure";
    std::filesystem::remove_all(home);
    SetHomeForTest(home);

    const EditorSettings initial = DefaultEditorSettings();
    ConfigurationService configuration = CreateEditorConfigurationService(initial);
    EditorDataBus events;
    LocalizationService localization{LocaleTag{"en-US"}};
    EditorSettingsService settings{initial, configuration, events, localization};
    int committedEventCount = 0;
    const Subscription subscription = events.Subscribe<EditorSettingsChangedEvent>([&committedEventCount](const EditorSettingsChangedEvent &event) {
        if (event.phase == SettingsChangePhase::Committed)
        {
            ++committedEventCount;
        }
    });

    EditorSettings invalid = initial;
    invalid.accentColorHex = "invalid";
    assert(settings.Commit(EditorSettingsDraft{.baseRevision = 0, .settings = invalid}).HasError());
    assert(settings.Snapshot().revision == 0);
    assert(settings.Snapshot().settings == initial);
    assert(committedEventCount == 0);

    EditorSettings next = initial;
    next.themePreset = EditorThemePreset::Midnight;
    assert(settings.Commit(EditorSettingsDraft{.baseRevision = 0, .settings = next}).HasValue());
    const EditorSettingsSnapshot committed = settings.Snapshot();
    assert(committed.revision == 1);
    assert(committed.settings == next);
    assert(committedEventCount == 1);

    EditorSettings stale = initial;
    stale.themePreset = EditorThemePreset::Light;
    assert(settings.Commit(EditorSettingsDraft{.baseRevision = 0, .settings = stale}).HasError());
    assert(settings.Snapshot().revision == committed.revision);
    assert(settings.Snapshot().settings == committed.settings);
    assert(committedEventCount == 1);
}

void SuccessfulAuthorityCommitPersistsActivatesAndPublishesOneCommittedEvent()
{
    using namespace Horo;
    using namespace Horo::Editor;

    const std::filesystem::path home = std::filesystem::temp_directory_path() / "horo_editor_settings_authority_success";
    std::filesystem::remove_all(home);
    SetHomeForTest(home);

    const EditorSettings initial = DefaultEditorSettings();
    ConfigurationService configuration = CreateEditorConfigurationService(initial);
    EditorDataBus events;
    LocalizationService localization{LocaleTag{"en-US"}};
    EditorSettingsService settings{initial, configuration, events, localization};
    int eventCount = 0;
    EditorSettingsChangedEvent observed{};
    const Subscription subscription = events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
        ++eventCount;
        observed = event;
    });

    EditorSettings draftSettings = initial;
    draftSettings.themePreset = EditorThemePreset::Light;
    draftSettings.accentColorHex = "#112233";
    const auto result = settings.Commit(EditorSettingsDraft{.baseRevision = settings.Snapshot().revision, .settings = draftSettings});

    assert(result.HasValue());
    assert(result.Value().revision == 1);
    assert(settings.Snapshot() == result.Value());
    assert(configuration.Snapshot().Revision() == 1);
    assert(std::get<std::string>(configuration.Snapshot().Get(SettingKey{"editor.theme.active"})) == "light");
    assert(LoadEditorSettingsDocument().settings == draftSettings);
    assert(eventCount == 1);
    assert(observed.revision == 1);
    assert(observed.phase == SettingsChangePhase::Committed);
    assert(observed.changedDomains == SettingsDomain::All);
}

} // namespace

int main()
{
    SaveAndLoadRoundTrip();
    InvalidValuesAreRejectedOnSave();
    MalformedFileFallsBackToDefaults();
    PersistedAppearanceApplyCommitsOneAtomicConfigurationRevision();
    FailedOrStaleAuthorityCommitRetainsCommittedSnapshotAndDoesNotPublish();
    SuccessfulAuthorityCommitPersistsActivatesAndPublishesOneCommittedEvent();
    return 0;
}
