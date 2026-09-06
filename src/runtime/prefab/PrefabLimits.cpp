#include "Horo/Prefab/PrefabLimits.h"

#include "Horo/Prefab/PrefabErrors.h"

#include <array>
#include <limits>

namespace Horo::Prefab {
    namespace {
        struct Limit final {
            std::size_t value;
            std::size_t maximum;
        };

        [[nodiscard]] bool AddChecked(std::size_t &total, const std::size_t value) noexcept {
            if (value > std::numeric_limits<std::size_t>::max() - total)
                return false;
            total += value;
            return true;
        }

        [[nodiscard]] bool MultiplyChecked(const std::size_t left, const std::size_t right, std::size_t &result) noexcept {
            if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
                return false;
            result = left * right;
            return true;
        }
    }  // namespace

    /** @copydoc PrefabLimitProfile::Create */
    Result<PrefabLimitProfile> PrefabLimitProfile::Create(PrefabProjectPolicy policy) {
        const std::array limits{
            Limit{policy.maximumHierarchyDepth, PrefabHardLimits::SourceHierarchyDepth},
            Limit{policy.maximumObjectCount, PrefabHardLimits::SourceObjectCount},
            Limit{policy.maximumSourcePayloadBytes, PrefabHardLimits::SourcePayloadBytes},
            Limit{policy.maximumExpandedPayloadBytes, PrefabHardLimits::ExpandedPayloadBytes},
            Limit{policy.maximumCookedPayloadBytes, PrefabHardLimits::CookedPayloadBytes},
            Limit{policy.maximumComponentsPerObject, PrefabHardLimits::ComponentsPerObject},
            Limit{policy.maximumReferencedAssets, PrefabHardLimits::ReferencedAssets},
            Limit{policy.maximumDirectNestedPlacements, PrefabHardLimits::DirectNestedPlacements},
            Limit{policy.maximumVariantInheritanceDepth, PrefabHardLimits::VariantInheritanceDepth},
            Limit{policy.maximumNestedPrefabDepth, PrefabHardLimits::NestedPrefabDepth},
            Limit{policy.maximumOverrideRecords, PrefabHardLimits::OverrideRecords},
            Limit{policy.maximumConflictAndOrphanRecords, PrefabHardLimits::ConflictAndOrphanRecords},
            Limit{policy.maximumPropertyPathSegments, PrefabHardLimits::PropertyPathSegments},
            Limit{policy.maximumOverrideValueBytes, PrefabHardLimits::OverrideValueBytes},
            Limit{policy.maximumOverrideSetBytes, PrefabHardLimits::OverrideSetBytes},
            Limit{policy.maximumBindingSlots, PrefabHardLimits::BindingSlots},
            Limit{policy.maximumBindingUses, PrefabHardLimits::BindingUses},
            Limit{policy.maximumInstanceBindings, PrefabHardLimits::InstanceBindings},
            Limit{policy.maximumRuntimeSpawnDepth, PrefabHardLimits::RuntimeSpawnDepth},
        };
        for (const Limit limit : limits) {
            if (limit.value == 0 || limit.value > limit.maximum)
                return Result<PrefabLimitProfile>::Failure(MakeError(PrefabErrors::LimitProfileInvalid));
        }

        if (policy.maximumOverrideValueBytes > policy.maximumOverrideSetBytes)
            return Result<PrefabLimitProfile>::Failure(MakeError(PrefabErrors::LimitProfileInvalid));

        std::size_t componentWork{};
        if (!MultiplyChecked(policy.maximumObjectCount, policy.maximumComponentsPerObject, componentWork))
            return Result<PrefabLimitProfile>::Failure(MakeError(PrefabErrors::LimitProfileInvalid));
        std::size_t overridePathWork{};
        if (!MultiplyChecked(policy.maximumOverrideRecords, policy.maximumPropertyPathSegments, overridePathWork))
            return Result<PrefabLimitProfile>::Failure(MakeError(PrefabErrors::LimitProfileInvalid));
        std::size_t workItems{};
        for (const std::size_t value :
             {policy.maximumObjectCount, componentWork, policy.maximumReferencedAssets, policy.maximumDirectNestedPlacements,
              policy.maximumVariantInheritanceDepth, policy.maximumNestedPrefabDepth, overridePathWork,
              policy.maximumConflictAndOrphanRecords, policy.maximumBindingUses}) {
            if (!AddChecked(workItems, value))
                return Result<PrefabLimitProfile>::Failure(MakeError(PrefabErrors::LimitProfileInvalid));
        }
        return Result<PrefabLimitProfile>::Success(PrefabLimitProfile{std::move(policy), workItems});
    }

    /** @copydoc PrefabLimitProfile::Policy */
    const PrefabProjectPolicy &PrefabLimitProfile::Policy() const noexcept {
        return policy_;
    }

    /** @copydoc PrefabLimitProfile::MaximumExpansionWorkItems */
    std::size_t PrefabLimitProfile::MaximumExpansionWorkItems() const noexcept {
        return maximumExpansionWorkItems_;
    }

    /** @copydoc PrefabExpansionBudget::PrefabExpansionBudget */
    PrefabExpansionBudget::PrefabExpansionBudget(const PrefabLimitProfile &profile) noexcept
        : remaining_(profile.MaximumExpansionWorkItems()) {}

    /** @copydoc PrefabExpansionBudget::PrefabExpansionBudget(PrefabExpansionBudget&&) */
    PrefabExpansionBudget::PrefabExpansionBudget(PrefabExpansionBudget &&other) noexcept : remaining_(std::exchange(other.remaining_, 0)) {}

    /** @copydoc PrefabExpansionBudget::operator= */
    PrefabExpansionBudget &PrefabExpansionBudget::operator=(PrefabExpansionBudget &&other) noexcept {
        remaining_ = std::exchange(other.remaining_, 0);
        return *this;
    }

    /** @copydoc PrefabExpansionBudget::Consume */
    Result<void> PrefabExpansionBudget::Consume(const std::size_t workItems) {
        if (workItems > remaining_)
            return Result<void>::Failure(MakeError(PrefabErrors::WorkBudgetExceeded));
        remaining_ -= workItems;
        return Result<void>::Success();
    }

    /** @copydoc PrefabExpansionBudget::Remaining */
    std::size_t PrefabExpansionBudget::Remaining() const noexcept {
        return remaining_;
    }
}  // namespace Horo::Prefab
