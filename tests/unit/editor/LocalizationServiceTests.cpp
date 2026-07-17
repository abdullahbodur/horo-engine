#include "Horo/Editor/Localization/LocalizationService.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace
{
Horo::Editor::LocalizationCatalog Catalog(const char *locale, const char *text)
{
    Horo::Editor::LocalizationCatalog catalog{.locale = Horo::Editor::LocaleTag{locale}};
    catalog.messages.emplace(Horo::Editor::MessageKey{"editor", "settings.title"}, text);
    return catalog;
}

void LocaleTagsNormalizeAndRejectInvalidInput()
{
    const auto normalized = Horo::Editor::LocaleTag::Parse("tr-TR");
    assert(normalized.has_value());
    assert(normalized->value == "tr-TR");
    assert(!Horo::Editor::LocaleTag::Parse("not a locale").has_value());
}

void LocaleSwitchUsesImmutablePreparedSnapshot()
{
    Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
    assert(service.RegisterCatalog(Catalog("en-US", "Settings")));
    assert(service.RegisterCatalog(Catalog("tr-TR", "Ayarlar")));
    assert(service.Prepare(Horo::Editor::LocaleTag{"en-US"}));
    assert(service.ActivatePrepared());

    assert(service.Get("editor", "settings.title") == "Settings");
    const auto before = service.Snapshot();

    assert(service.Prepare(Horo::Editor::LocaleTag{"tr-TR"}));
    assert(service.Get("editor", "settings.title") == "Settings");
    assert(service.ActivatePrepared());
    assert(service.Get("editor", "settings.title") == "Ayarlar");
    assert(before.revision != service.Snapshot().revision);
}

void FailedPreparationLeavesActiveLocaleUnchanged()
{
    Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
    assert(service.RegisterCatalog(Catalog("en-US", "Settings")));
    assert(service.Prepare(Horo::Editor::LocaleTag{"en-US"}));
    assert(service.ActivatePrepared());

    Horo::Editor::LocalizationError error;
    assert(!service.Prepare(Horo::Editor::LocaleTag{"de-DE"}, &error));
    assert(error.code == "editor.localization.catalog_missing");
    assert(service.ActiveLocale().value == "en-US");
}
void MissingMessageDoesNotUseSourceFallback()
{
    Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
    assert(service.RegisterCatalog(Catalog("en-US", "Settings")));
    assert(service.Prepare(Horo::Editor::LocaleTag{"en-US"}));
    assert(service.ActivatePrepared());

    assert(service.Get("editor", "missing") == "[missing:editor:missing]");
}
void CatalogFileLoaderParsesResourceFormat()
{
    const auto path = std::filesystem::temp_directory_path() / "horo-localization-test.json";
    {
        std::ofstream output(path);
        output
            << R"({"schemaVersion":1,"messageFormat":"plain-text-1","locale":"tr-TR","namespace":"editor","messages":{"settings.title":{"text":"Ayarlar"}}})";
    }

    Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
    Horo::Editor::LocalizationError error;
    assert(service.LoadCatalogFile(path, &error));
    assert(service.Prepare(Horo::Editor::LocaleTag{"tr-TR"}, &error));
    assert(service.ActivatePrepared(&error));
    assert(service.Get("editor", "settings.title") == "Ayarlar");
    std::filesystem::remove(path);
}

void AssetLocalizationCatalogsContainRequiredEditorMessages()
{
    constexpr std::array globalDockKeys{
        "workspace.global_dock.tab.assets",
        "workspace.global_dock.tab.console",
        "workspace.global_dock.tab.mcp",
        "workspace.global_dock.tab.performance",
        "workspace.global_dock.tab.physics",
        "workspace.global_dock.tab.audio",
        "workspace.global_dock.tab.network",
        "workspace.global_dock.tab.localization",
        "workspace.global_dock.preview.console",
        "workspace.global_dock.preview.mcp",
        "workspace.global_dock.preview.performance",
        "workspace.global_dock.preview.physics",
        "workspace.global_dock.preview.audio",
        "workspace.global_dock.preview.network",
        "workspace.global_dock.preview.localization",
        "workspace.global_dock.console.session_started",
        "workspace.global_dock.console.texture_converted",
        "workspace.global_dock.console.scene_loaded",
        "workspace.global_dock.console.missing_animation",
        "workspace.global_dock.console.selection",
        "workspace.global_dock.mcp.bridge",
        "workspace.global_dock.mcp.tools",
        "workspace.global_dock.mcp.awaiting",
        "workspace.global_dock.performance.gpu",
        "workspace.global_dock.performance.cpu",
        "workspace.global_dock.performance.memory",
        "workspace.global_dock.physics.solver",
        "workspace.global_dock.physics.layers",
        "workspace.global_dock.physics.memory",
        "workspace.global_dock.audio.master",
        "workspace.global_dock.audio.busses",
        "workspace.global_dock.audio.device",
        "workspace.global_dock.network.ping",
        "workspace.global_dock.network.replication",
        "workspace.global_dock.network.connection",
        "workspace.global_dock.localization.locale",
        "workspace.global_dock.localization.strings",
        "workspace.global_dock.localization.fonts",
    };
    const auto assertGlobalDockKeysExist = [&globalDockKeys](const Horo::Editor::LocalizationService &service) {
        for (const char *key : globalDockKeys)
            assert(!service.Get("editor", key).starts_with("[missing:"));
    };

    const std::filesystem::path enPath = "assets/localization/editor/en-US.json";
    const std::filesystem::path trPath = "assets/localization/editor/tr-TR.json";
    if (!std::filesystem::exists(enPath) || !std::filesystem::exists(trPath))
        return;

    Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
    Horo::Editor::LocalizationError error;
    assert(service.LoadCatalogFile(enPath, &error));
    assert(service.LoadCatalogFile(trPath, &error));

    assert(service.Prepare(Horo::Editor::LocaleTag{"en-US"}, &error));
    assert(service.ActivatePrepared(&error));
    assert(service.Get("editor", "settings.input.shortcut.click_to_record") == "Click to record");
    assert(service.Get("editor", "settings.input.shortcut.press_keys") == "Press keys...");
    assert(service.Get("editor", "workspace.content_browser.embedded") == "EMBEDDED");
    assert(service.Get("editor", "workspace.content_browser.project_asset_dock") == "Project asset dock");
    assert(service.Get("editor", "workspace.global_dock.tab.assets") == "Assets");
    assert(service.Get("editor", "workspace.global_dock.tab.localization") == "L10n");
    assertGlobalDockKeysExist(service);

    assert(service.Prepare(Horo::Editor::LocaleTag{"tr-TR"}, &error));
    assert(service.ActivatePrepared(&error));
    assert(service.Get("editor", "settings.input.shortcut.click_to_record") == "Kaydetmek için tıkla");
    assert(service.Get("editor", "settings.input.shortcut.press_keys") == "Tuşlara basın...");
    assert(service.Get("editor", "workspace.content_browser.embedded") == "YERLEŞİK");
    assert(service.Get("editor", "workspace.content_browser.project_asset_dock") == "Proje varlık paneli");
    assert(service.Get("editor", "workspace.global_dock.tab.assets") == "Varlıklar");
    assert(service.Get("editor", "workspace.global_dock.tab.localization") == "L10n");
    assertGlobalDockKeysExist(service);
}
} // namespace

int main()
{
    LocaleTagsNormalizeAndRejectInvalidInput();
    LocaleSwitchUsesImmutablePreparedSnapshot();
    FailedPreparationLeavesActiveLocaleUnchanged();
    MissingMessageDoesNotUseSourceFallback();
    CatalogFileLoaderParsesResourceFormat();
    AssetLocalizationCatalogsContainRequiredEditorMessages();
    return 0;
}
