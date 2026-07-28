#include <catch2/catch_test_macros.hpp>

#include "ContentBrowserPanel.h"
#include "ContentBrowserPanelLayout.h"
#include "ContentBrowserModel.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "runtime/assets/importer/builtin/obj_mesh/ObjMeshImporter.h"

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "Horo/Foundation/DataBus.h"

#include <imgui.h>

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    class TestLocalization final : public Horo::Editor::ILocalizationService
    {
    public:
        [[nodiscard]] const std::string& Get(const std::string_view, const std::string_view localKey) const override
        {
            std::string_view value = localKey;
            if (localKey == "workspace.global_dock.tab.assets")
                value = "Assets";
            else if (localKey == "workspace.global_dock.tab.console")
                value = "Console";
            else if (localKey == "workspace.global_dock.tab.mcp")
                value = "MCP";
            else if (localKey == "workspace.global_dock.tab.performance")
                value = "Perf";
            else if (localKey == "workspace.global_dock.tab.physics")
                value = "Physics";
            else if (localKey == "workspace.global_dock.tab.audio")
                value = "Audio";
            else if (localKey == "workspace.global_dock.tab.network")
                value = "Net";
            else if (localKey == "workspace.global_dock.tab.localization")
                value = "L10n";

            const auto [entry, inserted] = values_.try_emplace(std::string(localKey), value);
            static_cast<void>(inserted);
            return entry->second;
        }

    private:
        mutable std::unordered_map<std::string, std::string> values_;
    };

    TEST_CASE("Content browser grid metrics respond to available width", "[unit][editor][gui]")
    {
        using Horo::Editor::ComputeContentBrowserGridMetrics;
        using Horo::Editor::kGlobalDockMinimumFontSize;

        static_assert(kGlobalDockMinimumFontSize == Horo::Editor::Theme::FontPx::SansCompact);

        const auto wide = ComputeContentBrowserGridMetrics(580.0F);
        REQUIRE((wide.columns == 8));
        REQUIRE((std::abs(wide.cardWidth - 67.25F) < 0.001F));

        const auto narrow = ComputeContentBrowserGridMetrics(240.0F);
        REQUIRE((narrow.columns == 3));
        REQUIRE((std::abs(narrow.cardWidth - 76.0F) < 0.001F));
    }

    TEST_CASE("Global dock exposes the default tabs", "[unit][editor][gui]")
    {
        using namespace Horo::Editor;

        constexpr std::array expected{
            GlobalDockTab::Assets, GlobalDockTab::Console, GlobalDockTab::Mcp, GlobalDockTab::Performance,
            GlobalDockTab::Physics, GlobalDockTab::Audio, GlobalDockTab::Network, GlobalDockTab::Localization,
        };
        REQUIRE((DefaultGlobalDockTabs() == expected));

        ContentBrowserPanel panel;
        REQUIRE((panel.ActiveTab() == GlobalDockTab::Assets));
    }

    TEST_CASE("Content browser exposes absolute folders assets and breadcrumbs", "[unit][editor][gui]")
    {
        using namespace Horo;
        using namespace Horo::Assets;
        using namespace Horo::Editor;

        const std::filesystem::path projectRoot =
            std::filesystem::temp_directory_path() /
            ("horo-content-browser-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        const std::filesystem::path assetRoot = projectRoot / "assets";
        const std::filesystem::path shotguns = assetRoot / "Props/Guns/Shotguns";
        std::filesystem::create_directories(shotguns);
        {
            std::ofstream payload(assetRoot / "root.horoasset", std::ios::binary);
            payload << "asset";
        }

        AssetRegistry registry;
        const auto report = registry.Publish({
            AssetRecord{
                .id = AssetId::Parse("00112233-4455-6677-8899-aabbccddeeff").Value(),
                .type = AssetTypeId::Parse("core.mesh").Value(),
                .sourcePath = ProjectPath::Parse("assets/root.horoasset").Value(),
                .metadataPath = ProjectPath::Parse("assets/root.horoasset.horo").Value(),
            },
        });
        REQUIRE((report.status == AssetRegistryBuildStatus::Complete));

        const auto root = BuildContentBrowserDirectory(projectRoot, {}, registry.Snapshot());
        REQUIRE((std::filesystem::path{root.absoluteRootPath}.is_absolute()));
        REQUIRE((root.absoluteCurrentPath == root.absoluteRootPath));
        REQUIRE((root.entries.size() == 2));
        REQUIRE((root.entries[0].kind == ContentBrowserEntryKind::Directory));
        REQUIRE((root.entries[0].displayName == "Props"));
        REQUIRE((std::filesystem::path{root.entries[0].absolutePath}.is_absolute()));
        REQUIRE((root.entries[1].kind == ContentBrowserEntryKind::Asset));
        REQUIRE((root.entries[1].displayName == "root"));
        REQUIRE((root.entries[1].assetType == "core.mesh"));

        const auto nested = BuildContentBrowserDirectory(projectRoot, shotguns, registry.Snapshot());
        REQUIRE((nested.breadcrumbs.size() == 4));
        REQUIRE((nested.breadcrumbs[0].label == "assets"));
        REQUIRE((nested.breadcrumbs[1].label == "Props"));
        REQUIRE((nested.breadcrumbs[2].label == "Guns"));
        REQUIRE((nested.breadcrumbs[3].label == "Shotguns"));
        for (const auto& breadcrumb : nested.breadcrumbs)
        {
            REQUIRE((std::filesystem::path{breadcrumb.absolutePath}.is_absolute()));
            REQUIRE((breadcrumb.absolutePath.find("..") == std::string::npos));
        }
        REQUIRE((!IsContentBrowserDirectoryTargetAllowed(assetRoot, "Props")));
        REQUIRE((!IsContentBrowserDirectoryTargetAllowed(assetRoot, projectRoot)));

        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
    }

    TEST_CASE(
        "Content browser search type filter and sort project immutable entry indices",
        "[unit][editor][gui]")
    {
        using namespace Horo::Editor;

        const ContentBrowserDirectory directory{
            .entries = {
                ContentBrowserEntry{
                    .kind = ContentBrowserEntryKind::Directory,
                    .absolutePath = "/project/assets/Props",
                    .displayName = "Props",
                },
                ContentBrowserEntry{
                    .kind = ContentBrowserEntryKind::Asset,
                    .absolutePath = "/project/assets/crate.horoasset",
                    .displayName = "Crate",
                    .assetType = "core.mesh",
                },
                ContentBrowserEntry{
                    .kind = ContentBrowserEntryKind::Asset,
                    .absolutePath = "/project/assets/wall.horoasset",
                    .displayName = "Wall",
                    .assetType = "core.texture",
                },
                ContentBrowserEntry{
                    .kind = ContentBrowserEntryKind::Asset,
                    .absolutePath = "/project/assets/hero.horoasset",
                    .displayName = "hero",
                    .assetType = "core.mesh",
                },
            },
            .readable = true,
        };

        const auto searched = ProjectContentBrowserEntries(
            directory, {.name = "wALL"});
        REQUIRE((searched == std::vector<std::size_t>{2}));

        const auto meshes = ProjectContentBrowserEntries(
            directory, {.assetType = "core.mesh"});
        REQUIRE((meshes == std::vector<std::size_t>{0, 1, 3}));

        const auto byDescendingType = ProjectContentBrowserEntries(
            directory,
            {
                .sortField = ContentBrowserSortField::Type,
                .sortDirection =
                    ContentBrowserSortDirection::Descending,
            });
        REQUIRE(
            (byDescendingType ==
             std::vector<std::size_t>{0, 2, 3, 1}));
    }

    TEST_CASE("Content browser uses the mesh fallback for topology-free legacy assets",
              "[unit][editor][gui]")
    {
        using namespace Horo;
        using namespace Horo::Editor;

        const std::filesystem::path projectRoot =
            std::filesystem::temp_directory_path() /
            ("horo-content-browser-legacy-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        const std::filesystem::path assetRoot = projectRoot / "assets/Meshes";
        std::filesystem::create_directories(assetRoot);
        const std::filesystem::path assetPath = assetRoot / "legacy.horoasset";

        std::vector<std::uint8_t> payload;
        const auto writeU32 = [&payload](const std::uint32_t value)
        {
            for (unsigned shift = 0; shift < 32; shift += 8)
                payload.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        };
        const auto writeFloat = [&writeU32](const float value)
        {
            writeU32(std::bit_cast<std::uint32_t>(value));
        };
        writeU32(1);
        writeU32(2);
        writeU32(1);
        for (const float bound : {-1.0F, -1.0F, -1.0F, 1.0F, 1.0F, 1.0F})
            writeFloat(bound);
        writeU32(24);
        writeU32(0);
        writeU32(0);
        for (const float component : {-1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F})
            writeFloat(component);
        {
            std::ofstream output(assetPath, std::ios::binary);
            output.write(reinterpret_cast<const char*>(payload.data()),
                         static_cast<std::streamsize>(payload.size()));
        }
        {
            std::ofstream metadata(assetPath.string() + ".meta");
            metadata << R"({"sourceFile":"/tmp/legacy.obj","type":"core.mesh"})";
        }

        Assets::AssetImporterCatalog importerCatalog;
        REQUIRE((Assets::RegisterObjMeshImporter(importerCatalog).HasValue()));
        auto publishedCatalog = importerCatalog.Publish();
        REQUIRE(publishedCatalog.HasValue());
        const ContentBrowserDirectory directory =
            BuildContentBrowserDirectory(projectRoot, assetRoot, {}, publishedCatalog.Value().get());
        REQUIRE((directory.readable));
        REQUIRE((directory.entries.size() == 1));
        REQUIRE((directory.entries[0].displayName == "legacy"));
        REQUIRE((directory.entries[0].assetType == "core.mesh"));
        REQUIRE((!directory.entries[0].registered));
        REQUIRE((std::filesystem::path{directory.entries[0].absoluteMetadataPath}.is_absolute()));
        REQUIRE((directory.entries[0].importerContributionId == "horo.asset-importer.obj-mesh"));
        REQUIRE((directory.entries[0].importerModuleId == "horo.builtin.assets.importer.obj"));
        REQUIRE((directory.entries[0].importerModuleVersion == "1.0.0"));
        REQUIRE((directory.entries[0].previewFallback == Assets::AssetPreviewFallback::Mesh));
        REQUIRE((!directory.entries[0].previewImage.IsValid()));

        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
    }

    void RenderAtWidth(const float width, const char* windowId, Horo::Editor::ContentBrowserPanel& panel,
                       const Horo::Editor::EditorGuiContext& context,
                       const Horo::Editor::ContentBrowserLoadState loadState =
                           Horo::Editor::ContentBrowserLoadState::Ready)
    {
        using namespace Horo::Editor;

        EditorWorkspaceViewModel viewModel;
        viewModel.contentBrowser = ContentBrowserDirectory{
            .absoluteRootPath = "/tmp/HoroProject/assets",
            .absoluteCurrentPath = "/tmp/HoroProject/assets/Props",
            .breadcrumbs = {
                ContentBrowserBreadcrumb{.label = "assets", .absolutePath = "/tmp/HoroProject/assets"},
                ContentBrowserBreadcrumb{.label = "Props", .absolutePath = "/tmp/HoroProject/assets/Props"},
            },
            .entries = {
            ContentBrowserEntry{
                .kind = ContentBrowserEntryKind::Directory,
                .absolutePath = "/tmp/HoroProject/assets/Props/Guns",
                .displayName = "Guns",
            },
            ContentBrowserEntry{
                .kind = ContentBrowserEntryKind::Asset,
                .absolutePath = "/tmp/HoroProject/assets/Props/crate.horoasset",
                .displayName = "crate.horoasset",
                .assetId = "00112233-4455-6677-8899-aabbccddeeff",
                .assetType = "core.mesh",
            },
            ContentBrowserEntry{
                .kind = ContentBrowserEntryKind::Asset,
                .absolutePath = "/tmp/HoroProject/assets/Props/wall.horoasset",
                .displayName = "wall.horoasset",
                .assetId = "10112233-4455-6677-8899-aabbccddeeff",
                .assetType = "core.texture",
            },
            },
            .readable = true,
            .loadState = loadState,
        };
        EditorWorkspaceViewCommandData command;
        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(width + 20.0F, 260.0F));
        ImGui::Begin(windowId, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
        panel.DrawPanel(ImGui::GetCursorScreenPos(), ImVec2(width, 220.0F), viewModel, command, context);
        ImGui::End();
    }
} // namespace

TEST_CASE("Content browser renders responsive layouts and every dock tab", "[unit][editor][gui]")
{
    using namespace Horo;
    using namespace Horo::Editor;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());

    EngineDataBus engineEvents;
    EditorDataBus editorEvents;
    TestLocalization localization;
    ImFont* defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{.sans = defaultFont, .sansCompact = defaultFont, .sansEmphasis = defaultFont};
    const ThemeContext theme{.fonts = fonts};
    const EditorSettingsSnapshot settings{};
    const EditorGuiContext context{
        .engineEvents = engineEvents,
        .editorEvents = editorEvents,
        .localization = localization,
        .theme = theme,
        .settings = settings
    };
    ContentBrowserPanel panel;

    ImGui::NewFrame();
    RenderAtWidth(600.0F, "ContentBrowserWide", panel, context);
    ImGui::Render();

    ImGui::NewFrame();
    RenderAtWidth(260.0F, "ContentBrowserNarrow", panel, context);
    ImGui::Render();

    ImGui::NewFrame();
    RenderAtWidth(
        600.0F, "ContentBrowserLoading", panel, context,
        ContentBrowserLoadState::Loading);
    ImGui::Render();

    ImGui::NewFrame();
    RenderAtWidth(
        600.0F, "ContentBrowserError", panel, context,
        ContentBrowserLoadState::Error);
    ImGui::Render();

    for (const GlobalDockTab tab : DefaultGlobalDockTabs())
    {
        ContentBrowserPanel tabPanel{tab};
        ImGui::NewFrame();
        RenderAtWidth(900.0F, "GlobalDockTabMatrix", tabPanel, context);
        ImGui::Render();
        REQUIRE((tabPanel.ActiveTab() == tab));
    }

    Log::StructuredLogStore logStore{8};
    for (std::size_t level = 0; level < 5; ++level)
    {
        logStore.Append(Log::StructuredLogRecord{
            .sequence = level + 1U,
            .timestampUtc = std::chrono::system_clock::now(),
            .level = static_cast<Log::Level>(level),
            .category = "editor.console.test",
            .message = "Live structured log row",
        });
    }
    ContentBrowserPanel liveConsole{GlobalDockTab::Console};
    PanelContext panelContext{.dataBus = editorEvents, .logQuery = &logStore};
    liveConsole.OnAttach(panelContext);
    ImGui::NewFrame();
    RenderAtWidth(900.0F, "LiveConsoleRows", liveConsole, context);
    ImGui::Render();
    liveConsole.OnDetach();

    ImGui::DestroyContext();
}
