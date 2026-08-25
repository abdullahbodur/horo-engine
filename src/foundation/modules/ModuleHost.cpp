#include "Horo/Foundation/ModuleHost.h"

#include "foundation/FoundationErrors.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace Horo {
    namespace {
        /** @brief Creates a typed composition failure from a stable error descriptor. */
        template <typename ValueT> [[nodiscard]] Result<ValueT> Fail(const ErrorCodeDescriptor &descriptor, std::string message) {
            return Result<ValueT>::Failure(MakeError(descriptor, std::move(message)));
        }

        /** @brief Deactivates one active entry and releases its activation context. */
        void DeactivateOne(ActiveModule &active) noexcept {
            if (active.deactivate != nullptr && active.context != nullptr)
                active.deactivate(*active.context);
            active.context.reset();
        }

        /** @brief Rolls back every activation started after @p base in reverse order. */
        void RollBackTo(std::vector<ActiveModule> &activeModules, const std::size_t base) noexcept {
            while (activeModules.size() > base) {
                DeactivateOne(activeModules.back());
                activeModules.pop_back();
            }
        }
    }  // namespace

    /** @copydoc ModuleHost::ActivateRegistered */
    Result<std::size_t> ModuleHost::ActivateRegistered(const ModuleActivationContext::DependencyBindings bindings) {
        // Graph validation runs before any callback: a rejected set leaves the host untouched.
        const Result<ValidatedModuleGraph> validated = ValidateModuleGraph(m_registered);
        if (validated.HasError())
            return Result<std::size_t>::Failure(validated.ErrorValue());

        // Index descriptors by identity so activation follows the validated ID order.
        std::vector<const ModuleDescriptor *> ordered;
        ordered.reserve(validated.Value().initializationOrder.size());
        for (const ModuleId &id : validated.Value().initializationOrder) {
            const auto found = std::ranges::find_if(m_registered, [&id](const ModuleDescriptor &descriptor) {
                return descriptor.id == id;
            });
            ordered.push_back(std::to_address(found));
        }

        // Roll back only modules started by this call; a prior successful activation
        // may still own the front of the active list (incremental compose-then-activate).
        const std::size_t base = m_active.size();
        for (const ModuleDescriptor *descriptor : ordered) {
            auto context = std::make_unique<ModuleActivationContext>(descriptor->id, bindings);
            if (descriptor->lifecycle.activate != nullptr) {
                if (const Result<void> activated = descriptor->lifecycle.activate(*context); activated.HasError()) {
                    RollBackTo(m_active, base);
                    return Fail<std::size_t>(ModuleDescriptorErrors::InvalidDescriptor,
                                             "Activation of module '" + descriptor->id.value + "' failed.");
                }
            }
            ActiveModule active{.id = descriptor->id, .deactivate = descriptor->lifecycle.deactivate, .context = std::move(context)};
            m_active.push_back(std::move(active));
        }
        m_registered.clear();
        return Result<std::size_t>::Success(m_active.size() - base);
    }

    /** @copydoc ModuleHost::DeactivateAll */
    void ModuleHost::DeactivateAll() noexcept {
        RollBackTo(m_active, 0);
        m_registered.clear();
    }

    /** @copydoc ModuleHost::HasActiveModules */
    bool ModuleHost::HasActiveModules() const noexcept {
        return !m_active.empty();
    }
}  // namespace Horo
