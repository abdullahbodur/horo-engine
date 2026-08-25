#include "Horo/Foundation/ModuleHost.h"

#include <algorithm>
#include <cassert>

namespace Horo {
    namespace {
        /** @brief Returns whether one host-owned module state transition is legal. */
        [[nodiscard]] bool IsLegalTransition(const ModuleLifecycleState from, const ModuleLifecycleState to) noexcept {
            using enum ModuleLifecycleState;
            switch (from) {
                case Registered:
                    return to == Activating || to == Stopped;
                case Activating:
                    return to == Active || to == Failed;
                case Active:
                    return to == CancellationRequested;
                case CancellationRequested:
                    return to == Draining;
                case Draining:
                    return to == Stopped;
                case Stopped:
                case Failed:
                    return false;
            }
            return false;
        }
    }  // namespace

    /** @copydoc ModuleHost::StateOf */
    std::optional<ModuleLifecycleState> ModuleHost::StateOf(const ModuleId &id) const noexcept {
        const auto found = std::ranges::find_if(m_states, [&id](const ModuleStateRecord &record) {
            return record.id == id;
        });
        if (found == m_states.end())
            return std::nullopt;
        return found->state;
    }

    void ModuleHost::Transition(const ModuleId &id, const ModuleLifecycleState state) noexcept {
        const auto found = std::ranges::find_if(m_states, [&id](const ModuleStateRecord &record) {
            return record.id == id;
        });
        assert(found != m_states.end());
        assert(IsLegalTransition(found->state, state));
        found->state = state;
    }

    void ModuleHost::RequestCancellationFrom(const std::size_t base) noexcept {
        for (std::size_t index = m_active.size(); index > base; --index) {
            ActiveModule &active = m_active[index - 1];
            active.context->RequestShutdown();
            Transition(active.id, ModuleLifecycleState::CancellationRequested);
        }
    }

    void ModuleHost::DeactivateFrom(const std::size_t base) noexcept {
        while (m_active.size() > base) {
            ActiveModule &active = m_active.back();
            Transition(active.id, ModuleLifecycleState::Draining);
            active.context->DrainCallbacks();
            if (active.drain != nullptr)
                active.drain(*active.context);
            if (active.deactivate != nullptr)
                active.deactivate(*active.context);
            active.context.reset();
            Transition(active.id, ModuleLifecycleState::Stopped);
            m_active.pop_back();
        }
    }
}  // namespace Horo
