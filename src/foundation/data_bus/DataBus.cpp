#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/Logging/Logger.h"


#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Horo
{
    struct EngineDataBus::State
    {
        struct Record { std::uint64_t id; Handler handler; };
        struct Queued { EventTypeId type; std::string name; std::shared_ptr<const EventPayload> payload; QueuedPublisher publish; };
        EngineDataBusConfig config;
        std::mutex mutex;
        std::unordered_map<EventTypeId, std::vector<Record>> handlers;
        std::deque<Queued> queued;
        std::vector<EventTypeId> activeTypes;
        std::vector<DeferredPublisher> deferred;
        std::uint64_t nextId = 1;
        explicit State(EngineDataBusConfig value) : config(value) {}
    };

    Subscription::~Subscription() { Reset(); }
    Subscription::Subscription(Subscription &&other) noexcept : m_release(std::move(other.m_release)) { other.m_release = nullptr; }
    Subscription &Subscription::operator=(Subscription &&other) noexcept { if (this != &other) { Reset(); m_release = std::move(other.m_release); other.m_release = nullptr; } return *this; }
    void Subscription::Reset() noexcept { if (m_release) { m_release(); m_release = {}; } }

    EngineDataBus::EngineDataBus(EngineDataBusConfig config) : m_state(std::make_shared<State>(config)) {}
    EngineDataBus::~EngineDataBus() { Clear(); }
    EngineDataBus::EngineDataBus(EngineDataBus &&) noexcept = default;
    EngineDataBus &EngineDataBus::operator=(EngineDataBus &&) noexcept = default;

    Subscription EngineDataBus::SubscribeErased(const EventTypeId type, const std::string_view name, Handler handler)
    {
        const auto state = m_state;
        std::uint64_t id = 0;
        { std::lock_guard lock(state->mutex); id = state->nextId++; state->handlers[type].push_back({id, std::move(handler)}); }
        LOG_TRACE(state->config.logCategory, "subscribe event=%s handler=%llu", name.data(), static_cast<unsigned long long>(id));
        return Subscription([weak = std::weak_ptr<State>(state), type, id, category = state->config.logCategory, eventName = std::string(name)] {
            if (const auto locked = weak.lock()) { std::lock_guard lock(locked->mutex); auto it = locked->handlers.find(type); if (it != locked->handlers.end()) { auto &records = it->second; records.erase(std::remove_if(records.begin(), records.end(), [id](const State::Record &record) { return record.id == id; }), records.end()); } LOG_TRACE(category, "unsubscribe event=%s handler=%llu", eventName.c_str(), static_cast<unsigned long long>(id)); }
        });
    }

    void EngineDataBus::PublishErased(const EventTypeId type, const std::string_view name, const EventPayload *raw, DeferredPublisher retry)
    {
        const auto state = m_state;
        if (std::find(state->activeTypes.begin(), state->activeTypes.end(), type) != state->activeTypes.end()) { state->deferred.push_back(std::move(retry)); return; }
        std::vector<Handler> snapshot;
        { std::lock_guard lock(state->mutex); if (const auto it = state->handlers.find(type); it != state->handlers.end()) for (const auto &record : it->second) snapshot.push_back(record.handler); }
        const auto start = std::chrono::steady_clock::now();
        LOG_TRACE(state->config.logCategory, "publish event=%s handlers=%zu", name.data(), snapshot.size());
        state->activeTypes.push_back(type);
        for (const auto &handler : snapshot) { try { handler(raw); } catch (const std::exception &exception) { LOG_ERROR(state->config.logCategory, "handler failed event=%s error=%s", name.data(), exception.what()); } catch (...) { LOG_ERROR(state->config.logCategory, "handler failed event=%s error=unknown", name.data()); } }
        state->activeTypes.pop_back();
        LOG_TRACE(state->config.logCategory, "dispatch complete event=%s elapsed_us=%lld", name.data(), static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()));
        if (state->activeTypes.empty()) { auto deferred = std::move(state->deferred); state->deferred.clear(); for (auto &publish : deferred) publish(*this); }
    }

    void EngineDataBus::QueueErased(const EventTypeId type, const std::string_view name, std::shared_ptr<const EventPayload> payload, QueuedPublisher publish)
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->queued.size() >= m_state->config.maxAsyncQueueSize) { LOG_TRACE(m_state->config.logCategory, "async drop event=%s reason=queue_full", name.data()); return; }
        m_state->queued.push_back({type, std::string(name), std::move(payload), std::move(publish)});
    }
    void EngineDataBus::DispatchQueued() { std::deque<State::Queued> queued; { std::lock_guard lock(m_state->mutex); queued.swap(m_state->queued); } for (auto &event : queued) event.publish(*this, event.payload.get()); }
    void EngineDataBus::Clear() { if (!m_state) return; std::lock_guard lock(m_state->mutex); m_state->handlers.clear(); m_state->queued.clear(); }
}


