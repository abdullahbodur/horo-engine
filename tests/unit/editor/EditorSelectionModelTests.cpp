#include "editor/project_model/EditorSelectionModel.h"

#include <cassert>
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

void ValidSelectionCommitsOnceAndDeduplicatesStableIds()
{
    SelectionFixture fixture;
    const auto created = fixture.commands.Execute(CreateSceneObjectCommand{.name = "Box"});
    assert(created.HasValue());
    const SceneObjectId object = created.Value().object;

    std::vector<SelectionChangedEvent> events;
    auto subscription = fixture.events.Subscribe<SelectionChangedEvent>(
        [&events](const SelectionChangedEvent &event) { events.push_back(event); });
    const auto selected = fixture.selection.SetObjects({object, object}, object);

    assert(selected.HasValue());
    assert(fixture.selection.Current().objects == std::vector{object});
    assert(fixture.selection.Current().primary == object);
    assert(fixture.selection.Current().revision == SelectionRevision{1});
    assert(events.size() == 1);
    assert(events.back().kind == SelectionChangeKind::ObjectsChanged);

    assert(fixture.selection.SetObjects({object}, object).HasValue());
    assert(events.size() == 1);
    static_cast<void>(subscription);
}

void InvalidSelectionIsRejectedWithoutChangingState()
{
    SelectionFixture fixture;
    const auto created = fixture.commands.Execute(CreateSceneObjectCommand{.name = "Box"});
    assert(created.HasValue());

    std::vector<SelectionChangedEvent> events;
    auto subscription = fixture.events.Subscribe<SelectionChangedEvent>(
        [&events](const SelectionChangedEvent &event) { events.push_back(event); });
    const auto missing = fixture.selection.SetObjects({SceneObjectId{999}}, SceneObjectId{999});
    const auto invalidPrimary = fixture.selection.SetObjects({created.Value().object}, SceneObjectId{999});

    assert(missing.HasError());
    assert(invalidPrimary.HasError());
    assert(fixture.selection.Current().revision == SelectionRevision{});
    assert(fixture.selection.Current().objects.empty());
    assert(events.empty());
    static_cast<void>(subscription);
}

void ReconcileRemovesDeletedObjectsAndPromotesASurvivingPrimary()
{
    SelectionFixture fixture;
    const auto first = fixture.commands.Execute(CreateSceneObjectCommand{.name = "First"});
    const auto second = fixture.commands.Execute(CreateSceneObjectCommand{.name = "Second"});
    assert(first.HasValue() && second.HasValue());
    assert(fixture.selection.SetObjects({first.Value().object, second.Value().object}, first.Value().object).HasValue());

    std::vector<SelectionChangedEvent> events;
    auto subscription = fixture.events.Subscribe<SelectionChangedEvent>(
        [&events](const SelectionChangedEvent &event) { events.push_back(event); });
    assert(fixture.commands.Execute(DeleteSceneObjectCommand{first.Value().object}).HasValue());
    fixture.selection.Reconcile();

    assert(fixture.selection.Current().objects == std::vector{second.Value().object});
    assert(fixture.selection.Current().primary == second.Value().object);
    assert(fixture.selection.Current().revision == SelectionRevision{2});
    assert(events.size() == 1);
    assert(events.back().kind == SelectionChangeKind::Reconciled);

    assert(fixture.commands.Execute(DeleteSceneObjectCommand{second.Value().object}).HasValue());
    fixture.selection.Reconcile();
    assert(fixture.selection.Current().objects.empty());
    assert(!fixture.selection.Current().primary.has_value());
    assert(events.size() == 2);
    assert(events.back().kind == SelectionChangeKind::Cleared);
    static_cast<void>(subscription);
}
} // namespace

int main()
{
    ValidSelectionCommitsOnceAndDeduplicatesStableIds();
    InvalidSelectionIsRejectedWithoutChangingState();
    ReconcileRemovesDeletedObjectsAndPromotesASurvivingPrimary();
    return 0;
}
