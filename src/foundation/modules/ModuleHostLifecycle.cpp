#include "Horo/Foundation/ModuleHost.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <utility>

namespace Horo {
    namespace {
        /** @brief Returns whether one host-owned module state transition is legal. */
        [[nodiscard]] constexpr bool IsLegalTransition(const ModuleLifecycleState from, const ModuleLifecycleState to) noexcept {
            using enum ModuleLifecycleState;
            constexpr std::array<std::pair<ModuleLifecycleState, ModuleLifecycleState>, 7> kAllowedTransitions{{
                {Registered, Activating},
                {Registered, Stopped},
                {Activating, Active},
                {Activating, Failed},
                {Active, CancellationRequested},
                {CancellationRequested, Draining},
                {Draining, Stopped},
            }};
            return std::ranges::find(kAllowedTransitions, std::pair{from, to}) != kAllowedTransitions.end();
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
        if (found == m_states.end() || !IsLegalTransition(found->state, state)) {
            assert(found != m_states.end() && "Unknown module identity in lifecycle transition.");
            assert(found != m_states.end() && IsLegalTransition(found->state, state) && "Illegal module lifecycle transition.");
            return;
        }
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

    void ModuleHost::StopUnactivatedRegistered() noexcept {
        for (const ModuleDescriptor &pending : m_registered) {
            if (StateOf(pending.id) == ModuleLifecycleState::Registered)
                Transition(pending.id, ModuleLifecycleState::Stopped);
        }
        m_registered.clear();
    }

    void ModuleHost::RollbackActivation(const ModuleDescriptor &failedDescriptor,
                                        std::unique_ptr<ModuleActivationContext> failedContext,
                                        const std::size_t base) noexcept {
        Transition(failedDescriptor.id, ModuleLifecycleState::Failed);
        failedContext->RequestShutdown();
        RequestCancellationFrom(base);
        failedContext->DrainCallbacks();
        failedContext.reset();
        DeactivateFrom(base);
        StopUnactivatedRegistered();
    }
}  // namespace Horo
