#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/ModuleHost.h"
#include "foundation/FoundationErrors.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>

namespace Horo {
    /** @brief Shared callback-admission and drainage state retained by outstanding leases. */
    class ModuleCallbackGate {
    public:
        std::mutex mutex;
        std::condition_variable drained;
        std::size_t activeCallbacks{};
        bool accepting{true};
    };

    ModuleCallbackLease::ModuleCallbackLease(std::shared_ptr<ModuleCallbackGate> gate, const void *bindings,
                                             CancellationToken cancellation) noexcept
        : m_gate(std::move(gate)), m_bindings(bindings), m_cancellation(std::move(cancellation)) {}

    ModuleCallbackLease::~ModuleCallbackLease() {
        Release();
    }

    ModuleCallbackLease::ModuleCallbackLease(ModuleCallbackLease &&other) noexcept
        : m_gate(std::move(other.m_gate)), m_bindings(other.m_bindings), m_cancellation(std::move(other.m_cancellation)) {
        other.m_bindings = nullptr;
    }

    ModuleCallbackLease &ModuleCallbackLease::operator=(ModuleCallbackLease &&other) noexcept {
        if (this == &other)
            return *this;
        Release();
        m_gate = std::move(other.m_gate);
        m_bindings = other.m_bindings;
        m_cancellation = std::move(other.m_cancellation);
        other.m_bindings = nullptr;
        return *this;
    }

    /** @copydoc ModuleCallbackLease::Bindings */
    const void *ModuleCallbackLease::Bindings() const noexcept {
        return m_bindings;
    }

    /** @copydoc ModuleCallbackLease::Cancellation */
    CancellationToken ModuleCallbackLease::Cancellation() const noexcept {
        return m_cancellation;
    }

    void ModuleCallbackLease::Release() noexcept {
        if (m_gate == nullptr)
            return;
        {
            const std::lock_guard lock(m_gate->mutex);
            --m_gate->activeCallbacks;
            if (m_gate->activeCallbacks == 0)
                m_gate->drained.notify_all();
        }
        m_gate.reset();
        m_bindings = nullptr;
    }

    /** @copydoc ModuleActivationContext::ModuleActivationContext */
    ModuleActivationContext::ModuleActivationContext(ModuleId identifier, const DependencyBindings bindings)
        : m_module(std::move(identifier)), m_bindings(bindings), m_callbackGate(std::make_shared<ModuleCallbackGate>()) {}

    ModuleActivationContext::~ModuleActivationContext() {
        RequestShutdown();
        DrainCallbacks();
    }

    /** @copydoc ModuleActivationContext::Module */
    const ModuleId &ModuleActivationContext::Module() const noexcept {
        return m_module;
    }

    /** @copydoc ModuleActivationContext::Bindings */
    ModuleActivationContext::DependencyBindings ModuleActivationContext::Bindings() const noexcept {
        return m_bindings;
    }

    /** @copydoc ModuleActivationContext::Cancellation */
    CancellationToken ModuleActivationContext::Cancellation() const noexcept {
        return m_cancellation.Token();
    }

    /** @copydoc ModuleActivationContext::AcquireCallbackLease */
    std::optional<ModuleCallbackLease> ModuleActivationContext::AcquireCallbackLease() const {
        const std::lock_guard lock(m_callbackGate->mutex);
        if (!m_callbackGate->accepting)
            return std::nullopt;
        ++m_callbackGate->activeCallbacks;
        return ModuleCallbackLease(m_callbackGate, m_bindings, m_cancellation.Token());
    }

    /** @copydoc ModuleActivationContext::AttachInstance */
    Result<void> ModuleActivationContext::AttachInstance(std::unique_ptr<IModuleInstance> instance) {
        if (instance == nullptr) {
            return Result<void>::Failure(
                MakeError(ModuleDescriptorErrors::InvalidDescriptor, "Module '" + m_module.value + "' attached a null instance."));
        }
        m_instance = std::move(instance);
        return Result<void>::Success();
    }

    /** @copydoc ModuleActivationContext::RequestShutdown */
    void ModuleActivationContext::RequestShutdown() noexcept {
        {
            const std::lock_guard lock(m_callbackGate->mutex);
            m_callbackGate->accepting = false;
        }
        m_cancellation.RequestCancellation();
    }

    /** @copydoc ModuleActivationContext::DrainCallbacks */
    void ModuleActivationContext::DrainCallbacks() noexcept {
        std::unique_lock lock(m_callbackGate->mutex);
        constexpr auto kDrainTimeout = std::chrono::seconds(5);
        const bool drained = m_callbackGate->drained.wait_for(lock, kDrainTimeout, [this] {
            return m_callbackGate->activeCallbacks == 0;
        });
        if (!drained) {
            Log::Logger::Write("foundation.modules", Log::Level::Warn,
                               "Module '" + m_module.value + "' callback drainage timed out with " +
                                   std::to_string(m_callbackGate->activeCallbacks) + " active callback(s).");
        }
    }
}  // namespace Horo
