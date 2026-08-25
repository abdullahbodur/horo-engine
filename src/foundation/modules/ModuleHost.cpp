#include "Horo/Foundation/ModuleHost.h"

#include "foundation/FoundationErrors.h"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace Horo {
    namespace {
        /** @brief Returns a typed composition failure from a stable error descriptor. */
        template <typename T> [[nodiscard]] Result<T> Fail(const ErrorCodeDescriptor &descriptor, std::string message) {
            return Result<T>::Failure(MakeError(descriptor, std::move(message)));
        }

        /** @brief Maps validated initialization order back to registered descriptor pointers. */
        [[nodiscard]] std::vector<const ModuleDescriptor *> OrderRegisteredDescriptors(const std::vector<ModuleDescriptor> &registered,
                                                                                       const std::vector<ModuleId> &order) {
            std::vector<const ModuleDescriptor *> result;
            result.reserve(order.size());
            for (const ModuleId &id : order) {
                const auto found = std::ranges::find_if(registered, [&id](const ModuleDescriptor &d) {
                    return d.id == id;
                });
                result.push_back(std::to_address(found));
            }
            return result;
        }
    }  // namespace

    ModuleHost::~ModuleHost() {
        DeactivateAll();
    }

    /** @copydoc ModuleHost::ActivateRegistered */
    Result<std::size_t> ModuleHost::ActivateRegistered(const ModuleActivationContext::DependencyBindings bindings) {
        // Graph validation runs before any callback: a rejected set leaves the host untouched.
        auto validated = ValidateModuleGraph(m_registered);
        if (validated.HasError())
            return Result<std::size_t>::Failure(validated.ErrorValue());

        const std::vector<const ModuleDescriptor *> ordered =
            OrderRegisteredDescriptors(m_registered, validated.Value().initializationOrder);

        // Roll back only modules started by this call; prior activations own the front of the list.
        const std::size_t base = m_active.size();
        m_active.reserve(base + ordered.size());
        for (const ModuleDescriptor *descriptor : ordered) {
            auto context = std::make_unique<ModuleActivationContext>(descriptor->id, bindings);
            Transition(descriptor->id, ModuleLifecycleState::Activating);
            if (descriptor->lifecycle.activate != nullptr) {
                if (const Result<void> r = descriptor->lifecycle.activate(*context); r.HasError()) {
                    const std::string failedId = descriptor->id.value;
                    RollbackActivation(descriptor->id, std::move(context), base);
                    return Fail<std::size_t>(ModuleDescriptorErrors::InvalidDescriptor,
                                             std::format("Activation of module '{}' failed.", failedId));
                }
            }
            m_active.push_back(ActiveModule{.id = descriptor->id,
                                            .drain = descriptor->lifecycle.drain,
                                            .deactivate = descriptor->lifecycle.deactivate,
                                            .context = std::move(context)});
            Transition(descriptor->id, ModuleLifecycleState::Active);
        }
        m_registered.clear();
        return Result<std::size_t>::Success(m_active.size() - base);
    }

    /** @copydoc ModuleHost::DeactivateAll */
    void ModuleHost::DeactivateAll() noexcept {
        RequestCancellationFrom(0);
        DeactivateFrom(0);
        StopUnactivatedRegistered();
    }

    /** @copydoc ModuleHost::HasActiveModules */
    bool ModuleHost::HasActiveModules() const noexcept {
        return !m_active.empty();
    }
}  // namespace Horo
