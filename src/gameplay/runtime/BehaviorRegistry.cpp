#include "Horo/Gameplay/BehaviorRegistry.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <unordered_set>

namespace Horo::Gameplay {
    struct BehaviorRegistry::Impl {
        std::vector<BehaviorRegistration> registrations;
        bool frozen{};
    };

    namespace {
        [[nodiscard]] Result<void> ValidateDescriptor(const BehaviorRegistration &registration) {
            const BehaviorDescriptor &descriptor = registration.descriptor;
            if (!descriptor.typeId.IsValid() || descriptor.schemaVersion == 0 || descriptor.displayName.empty() ||
                !registration.factory.create)
                return Result<void>::Failure(
                    MakeError(GameplayErrors::InvalidBehaviorComponent, "Behavior descriptor or factory binding is incomplete."));

            std::unordered_set<std::string_view> fields;
            fields.reserve(descriptor.fields.size());
            for (const BehaviorFieldDescriptor &field : descriptor.fields) {
                if (field.name.empty() || field.name.size() > MaximumBehaviorFieldNameBytes || !fields.emplace(field.name).second)
                    return Result<void>::Failure(
                        MakeError(GameplayErrors::InvalidBehaviorComponent, "Behavior descriptor fields must have unique bounded names."));
            }
            std::unordered_set<std::string_view> nodes;
            nodes.reserve(descriptor.phases.size());
            for (const BehaviorPhaseDescriptor &phase : descriptor.phases) {
                if (phase.nodeId.empty() || !nodes.emplace(phase.nodeId).second)
                    return Result<void>::Failure(
                        MakeError(GameplayErrors::InvalidBehaviorComponent, "Behavior phase schedule nodes must be non-empty and unique."));
            }
            return Result<void>::Success();
        }
    }  // namespace

    BehaviorRegistry::BehaviorRegistry() : impl_(std::make_unique<Impl>()) {}

    BehaviorRegistry::~BehaviorRegistry() = default;
    BehaviorRegistry::BehaviorRegistry(BehaviorRegistry &&) noexcept = default;
    BehaviorRegistry &BehaviorRegistry::operator=(BehaviorRegistry &&) noexcept = default;

    /** @copydoc BehaviorRegistry::Register */
    Result<void> BehaviorRegistry::Register(BehaviorRegistration registration) {
        if (impl_->frozen)
            return Result<void>::Failure(MakeError(GameplayErrors::RegistryFrozen));
        if (const Result<void> valid = ValidateDescriptor(registration); valid.HasError())
            return valid;
        if (Find(registration.descriptor.typeId) != nullptr)
            return Result<void>::Failure(MakeError(GameplayErrors::DuplicateBehaviorType));
        impl_->registrations.push_back(std::move(registration));
        return Result<void>::Success();
    }

    /** @copydoc BehaviorRegistry::Freeze */
    Result<void> BehaviorRegistry::Freeze() {
        if (impl_->frozen)
            return Result<void>::Success();
        std::ranges::sort(impl_->registrations, {}, [](const BehaviorRegistration &registration) {
            return registration.descriptor.typeId.Value();
        });
        impl_->frozen = true;
        return Result<void>::Success();
    }

    /** @copydoc BehaviorRegistry::IsFrozen */
    bool BehaviorRegistry::IsFrozen() const noexcept {
        return impl_->frozen;
    }

    /** @copydoc BehaviorRegistry::Registrations */
    std::span<const BehaviorRegistration> BehaviorRegistry::Registrations() const noexcept {
        return impl_->registrations;
    }

    /** @copydoc BehaviorRegistry::Find */
    const BehaviorRegistration *BehaviorRegistry::Find(const BehaviorTypeId &typeId) const noexcept {
        const auto found = std::ranges::find(impl_->registrations, typeId, [](const BehaviorRegistration &registration) {
            return registration.descriptor.typeId;
        });
        return found == impl_->registrations.end() ? nullptr : &*found;
    }
}  // namespace Horo::Gameplay
