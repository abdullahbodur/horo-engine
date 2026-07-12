#include "Horo/Editor/Localization/LocalizationService.h"

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
        output << R"({"schemaVersion":1,"messageFormat":"plain-text-1","locale":"tr-TR","namespace":"editor","messages":{"settings.title":{"text":"Ayarlar"}}})";
    }

    Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
    Horo::Editor::LocalizationError error;
    assert(service.LoadCatalogFile(path, &error));
    assert(service.Prepare(Horo::Editor::LocaleTag{"tr-TR"}, &error));
    assert(service.ActivatePrepared(&error));
    assert(service.Get("editor", "settings.title") == "Ayarlar");
    std::filesystem::remove(path);
}

void AssetLocalizationCatalogsContainShortcutMessages()
{
    const std::filesystem::path enPath = "assets/localization/editor/en-US.json";
    const std::filesystem::path trPath = "assets/localization/editor/tr-TR.json";
    if (!std::filesystem::exists(enPath) || !std::filesystem::exists(trPath)) return;

    Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
    Horo::Editor::LocalizationError error;
    assert(service.LoadCatalogFile(enPath, &error));
    assert(service.LoadCatalogFile(trPath, &error));

    assert(service.Prepare(Horo::Editor::LocaleTag{"en-US"}, &error));
    assert(service.ActivatePrepared(&error));
    assert(service.Get("editor", "settings.input.shortcut.click_to_record") == "Click to record");
    assert(service.Get("editor", "settings.input.shortcut.press_keys") == "Press keys...");

    assert(service.Prepare(Horo::Editor::LocaleTag{"tr-TR"}, &error));
    assert(service.ActivatePrepared(&error));
    assert(service.Get("editor", "settings.input.shortcut.click_to_record") == "Kaydetmek için tıkla");
    assert(service.Get("editor", "settings.input.shortcut.press_keys") == "Tuşlara basın...");
}
} // namespace

int main()
{
    LocaleTagsNormalizeAndRejectInvalidInput();
    LocaleSwitchUsesImmutablePreparedSnapshot();
    FailedPreparationLeavesActiveLocaleUnchanged();
    MissingMessageDoesNotUseSourceFallback();
    CatalogFileLoaderParsesResourceFormat();
    AssetLocalizationCatalogsContainShortcutMessages();
    return 0;
}
