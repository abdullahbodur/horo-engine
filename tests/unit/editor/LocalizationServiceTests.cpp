#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/Localization/LocalizationService.h"

#include <array>
#include <filesystem>
#include <fstream>

namespace
{
    Horo::Editor::LocalizationCatalog Catalog(const char* locale, const char* text)
    {
        Horo::Editor::LocalizationCatalog catalog{.locale = Horo::Editor::LocaleTag{locale}};
        catalog.messages.emplace(Horo::Editor::MessageKey{"editor", "settings.title"}, text);
        return catalog;
    }

    TEST_CASE("Locale Tags Normalize And Reject Invalid Input", "[unit][editor]")
    {
        const auto normalized = Horo::Editor::LocaleTag::Parse("tr-TR");
        REQUIRE((normalized.has_value()));
        REQUIRE((normalized->value == "tr-TR"));
        REQUIRE((!Horo::Editor::LocaleTag::Parse("not a locale").has_value()));
    }

    TEST_CASE("Locale Switch Uses Immutable Prepared Snapshot", "[unit][editor]")
    {
        Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
        REQUIRE((service.RegisterCatalog(Catalog("en-US", "Settings"))));
        REQUIRE((service.RegisterCatalog(Catalog("tr-TR", "Ayarlar"))));
        REQUIRE((service.Prepare(Horo::Editor::LocaleTag{"en-US"})));
        REQUIRE((service.ActivatePrepared()));

        REQUIRE((service.Get("editor", "settings.title") == "Settings"));
        const auto before = service.Snapshot();

        REQUIRE((service.Prepare(Horo::Editor::LocaleTag{"tr-TR"})));
        REQUIRE((service.Get("editor", "settings.title") == "Settings"));
        REQUIRE((service.ActivatePrepared()));
        REQUIRE((service.Get("editor", "settings.title") == "Ayarlar"));
        REQUIRE((before.revision != service.Snapshot().revision));
    }

    TEST_CASE("Failed Preparation Leaves Active Locale Unchanged", "[unit][editor]")
    {
        Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
        REQUIRE((service.RegisterCatalog(Catalog("en-US", "Settings"))));
        REQUIRE((service.Prepare(Horo::Editor::LocaleTag{"en-US"})));
        REQUIRE((service.ActivatePrepared()));

        Horo::Editor::LocalizationError error;
        REQUIRE((!service.Prepare(Horo::Editor::LocaleTag{"de-DE"}, &error)));
        REQUIRE((error.code == "editor.localization.catalog_missing"));
        REQUIRE((service.ActiveLocale().value == "en-US"));
    }

    TEST_CASE("Missing Message Does Not Use Source Fallback", "[unit][editor]")
    {
        Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
        REQUIRE((service.RegisterCatalog(Catalog("en-US", "Settings"))));
        REQUIRE((service.Prepare(Horo::Editor::LocaleTag{"en-US"})));
        REQUIRE((service.ActivatePrepared()));

        REQUIRE((service.Get("editor", "missing") == "[missing:editor:missing]"));
    }

    TEST_CASE("Catalog File Loader Parses Resource Format", "[unit][editor]")
    {
        const auto path = std::filesystem::temp_directory_path() / "horo-localization-test.json";
        {
            std::ofstream output(path);
            output
                << R"({"schemaVersion":1,"messageFormat":"plain-text-1","locale":"tr-TR","namespace":"editor","messages":{"settings.title":{"text":"Ayarlar"}}})";
        }

        Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
        Horo::Editor::LocalizationError error;
        REQUIRE((service.LoadCatalogFile(path, &error)));
        REQUIRE((service.Prepare(Horo::Editor::LocaleTag{"tr-TR"}, &error)));
        REQUIRE((service.ActivatePrepared(&error)));
        REQUIRE((service.Get("editor", "settings.title") == "Ayarlar"));
        std::filesystem::remove(path);
    }

    TEST_CASE("Asset Localization Catalogs Contain Required Editor Messages", "[unit][editor]")
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
        const auto assertGlobalDockKeysExist = [&globalDockKeys](const Horo::Editor::LocalizationService& service)
        {
            for (const char* key : globalDockKeys)
                REQUIRE((!service.Get("editor", key).starts_with("[missing:")));
        };

        const std::filesystem::path enPath = "assets/localization/editor/en-US.json";
        const std::filesystem::path trPath = "assets/localization/editor/tr-TR.json";
        if (!std::filesystem::exists(enPath) || !std::filesystem::exists(trPath))
            return;

        Horo::Editor::LocalizationService service{Horo::Editor::LocaleTag{"en-US"}};
        Horo::Editor::LocalizationError error;
        REQUIRE((service.LoadCatalogFile(enPath, &error)));
        REQUIRE((service.LoadCatalogFile(trPath, &error)));

        REQUIRE((service.Prepare(Horo::Editor::LocaleTag{"en-US"}, &error)));
        REQUIRE((service.ActivatePrepared(&error)));
        REQUIRE((service.Get("editor", "settings.input.shortcut.click_to_record") == "Click to record"));
        REQUIRE((service.Get("editor", "settings.input.shortcut.press_keys") == "Press keys..."));
        REQUIRE((service.Get("editor", "workspace.content_browser.embedded") == "EMBEDDED"));
        REQUIRE((service.Get("editor", "workspace.content_browser.project_asset_dock") == "Project asset dock"));
        REQUIRE((service.Get("editor", "workspace.global_dock.tab.assets") == "Assets"));
        REQUIRE((service.Get("editor", "workspace.global_dock.tab.localization") == "L10n"));
        assertGlobalDockKeysExist(service);

        REQUIRE((service.Prepare(Horo::Editor::LocaleTag{"tr-TR"}, &error)));
        REQUIRE((service.ActivatePrepared(&error)));
        REQUIRE((service.Get("editor", "settings.input.shortcut.click_to_record") == "Kaydetmek için tıkla"));
        REQUIRE((service.Get("editor", "settings.input.shortcut.press_keys") == "Tuşlara basın..."));
        REQUIRE((service.Get("editor", "workspace.content_browser.embedded") == "YERLEŞİK"));
        REQUIRE((service.Get("editor", "workspace.content_browser.project_asset_dock") == "Proje varlık paneli"));
        REQUIRE((service.Get("editor", "workspace.global_dock.tab.assets") == "Varlıklar"));
        REQUIRE((service.Get("editor", "workspace.global_dock.tab.localization") == "L10n"));
        assertGlobalDockKeysExist(service);
    }
} // namespace
