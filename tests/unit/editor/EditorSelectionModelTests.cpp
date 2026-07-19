#include <catch2/catch_test_macros.hpp>

#include "editor/project_model/EditorSelectionModel.h"

#include <vector>

namespace
{
    using namespace Horo::Editor;

    struct SelectionFixture
    {
        EditorDataBus events;
        SceneDocument document;
        EditorHistory history;
        SceneDocumentCommandExecutor commands{document, history};
        EditorSelectionModel selection{document, events};
    };

    TEST_CASE("Valid Selection Commits Once And Deduplicates Stable Ids", "[unit][editor]")
    {
        SelectionFixture fixture;
        const auto created = fixture.commands.Execute(CreateSceneObjectCommand{.name = "Box"});
        REQUIRE((created.HasValue()));
        const SceneObjectId object = created.Value().object;

        std::vector<SelectionChangedEvent> events;
        auto subscription = fixture.events.Subscribe<SelectionChangedEvent>(
            [&events](const SelectionChangedEvent& event) { events.push_back(event); });
        const auto selected = fixture.selection.SetObjects({object, object}, object);

        REQUIRE((selected.HasValue()));
        REQUIRE((fixture.selection.Current().objects == std::vector{object}));
        REQUIRE((fixture.selection.Current().primary == object));
        REQUIRE((fixture.selection.Current().revision == SelectionRevision{1}));
        REQUIRE((events.size() == 1));
        REQUIRE((events.back().kind == SelectionChangeKind::ObjectsChanged));

        REQUIRE((fixture.selection.SetObjects({object}, object).HasValue()));
        REQUIRE((events.size() == 1));
        static_cast<void>(subscription);
    }

    TEST_CASE("Invalid Selection Is Rejected Without Changing State", "[unit][editor]")
    {
        SelectionFixture fixture;
        const auto created = fixture.commands.Execute(CreateSceneObjectCommand{.name = "Box"});
        REQUIRE((created.HasValue()));

        std::vector<SelectionChangedEvent> events;
        auto subscription = fixture.events.Subscribe<SelectionChangedEvent>(
            [&events](const SelectionChangedEvent& event) { events.push_back(event); });
        const auto missing = fixture.selection.SetObjects({SceneObjectId{999}}, SceneObjectId{999});
        const auto invalidPrimary = fixture.selection.SetObjects({created.Value().object}, SceneObjectId{999});

        REQUIRE((missing.HasError()));
        REQUIRE((invalidPrimary.HasError()));
        REQUIRE((fixture.selection.Current().revision == SelectionRevision{}));
        REQUIRE((fixture.selection.Current().objects.empty()));
        REQUIRE((events.empty()));
        static_cast<void>(subscription);
    }

    TEST_CASE("Reconcile Removes Deleted Objects And Promotes A Surviving Primary", "[unit][editor]")
    {
        SelectionFixture fixture;
        const auto first = fixture.commands.Execute(CreateSceneObjectCommand{.name = "First"});
        const auto second = fixture.commands.Execute(CreateSceneObjectCommand{.name = "Second"});
        REQUIRE((first.HasValue() && second.HasValue()));
        REQUIRE(
            (fixture.selection.SetObjects({first.Value().object, second.Value().object}, first.Value().object).HasValue(
            )));

        std::vector<SelectionChangedEvent> events;
        auto subscription = fixture.events.Subscribe<SelectionChangedEvent>(
            [&events](const SelectionChangedEvent& event) { events.push_back(event); });
        REQUIRE((fixture.commands.Execute(DeleteSceneObjectCommand{first.Value().object}).HasValue()));
        fixture.selection.Reconcile();

        REQUIRE((fixture.selection.Current().objects == std::vector{second.Value().object}));
        REQUIRE((fixture.selection.Current().primary == second.Value().object));
        REQUIRE((fixture.selection.Current().revision == SelectionRevision{2}));
        REQUIRE((events.size() == 1));
        REQUIRE((events.back().kind == SelectionChangeKind::Reconciled));

        REQUIRE((fixture.commands.Execute(DeleteSceneObjectCommand{second.Value().object}).HasValue()));
        fixture.selection.Reconcile();
        REQUIRE((fixture.selection.Current().objects.empty()));
        REQUIRE((!fixture.selection.Current().primary.has_value()));
        REQUIRE((events.size() == 2));
        REQUIRE((events.back().kind == SelectionChangeKind::Cleared));
        static_cast<void>(subscription);
    }
} // namespace
