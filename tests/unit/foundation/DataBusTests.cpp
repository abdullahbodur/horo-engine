#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Foundation/DataBus.h"

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

TEST_CASE("Subscription Token Detaches Handler", "[unit][foundation]")
{
    Horo::EngineDataBus bus;
    int handled = 0;
    {
        auto subscription = bus.Subscribe<EnginePingEvent>([&handled](const EnginePingEvent &) { ++handled; });
        bus.Publish(EnginePingEvent{.value = 1});
        REQUIRE((handled == 1));
    }

    bus.Publish(EnginePingEvent{.value = 2});
    REQUIRE((handled == 1));
}

TEST_CASE("Publish Targets Matching Event Type Only", "[unit][foundation]")
{
    Horo::EngineDataBus bus;
    int engineHandled = 0;
    int editorHandled = 0;
    auto engineSubscription =
        bus.Subscribe<EnginePingEvent>([&engineHandled](const EnginePingEvent &) { ++engineHandled; });
    auto editorSubscription = bus.Subscribe<EditorSelectionInvalidatedEvent>(
        [&editorHandled](const EditorSelectionInvalidatedEvent &) { ++editorHandled; });

    bus.Publish(EnginePingEvent{.value = 9});
    REQUIRE((engineHandled == 1));
    REQUIRE((editorHandled == 0));
}

TEST_CASE("Handler Failure Is Isolated", "[unit][foundation]")
{
    Horo::EngineDataBus bus;
    int handled = 0;
    auto throwingSubscription = bus.Subscribe<EnginePingEvent>(
        [](const EnginePingEvent &) { throw std::runtime_error{"expected test failure"}; });
    auto healthySubscription = bus.Subscribe<EnginePingEvent>([&handled](const EnginePingEvent &) { ++handled; });

    bus.Publish(EnginePingEvent{});
    REQUIRE((handled == 1));
}

TEST_CASE("Async Events Dispatch At The Explicit Synchronization Point", "[unit][foundation]")
{
    Horo::EngineDataBus bus;
    int handled = 0;
    auto subscription =
        bus.Subscribe<EnginePingEvent>([&handled](const EnginePingEvent &event) { handled = event.value; });

    bus.PublishAsync(EnginePingEvent{.value = 42});
    REQUIRE((handled == 0));
    bus.DispatchQueued();
    REQUIRE((handled == 42));
}

TEST_CASE("Editor Bus Does Not Share Engine Handlers", "[unit][foundation]")
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
    REQUIRE((engineHandled == 0));
    REQUIRE((editorHandled == 1));
}

TEST_CASE("Same Type Recursion Is Deferred Until Current Delivery Completes", "[unit][foundation]")
{
    Horo::Editor::EditorDataBus bus;
    std::vector<int> order;
    auto firstSubscription =
        bus.Subscribe<EditorSelectionInvalidatedEvent>([&](const EditorSelectionInvalidatedEvent &event) {
            order.push_back(event.revision);
            if (event.revision == 1)
            {
                bus.Publish(EditorSelectionInvalidatedEvent{.revision = 2});
            }
        });
    auto secondSubscription = bus.Subscribe<EditorSelectionInvalidatedEvent>(
        [&](const EditorSelectionInvalidatedEvent &event) { order.push_back(event.revision * 10); });

    bus.Publish(EditorSelectionInvalidatedEvent{.revision = 1});
    REQUIRE((order == std::vector{1, 10, 2, 20}));
}
} // namespace
