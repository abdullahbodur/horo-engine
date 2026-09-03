#include "GameServiceRegistryDetail.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace Horo::Gameplay::Detail {
    namespace {
        using ServiceIndex = std::unordered_map<std::string_view, std::size_t>;

        [[nodiscard]] ServiceIndex IndexServices(const std::span<const GameplayServiceRegistration> registrations) {
            ServiceIndex index;
            index.reserve(registrations.size());
            for (std::size_t position = 0; position < registrations.size(); ++position)
                index.try_emplace(registrations[position].descriptor.id.Value(), position);
            return index;
        }

        [[nodiscard]] Result<void> ValidateDependencies(const std::span<const GameplayServiceRegistration> registrations,
                                                        const ServiceIndex &index) {
            for (const GameplayServiceRegistration &registration : registrations) {
                for (const GameplayServiceId &dependency : registration.descriptor.dependencies) {
                    const auto provider = index.find(dependency.Value());
                    if (provider == index.end())
                        return Result<void>::Failure(MakeError(GameplayErrors::ServiceDependencyMissing));
                    if (registration.descriptor.scope == GameplayServiceScope::Project &&
                        registrations[provider->second].descriptor.scope == GameplayServiceScope::Scene)
                        return Result<void>::Failure(MakeError(GameplayErrors::ServiceScopeViolation));
                }
            }
            return Result<void>::Success();
        }

        using CapabilityProviders = std::unordered_map<std::string_view, std::size_t>;

        [[nodiscard]] Result<CapabilityProviders> IndexCapabilityProviders(const std::span<const GameplayServiceRegistration> registrations,
                                                                           const std::span<const GameplayCapabilityId> hostCapabilities) {
            constexpr std::size_t HostProvider = std::numeric_limits<std::size_t>::max();
            CapabilityProviders providers;
            providers.reserve(hostCapabilities.size() + registrations.size());
            for (const GameplayCapabilityId &capability : hostCapabilities) {
                if (!capability.IsValid() || !providers.try_emplace(capability.Value(), HostProvider).second)
                    return Result<CapabilityProviders>::Failure(MakeError(GameplayErrors::InvalidServiceDescriptor));
            }
            for (std::size_t index = 0; index < registrations.size(); ++index) {
                for (const GameplayCapabilityId &capability : registrations[index].descriptor.providedCapabilities) {
                    if (!providers.try_emplace(capability.Value(), index).second)
                        return Result<CapabilityProviders>::Failure(MakeError(GameplayErrors::InvalidServiceDescriptor));
                }
            }
            return Result<CapabilityProviders>::Success(std::move(providers));
        }

        [[nodiscard]] Result<void> ValidateCapabilityRequirement(const GameplayServiceRegistration &registration,
                                                                 const GameplayCapabilityId &required,
                                                                 const std::span<const GameplayServiceRegistration> registrations,
                                                                 const CapabilityProviders &providers) {
            constexpr std::size_t HostProvider = std::numeric_limits<std::size_t>::max();
            const auto provider = providers.find(required.Value());
            if (provider == providers.end())
                return Result<void>::Failure(MakeError(GameplayErrors::CapabilityMissing));
            if (provider->second != HostProvider && registration.descriptor.scope == GameplayServiceScope::Project &&
                registrations[provider->second].descriptor.scope == GameplayServiceScope::Scene)
                return Result<void>::Failure(MakeError(GameplayErrors::ServiceScopeViolation));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateCapabilities(const std::span<const GameplayServiceRegistration> registrations,
                                                        const std::span<const GameplayCapabilityId> hostCapabilities) {
            Result<CapabilityProviders> indexed = IndexCapabilityProviders(registrations, hostCapabilities);
            if (indexed.HasError())
                return Result<void>::Failure(indexed.ErrorValue());
            const CapabilityProviders &providers = indexed.Value();
            for (const GameplayServiceRegistration &registration : registrations) {
                for (const GameplayCapabilityId &required : registration.descriptor.requiredCapabilities) {
                    if (Result<void> valid = ValidateCapabilityRequirement(registration, required, registrations, providers);
                        valid.HasError())
                        return valid;
                }
            }
            return Result<void>::Success();
        }
    }  // namespace

    Result<void> ValidateServiceGraph(const std::span<const GameplayServiceRegistration> registrations,
                                      const std::span<const GameplayCapabilityId> hostCapabilities) {
        const ServiceIndex index = IndexServices(registrations);
        if (const Result<void> dependencies = ValidateDependencies(registrations, index); dependencies.HasError())
            return dependencies;
        return ValidateCapabilities(registrations, hostCapabilities);
    }
}  // namespace Horo::Gameplay::Detail
