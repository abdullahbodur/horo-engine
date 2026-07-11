#pragma once

#include <atomic>
#include <memory>

namespace Horo
{
    /** @brief Cheap immutable observer of cooperative cancellation state. */
    class CancellationToken
    {
    public:
        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            for (auto state = m_state; state; state = state->parent)
            {
                if (state->requested.load()) return true;
            }
            return false;
        }
    private:
        struct State
        {
            std::atomic<bool> requested{false};
            std::shared_ptr<const State> parent;
        };
        friend class CancellationSource;
        explicit CancellationToken(std::shared_ptr<const State> state) : m_state(std::move(state)) {}
        std::shared_ptr<const State> m_state;
    };

    /** @brief Owner that can request cancellation for its token and derived children. */
    class CancellationSource
    {
    public:
        CancellationSource() : m_state(std::make_shared<CancellationToken::State>()) {}
        explicit CancellationSource(const CancellationToken &parent) : m_state(std::make_shared<CancellationToken::State>())
        {
            m_state->parent = parent.m_state;
        }
        [[nodiscard]] CancellationToken Token() const noexcept { return CancellationToken(m_state); }
        void RequestCancellation() noexcept { m_state->requested.store(true); }
    private:
        std::shared_ptr<CancellationToken::State> m_state;
    };
} // namespace Horo
