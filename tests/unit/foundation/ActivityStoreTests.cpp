#include "Horo/Foundation/BuildOutputStore.h"
#include "Horo/Foundation/OperationStore.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

TEST_CASE("Build output store retains a bounded typed snapshot", "[foundation][diagnostics]") {
    Horo::BuildOutputStore store{2};
    store.Append({.status = Horo::BuildOutputStatus::Info, .phase = "prepare", .message = "one"});
    store.Append({.status = Horo::BuildOutputStatus::Failed,
                  .phase = "compile",
                  .message = "two",
                  .source = Horo::DiagnosticSourceLocation{.absolutePath = "/project/source.cpp", .line = 12, .column = 3}});
    store.Append({.status = Horo::BuildOutputStatus::Succeeded, .phase = "link", .message = "three"});

    const auto snapshot = store.SnapshotIfChanged(0);
    REQUIRE(snapshot.has_value());
    REQUIRE((snapshot->records.size() == 2));
    REQUIRE((snapshot->droppedRecordCount == 1));
    REQUIRE((snapshot->records.front().message == "two"));
    REQUIRE((snapshot->records.front().source->line == 12));
    REQUIRE((!store.SnapshotIfChanged(snapshot->revision).has_value()));
}

TEST_CASE("Operation store enforces progress and terminal retention", "[foundation][jobs]") {
    Horo::OperationStore store{2, 1};
    const auto first = store.Begin({.title = "First", .phase = "scan", .progress = 0.4F});
    REQUIRE(first.has_value());
    REQUIRE(store.Update(*first, {.state = Horo::OperationState::Running, .phase = "scan", .progress = 0.7F}));
    REQUIRE_FALSE(store.Update(*first, {.state = Horo::OperationState::Running, .phase = "scan", .progress = 0.6F}));
    REQUIRE(store.Update(*first, {.state = Horo::OperationState::Running, .phase = "write", .progress = 0.1F}));
    REQUIRE_FALSE(store.Update(*first, {.state = Horo::OperationState::Queued, .phase = "write", .progress = 0.2F}));
    REQUIRE_FALSE(store.Update(*first, {.state = Horo::OperationState::Running, .progress = 0.05F}));
    REQUIRE(store.Update(*first, {.state = Horo::OperationState::Succeeded, .phase = "complete", .progress = 1.0F}));

    const auto second = store.Begin({.title = "Second"});
    REQUIRE(second.has_value());
    REQUIRE(store.Update(*second, {.state = Horo::OperationState::Failed, .phase = "failed"}));

    const auto snapshot = store.SnapshotIfChanged(0);
    REQUIRE(snapshot.has_value());
    REQUIRE((snapshot->operations.size() == 1));
    REQUIRE((snapshot->operations.front().id == *second));
    REQUIRE((snapshot->droppedTerminalCount == 1));
}

TEST_CASE("Operation cancellation dispatches once and completion wins the race safely", "[foundation][jobs]") {
    Horo::OperationStore store{4, 4};
    std::atomic<unsigned> cancelCalls{};
    const auto id = store.Begin({.title = "Cook", .cancellable = true, .requestCancel = [&cancelCalls] {
        cancelCalls.fetch_add(1);
    }});
    REQUIRE(id.has_value());

    std::vector<std::thread> callers;
    for (unsigned index = 0; index < 8; ++index)
        callers.emplace_back([&store, id] {
            static_cast<void>(store.RequestCancel(*id));
        });
    for (std::thread &caller : callers)
        caller.join();

    REQUIRE((cancelCalls.load() == 1));
    REQUIRE(store.Update(*id, {.state = Horo::OperationState::Cancelled, .phase = "cancelled"}));
    REQUIRE_FALSE(store.RequestCancel(*id));
    REQUIRE_FALSE(store.Update(*id, {.state = Horo::OperationState::Succeeded, .phase = "complete"}));
}

TEST_CASE("Build output producers publish bounded monotonic snapshots concurrently", "[foundation][diagnostics][concurrency]") {
    constexpr std::size_t producerCount = 4;
    constexpr std::size_t recordsPerProducer = 100;
    constexpr std::size_t capacity = 64;
    Horo::BuildOutputStore store{capacity};

    std::vector<std::thread> producers;
    producers.reserve(producerCount);
    for (std::size_t producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([&store, producer] {
            for (std::size_t record = 0; record < recordsPerProducer; ++record) {
                store.Append({.status = Horo::BuildOutputStatus::Info,
                              .phase = "concurrent",
                              .message = std::to_string(producer) + ':' + std::to_string(record)});
            }
        });
    }
    for (auto &producer : producers)
        producer.join();

    const auto snapshot = store.SnapshotIfChanged(0);
    REQUIRE(snapshot.has_value());
    REQUIRE((snapshot->records.size() == capacity));
    REQUIRE((snapshot->droppedRecordCount == producerCount * recordsPerProducer - capacity));
    for (std::size_t index = 1; index < snapshot->records.size(); ++index)
        REQUIRE((snapshot->records[index - 1].sequence < snapshot->records[index].sequence));
}

TEST_CASE("Operation snapshots remain owned while a producer completes", "[foundation][jobs][concurrency]") {
    Horo::OperationStore store{2, 2};
    const auto id = store.Begin({.title = "Validation", .phase = "scan", .progress = 0.0F});
    REQUIRE(id.has_value());

    std::atomic<bool> producerDone{false};
    std::atomic<bool> producerSucceeded{true};
    std::thread producer{[&store, id, &producerDone, &producerSucceeded] {
        for (unsigned step = 1; step <= 100; ++step) {
            if (!store.Update(*id, {.state = Horo::OperationState::Running,
                                    .phase = "scan",
                                    .message = std::to_string(step),
                                    .progress = static_cast<float>(step) / 100.0F})) {
                producerSucceeded.store(false, std::memory_order_release);
                break;
            }
        }
        if (producerSucceeded.load(std::memory_order_acquire) &&
            !store.Update(*id, {.state = Horo::OperationState::Succeeded, .phase = "complete", .message = "complete", .progress = 1.0F}))
            producerSucceeded.store(false, std::memory_order_release);
        producerDone.store(true, std::memory_order_release);
    }};

    std::uint64_t revision = 0;
    std::vector<Horo::OperationStoreSnapshot> ownedSnapshots;
    while (!producerDone.load(std::memory_order_acquire)) {
        if (auto snapshot = store.SnapshotIfChanged(revision); snapshot.has_value()) {
            REQUIRE((snapshot->revision > revision));
            revision = snapshot->revision;
            ownedSnapshots.push_back(std::move(*snapshot));
        }
        std::this_thread::yield();
    }
    producer.join();
    REQUIRE(producerSucceeded.load(std::memory_order_acquire));
    if (auto snapshot = store.SnapshotIfChanged(revision); snapshot.has_value())
        ownedSnapshots.push_back(std::move(*snapshot));

    REQUIRE_FALSE(ownedSnapshots.empty());
    REQUIRE((ownedSnapshots.back().operations.size() == 1));
    REQUIRE((ownedSnapshots.back().operations.front().state == Horo::OperationState::Succeeded));
    for (const auto &snapshot : ownedSnapshots)
        REQUIRE((snapshot.operations.size() <= snapshot.activeCapacity + snapshot.recentCapacity));
}
