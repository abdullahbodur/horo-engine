#include "Horo/Editor/ActivityBarLayout.h"

#include <cassert>
#include <vector>

namespace
{
using namespace Horo::Editor;

void MovesAnItemIntoAnInsertionSlot()
{
    ActivityBarLayout layout;
    static_cast<void>(layout.Insert("horo.hierarchy", ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
    static_cast<void>(layout.Insert("horo.inspector", ActivityBarSlot{ActivityBarRail::Left, 0, 1}));

    const auto result = layout.Move("horo.inspector", ActivityBarSlot{ActivityBarRail::Left, 1, 0});
    assert(result.Succeeded());
    assert(layout.FindSlot("horo.inspector").value() == (ActivityBarSlot{ActivityBarRail::Left, 1, 0}));
    assert(layout.FindSlot("horo.hierarchy").value() == (ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
}

void MovingWithinAGroupShiftsExistingItems()
{
    ActivityBarLayout layout;
    static_cast<void>(layout.Insert("one", ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
    static_cast<void>(layout.Insert("two", ActivityBarSlot{ActivityBarRail::Left, 0, 1}));
    static_cast<void>(layout.Insert("three", ActivityBarSlot{ActivityBarRail::Left, 0, 2}));

    assert(layout.Move("three", ActivityBarSlot{ActivityBarRail::Left, 0, 1}).Succeeded());
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 0) == "one");
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 1) == "three");
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 2) == "two");
}

void RejectsUnknownItemAndInvalidInsertionWithoutMutation()
{
    ActivityBarLayout layout;
    static_cast<void>(layout.Insert("one", ActivityBarSlot{ActivityBarRail::Left, 0, 0}));

    assert(!layout.Move("missing", ActivityBarSlot{ActivityBarRail::Left, 0, 0}).Succeeded());
    assert(!layout.Move("one", ActivityBarSlot{ActivityBarRail::Left, 0, 3}).Succeeded());
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 0) == "one");
}

void DroppingTheOnlyItemBackIntoItsGroupIsANoOp()
{
    ActivityBarLayout layout;
    static_cast<void>(layout.Insert("one", ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
    const auto result = layout.Move("one", ActivityBarSlot{ActivityBarRail::Left, 0, 1});
    assert(result.code == ActivityBarLayoutOperationCode::NoOp);
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 0) == "one");
}

void MovingAcrossGroupsCompactsTheSourceAndInsertsAtTheTargetIndex()
{
    ActivityBarLayout layout;
    static_cast<void>(layout.Insert("source-0", ActivityBarSlot{ActivityBarRail::Left, 1, 0}));
    static_cast<void>(layout.Insert("source-1", ActivityBarSlot{ActivityBarRail::Left, 1, 1}));
    static_cast<void>(layout.Insert("source-2", ActivityBarSlot{ActivityBarRail::Left, 1, 2}));
    static_cast<void>(layout.Insert("source-3", ActivityBarSlot{ActivityBarRail::Left, 1, 3}));
    static_cast<void>(layout.Insert("source-4", ActivityBarSlot{ActivityBarRail::Left, 1, 4}));
    static_cast<void>(layout.Insert("source-5", ActivityBarSlot{ActivityBarRail::Left, 1, 5}));
    static_cast<void>(layout.Insert("target-0", ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
    static_cast<void>(layout.Insert("target-1", ActivityBarSlot{ActivityBarRail::Left, 0, 1}));
    static_cast<void>(layout.Insert("target-2", ActivityBarSlot{ActivityBarRail::Left, 0, 2}));
    static_cast<void>(layout.Insert("target-3", ActivityBarSlot{ActivityBarRail::Left, 0, 3}));

    const auto result = layout.Move("source-2", ActivityBarSlot{ActivityBarRail::Left, 0, 2});
    assert(result.Succeeded());
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 0) == "target-0");
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 1) == "target-1");
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 2) == "source-2");
    assert(layout.ItemAt(ActivityBarRail::Left, 0, 3) == "target-2");
    assert(layout.ItemAt(ActivityBarRail::Left, 1, 0) == "source-0");
    assert(layout.ItemAt(ActivityBarRail::Left, 1, 1) == "source-1");
    assert(layout.ItemAt(ActivityBarRail::Left, 1, 2) == "source-3");
    assert(layout.ItemAt(ActivityBarRail::Left, 1, 3) == "source-4");
    assert(layout.ItemAt(ActivityBarRail::Left, 1, 4) == "source-5");
}
} // namespace

int main()
{
    MovesAnItemIntoAnInsertionSlot();
    MovingWithinAGroupShiftsExistingItems();
    RejectsUnknownItemAndInvalidInsertionWithoutMutation();
    DroppingTheOnlyItemBackIntoItsGroupIsANoOp();
    MovingAcrossGroupsCompactsTheSourceAndInsertsAtTheTargetIndex();
    return 0;
}
