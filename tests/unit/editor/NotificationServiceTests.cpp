#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/NotificationService.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

TEST_CASE("NotificationService publishes events across EditorDataBus", "[editor][notifications]")
{
    Horo::Editor::EditorDataBus dataBus;
    Horo::Editor::NotificationService notificationService(dataBus);

    std::vector<Horo::Editor::NotificationEvent> receivedEvents;
    const auto subscription = dataBus.Subscribe<Horo::Editor::NotificationEvent>(
        [&receivedEvents](const Horo::Editor::NotificationEvent &event) {
            receivedEvents.push_back(event);
        });

    SECTION("Publish with default parameters")
    {
        notificationService.Publish("gameplay", Horo::Editor::NotificationSeverity::Error,
                                    "A behavior type ID is duplicated.");

        REQUIRE(receivedEvents.size() == 1);
        CHECK(receivedEvents[0].id != 0);
        CHECK(receivedEvents[0].source == "gameplay");
        CHECK(receivedEvents[0].severity == Horo::Editor::NotificationSeverity::Error);
        CHECK(receivedEvents[0].message == "A behavior type ID is duplicated.");
        CHECK(receivedEvents[0].durationSeconds == 5.0f);
        CHECK(receivedEvents[0].actions.empty());
    }

    SECTION("Publish with deduplication key namespacing and action button")
    {
        notificationService.Publish("gameplay", Horo::Editor::NotificationSeverity::Error,
                                    "Play session blocked due to invalid manifest", "Play session blocked",
                                    "play_blocked", 0.0f,
                                    {Horo::Editor::NotificationAction{.label = "Open logs", .actionId = "open_logs"}});

        REQUIRE(receivedEvents.size() == 1);
        CHECK(receivedEvents[0].source == "gameplay");
        CHECK(receivedEvents[0].title == "Play session blocked");
        CHECK(receivedEvents[0].deduplicationKey == "gameplay::play_blocked");
        CHECK(receivedEvents[0].durationSeconds == 0.0f);
        REQUIRE(receivedEvents[0].actions.size() == 1);
        CHECK(receivedEvents[0].actions[0].label == "Open logs");
        CHECK(receivedEvents[0].actions[0].actionId == "open_logs");
    }
}
