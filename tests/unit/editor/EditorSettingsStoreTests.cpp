#include "Horo/Editor/EditorSettingsStore.h"

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

} // namespace

int main()
{
    SaveAndLoadRoundTrip();
    InvalidValuesAreRejectedOnSave();
    MalformedFileFallsBackToDefaults();
    return 0;
}
