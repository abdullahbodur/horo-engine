#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Foundation/DataBus.h"

#include <cassert>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{

struct EnginePingEvent
{
    static constexpr std::string_view HoroEventTypeName = "horo.tests.EnginePingEvent";
    int value = 0;
};

struct EditorSelectionInvalidatedEvent
{
    static constexpr std::string_view HoroEventTypeName = "horo.tests.EditorSelectionInvalidatedEvent";
    int revision = 0;
};

void SubscriptionTokenDetachesHandler()
{
    Horo::EngineDataBus bus;
    int handled = 0;
    {
        auto subscription = bus.Subscribe<EnginePingEvent>([&handled](const EnginePingEvent &) { ++handled; });
        bus.Publish(EnginePingEvent{.value = 1});
        assert(handled == 1);
    }

    bus.Publish(EnginePingEvent{.value = 2});
    assert(handled == 1);
}

void PublishTargetsMatchingEventTypeOnly()
{
    Horo::EngineDataBus bus;
    int engineHandled = 0;
    int editorHandled = 0;
    auto engineSubscription = bus.Subscribe<EnginePingEvent>([&engineHandled](const EnginePingEvent &) { ++engineHandled; });
    auto editorSubscription = bus.Subscribe<EditorSelectionInvalidatedEvent>(
        [&editorHandled](const EditorSelectionInvalidatedEvent &) { ++editorHandled; });

    bus.Publish(EnginePingEvent{.value = 9});
    assert(engineHandled == 1);
    assert(editorHandled == 0);
}

void HandlerFailureIsIsolated()
{
    Horo::EngineDataBus bus;
    int handled = 0;
    auto throwingSubscription = bus.Subscribe<EnginePingEvent>([](const EnginePingEvent &) {
        throw std::runtime_error{"expected test failure"};
    });
    auto healthySubscription = bus.Subscribe<EnginePingEvent>([&handled](const EnginePingEvent &) { ++handled; });

    bus.Publish(EnginePingEvent{});
    assert(handled == 1);
}

void AsyncEventsDispatchAtTheExplicitSynchronizationPoint()
{
    Horo::EngineDataBus bus;
    int handled = 0;
    auto subscription = bus.Subscribe<EnginePingEvent>([&handled](const EnginePingEvent &event) { handled = event.value; });

    bus.PublishAsync(EnginePingEvent{.value = 42});
    assert(handled == 0);
    bus.DispatchQueued();
    assert(handled == 42);
}

void EditorBusDoesNotShareEngineHandlers()
{
    Horo::EngineDataBus engineBus;
    Horo::Editor::EditorDataBus editorBus;
    int engineHandled = 0;
    int editorHandled = 0;
    auto engineSubscription = engineBus.Subscribe<EditorSelectionInvalidatedEvent>(
        [&engineHandled](const EditorSelectionInvalidatedEvent &) { ++engineHandled; });
    auto editorSubscription = editorBus.Subscribe<EditorSelectionInvalidatedEvent>(
        [&editorHandled](const EditorSelectionInvalidatedEvent &) { ++editorHandled; });

    editorBus.Publish(EditorSelectionInvalidatedEvent{.revision = 1});
    assert(engineHandled == 0);
    assert(editorHandled == 1);
}

void SameTypeRecursionIsDeferredUntilCurrentDeliveryCompletes()
{
    Horo::Editor::EditorDataBus bus;
    std::vector<int> order;
    auto firstSubscription = bus.Subscribe<EditorSelectionInvalidatedEvent>([&](const EditorSelectionInvalidatedEvent &event) {
        order.push_back(event.revision);
        if (event.revision == 1)
        {
            bus.Publish(EditorSelectionInvalidatedEvent{.revision = 2});
        }
    });
    auto secondSubscription = bus.Subscribe<EditorSelectionInvalidatedEvent>([&](const EditorSelectionInvalidatedEvent &event) {
        order.push_back(event.revision * 10);
    });

    bus.Publish(EditorSelectionInvalidatedEvent{.revision = 1});
    assert((order == std::vector<int>{1, 10, 2, 20}));
}

} // namespace

int main()
{
    SubscriptionTokenDetachesHandler();
    PublishTargetsMatchingEventTypeOnly();
    HandlerFailureIsIsolated();
    AsyncEventsDispatchAtTheExplicitSynchronizationPoint();
    EditorBusDoesNotShareEngineHandlers();
    SameTypeRecursionIsDeferredUntilCurrentDeliveryCompletes();
    return 0;
}
