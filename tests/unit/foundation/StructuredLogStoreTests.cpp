#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/Logging/StructuredLogStore.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace {
    [[nodiscard]] Horo::Log::StructuredLogRecord MakeRecord(const std::uint64_t sequence,
                                                            const Horo::Log::Level level = Horo::Log::Level::Info) {
        return Horo::Log::StructuredLogRecord{
            .sequence = sequence,
            .timestampUtc = std::chrono::system_clock::time_point{} + std::chrono::milliseconds(sequence),
            .level = level,
            .category = "test.category",
            .message = "message-" + std::to_string(sequence),
        };
    }
}  // namespace

TEST_CASE("Structured log store overwrites the oldest record at its fixed capacity", "[foundation][logging]") {
    Horo::Log::StructuredLogStore store{3};
    store.Append(MakeRecord(1));
    store.Append(MakeRecord(2));
    store.Append(MakeRecord(3));
    store.Append(MakeRecord(4));

    const auto snapshot = store.SnapshotIfChanged(0);
    REQUIRE(snapshot.has_value());
    REQUIRE((snapshot->revision == 4));
    REQUIRE((snapshot->capacity == 3));
    REQUIRE((snapshot->droppedRecordCount == 1));
    REQUIRE((snapshot->records.size() == 3));
    REQUIRE((snapshot->records.front()->sequence == 2));
    REQUIRE((snapshot->records.back()->sequence == 4));
    REQUIRE((!store.SnapshotIfChanged(snapshot->revision).has_value()));
}

TEST_CASE("Structured log store remains bounded under concurrent producers", "[foundation][logging]") {
    Horo::Log::StructuredLogStore store{64};
    std::vector<std::thread> producers;
    for (std::uint64_t producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&store, producer] {
            for (std::uint64_t record = 0; record < 100; ++record)
                store.Append(MakeRecord(producer * 100 + record));
        });
    }
    for (std::thread &producer : producers)
        producer.join();

    const auto snapshot = store.SnapshotIfChanged(0);
    REQUIRE(snapshot.has_value());
    REQUIRE((snapshot->revision == 400));
    REQUIRE((snapshot->records.size() == 64));
    REQUIRE((snapshot->droppedRecordCount == 336));
}

TEST_CASE("Logger fans accepted records out to the structured store", "[foundation][logging]") {
    auto store = std::make_shared<Horo::Log::StructuredLogStore>(8);
    const Horo::Log::Level previousLevel = Horo::Log::Logger::GetLevel();
    Horo::Log::Logger::SetLevel(Horo::Log::Level::Trace);
    Horo::Log::Logger::SetStructuredLogStore(store);

    Horo::Log::Logger::Write("test.live_console", Horo::Log::Level::Debug, "visible in both sinks");

    const auto snapshot = store->SnapshotIfChanged(0);
    REQUIRE(snapshot.has_value());
    REQUIRE((snapshot->records.size() == 1));
    REQUIRE((snapshot->records[0]->level == Horo::Log::Level::Debug));
    REQUIRE((snapshot->records[0]->category == "test.live_console"));
    REQUIRE((snapshot->records[0]->message == "visible in both sinks"));

    Horo::Log::Logger::SetStructuredLogStore(nullptr);
    Horo::Log::Logger::SetLevel(previousLevel);
}
