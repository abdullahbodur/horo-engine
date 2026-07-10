#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/SettingsModal.h"
#include "Horo/Foundation/DataBus.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

namespace Horo::Editor::Theme
{
    struct Fonts;
}

Horo::Editor::ModalFrameResult Horo::Editor::SettingsModal::Draw()
{
    return Horo::Editor::ModalFrameResult::None();
}

namespace
{

using namespace Horo;
using namespace Horo::Editor;

struct SettingsFixture
{
    EditorDataBus events;
    ConfigurationService configuration = CreateEditorConfigurationService(DefaultEditorSettings());
    EditorSettingsService settings{DefaultEditorSettings(), configuration, events};
    const Theme::Fonts &fonts = *reinterpret_cast<const Theme::Fonts *>(static_cast<std::uintptr_t>(1));
    EditorModalHost host{events};
};

SettingsModal *Open(SettingsFixture &fixture)
{
    auto modal = std::make_unique<SettingsModal>(fixture.settings, fixture.fonts, 0);
    SettingsModal *const result = modal.get();
    assert(fixture.host.OpenRoot(std::move(modal)).HasValue());
    fixture.host.OnUpdate(0.0F);
    return result;
}

void OnOpenHydratesTheDraftFromTheAuthoritySnapshot()
{
    SettingsFixture fixture;
    EditorSettings next = DefaultEditorSettings();
    next.uiScalePercent = 125;
    next.defaultSceneOnProjectOpen = "Assets/Scenes/Authority";
    assert(fixture.settings.Commit(EditorSettingsDraft{.baseRevision = 0, .settings = next}).HasValue());

    SettingsModal *const modal = Open(fixture);
    assert(modal->Draft().appearance.uiScale == 125);
    assert(std::string{modal->Draft().general.defaultScene} == "Assets/Scenes/Authority");
    assert(!modal->Draft().dirty);
}

void CleanCloseDoesNotPublishARevertedNotification()
{
    SettingsFixture fixture;
    int reverted = 0;
    const Subscription subscription = fixture.events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
        if (event.phase == SettingsChangePhase::Reverted) ++reverted;
    });

    Open(fixture);
    assert(fixture.host.RequestClose(ModalId{SettingsModal::kModalId}, ModalCloseReason::Cancelled).HasValue());
    fixture.host.OnUpdate(0.0F);
    assert(reverted == 0);
}

void DirtyCancelledClosePublishesExactlyOneRevertedNotification()
{
    SettingsFixture fixture;
    int reverted = 0;
    const Subscription subscription = fixture.events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
        if (event.phase == SettingsChangePhase::Reverted) ++reverted;
    });

    SettingsModal *const modal = Open(fixture);
    modal->Draft().general.autoSaveInterval = 12;
    assert(fixture.host.RequestClose(ModalId{SettingsModal::kModalId}, ModalCloseReason::Cancelled).HasValue());
    fixture.host.OnUpdate(0.0F);
    assert(reverted == 1);
    fixture.host.ForceDetachAllForShutdown();
    assert(reverted == 1);
}

void DirtyForcedClosePublishesExactlyOneRevertedNotification()
{
    SettingsFixture fixture;
    int reverted = 0;
    const Subscription subscription = fixture.events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
        if (event.phase == SettingsChangePhase::Reverted) ++reverted;
    });

    SettingsModal *const modal = Open(fixture);
    modal->Draft().general.autoSaveInterval = 12;
    fixture.host.ForceDetachAllForShutdown();
    assert(reverted == 1);
    fixture.host.ForceDetachAllForShutdown();
    assert(reverted == 1);
}

void ApplyPublishesOnlyTheAuthorityCommittedNotification()
{
    SettingsFixture fixture;
    int committed = 0;
    int reverted = 0;
    const Subscription subscription = fixture.events.Subscribe<EditorSettingsChangedEvent>([&](const EditorSettingsChangedEvent &event) {
        if (event.phase == SettingsChangePhase::Committed) ++committed;
        if (event.phase == SettingsChangePhase::Reverted) ++reverted;
    });

    SettingsModal *const modal = Open(fixture);
    modal->Draft().general.autoSaveInterval = 12;
    assert(modal->ApplyDraft());
    assert(committed == 1);
    assert(reverted == 0);

    assert(fixture.host.RequestClose(ModalId{SettingsModal::kModalId}, ModalCloseReason::Cancelled).HasValue());
    fixture.host.OnUpdate(0.0F);
    assert(committed == 1);
    assert(reverted == 0);
}

} // namespace

int main()
{
    OnOpenHydratesTheDraftFromTheAuthoritySnapshot();
    CleanCloseDoesNotPublishARevertedNotification();
    DirtyCancelledClosePublishesExactlyOneRevertedNotification();
    DirtyForcedClosePublishesExactlyOneRevertedNotification();
    ApplyPublishesOnlyTheAuthorityCommittedNotification();
    return 0;
}
