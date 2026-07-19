#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Editor/SettingsModal.h"
#include "Horo/Foundation/DataBus.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Horo::Editor::Theme
{
struct Fonts;
}

Horo::Editor::ModalFrameResult Horo::Editor::SettingsModal::Draw()
{
    return ModalFrameResult::None();
}

namespace
{
using namespace Horo;
using namespace Horo::Editor;

class ScopedSettingsHome
{
  public:
    ScopedSettingsHome()
        : path_(std::filesystem::temp_directory_path() /
                ("horo-settings-modal-lifecycle-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
#if defined(_WIN32)
        constexpr const char *key = "USERPROFILE";
#else
        constexpr const char *key = "HOME";
#endif
        if (const char *current = std::getenv(key))
            previous_ = current;
        std::filesystem::create_directories(path_);
#if defined(_WIN32)
        _putenv_s(key, path_.string().c_str());
#else
        setenv(key, path_.string().c_str(), 1);
#endif
    }

    ~ScopedSettingsHome()
    {
#if defined(_WIN32)
        constexpr const char *key = "USERPROFILE";
        _putenv_s(key, previous_.value_or("").c_str());
#else
        constexpr const char *key = "HOME";
        if (previous_)
            setenv(key, previous_->c_str(), 1);
        else
            unsetenv(key);
#endif
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

  private:
    std::filesystem::path path_;
    std::optional<std::string> previous_;
};

struct SettingsFixture
{
    EngineDataBus engineEvents;
    EditorDataBus events;
    Input::InputRouter input;
    ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings());
    LocalizationService localization{LocaleTag{"en-US"}};
    EditorSettingsService settings{DefaultEditorSettings(), configuration, events, localization};
    const Theme::Fonts &fonts = *reinterpret_cast<const Theme::Fonts *>(static_cast<std::uintptr_t>(1));
    ThemeContext theme{fonts};
    EditorSettingsSnapshot snapshot = settings.Snapshot();
    EditorGuiContext ctx{engineEvents, events, localization, theme, snapshot};
    EditorModalHost host{events, input};
};

SettingsModal *Open(SettingsFixture &fixture)
{
    auto modal = std::make_unique<SettingsModal>(fixture.ctx, fixture.settings, 0);
    SettingsModal *const result = modal.get();
    REQUIRE((fixture.host.OpenRoot(std::move(modal)).HasValue()));
    fixture.host.OnUpdate(0.0F);
    return result;
}

TEST_CASE("Opening settings hydrates the authority snapshot", "[unit][editor][settings]")
{
    const ScopedSettingsHome home;
    SettingsFixture fixture;
    EditorSettings next = DefaultEditorSettings();
    next.uiScalePercent = 125;
    next.defaultSceneOnProjectOpen = "Assets/Scenes/Authority";
    REQUIRE((fixture.settings.Commit(EditorSettingsDraft{.baseRevision = 0, .settings = next}).HasValue()));

    SettingsModal *const modal = Open(fixture);
    REQUIRE((modal->Draft().appearance.uiScale == 125));
    REQUIRE((std::string{modal->Draft().general.defaultScene} == "Assets/Scenes/Authority"));
    REQUIRE((!modal->Draft().dirty));
}

TEST_CASE("Closing a clean settings modal does not publish a revert", "[unit][editor][settings]")
{
    const ScopedSettingsHome home;
    SettingsFixture fixture;
    int reverted = 0;
    const Subscription subscription =
        fixture.events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
            if (event.phase == SettingsChangePhase::Reverted)
                ++reverted;
        });

    Open(fixture);
    REQUIRE((fixture.host.RequestClose(ModalId{SettingsModal::kModalId}, ModalCloseReason::Cancelled).HasValue()));
    fixture.host.OnUpdate(0.0F);
    REQUIRE((reverted == 0));
}

TEST_CASE("Cancelling dirty settings publishes one revert", "[unit][editor][settings]")
{
    const ScopedSettingsHome home;
    SettingsFixture fixture;
    int reverted = 0;
    const Subscription subscription =
        fixture.events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
            if (event.phase == SettingsChangePhase::Reverted)
                ++reverted;
        });

    SettingsModal *const modal = Open(fixture);
    modal->Draft().general.autoSaveInterval = 12;
    REQUIRE((fixture.host.RequestClose(ModalId{SettingsModal::kModalId}, ModalCloseReason::Cancelled).HasValue()));
    fixture.host.OnUpdate(0.0F);
    REQUIRE((reverted == 1));
    fixture.host.ForceDetachAllForShutdown();
    REQUIRE((reverted == 1));
}

TEST_CASE("Force closing dirty settings publishes one revert", "[unit][editor][settings]")
{
    const ScopedSettingsHome home;
    SettingsFixture fixture;
    int reverted = 0;
    const Subscription subscription =
        fixture.events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
            if (event.phase == SettingsChangePhase::Reverted)
                ++reverted;
        });

    SettingsModal *const modal = Open(fixture);
    modal->Draft().general.autoSaveInterval = 12;
    fixture.host.ForceDetachAllForShutdown();
    REQUIRE((reverted == 1));
    fixture.host.ForceDetachAllForShutdown();
    REQUIRE((reverted == 1));
}

TEST_CASE("Applying settings publishes only the authority commit", "[unit][editor][settings]")
{
    const ScopedSettingsHome home;
    SettingsFixture fixture;
    int committed = 0;
    int reverted = 0;
    const Subscription subscription =
        fixture.events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
            if (event.phase == SettingsChangePhase::Committed)
                ++committed;
            if (event.phase == SettingsChangePhase::Reverted)
                ++reverted;
        });

    SettingsModal *const modal = Open(fixture);
    modal->Draft().general.autoSaveInterval = 12;
    REQUIRE((modal->ApplyDraft()));
    REQUIRE((committed == 1));
    REQUIRE((reverted == 0));

    REQUIRE((fixture.host.RequestClose(ModalId{SettingsModal::kModalId}, ModalCloseReason::Cancelled).HasValue()));
    fixture.host.OnUpdate(0.0F);
    REQUIRE((committed == 1));
    REQUIRE((reverted == 0));
}
} // namespace
