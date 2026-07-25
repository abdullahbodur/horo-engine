/**
 * @copydoc CookCatalog.h
 */

#include "Horo/Assets/CookCatalog.h"

#include "Horo/Foundation/Result.h"
#include "../AssetErrors.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>

namespace Horo::Assets
{

// ---------------------------------------------------------------------------
// CookerContribution
// ---------------------------------------------------------------------------

bool CookerContribution::Handles(const AssetTypeId &type, const AssetCookTargetId &target) const noexcept
{
    if (assetType != type)
        return false;
    return std::find_if(targets.begin(), targets.end(),
                        [&](const AssetCookTargetId &t) { return t == target; }) != targets.end();
}

// ---------------------------------------------------------------------------
// CookerCatalogSnapshot
// ---------------------------------------------------------------------------

CookerCatalogSnapshot::CookerCatalogSnapshot(std::vector<CookerContribution> entries)
    : entries_(std::move(entries))
{
}

const ICookerStrategy *CookerCatalogSnapshot::Find(const AssetTypeId &type,
                                                    const AssetCookTargetId &target) const noexcept
{
    for (const auto &entry : entries_)
    {
        if (entry.Handles(type, target))
            return entry.strategy.get();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// CookerCatalog
// ---------------------------------------------------------------------------

struct CookerCatalog::State
{
    std::vector<CookerContribution> entries;
    std::shared_ptr<const CookerCatalogSnapshot> published;
    bool sealed{false};
};

CookerCatalog::CookerCatalog() : state_(std::make_unique<State>()) {}
CookerCatalog::~CookerCatalog() = default;

Result<void> CookerCatalog::Register(CookerContribution entry)
{
    if (state_->sealed)
    {
        return Result<void>::Failure(
            Error{CookErrors::CatalogSealed.code});
    }

    // Check for duplicate contribution ID
    for (const auto &existing : state_->entries)
    {
        if (existing.contributionId == entry.contributionId)
        {
            return Result<void>::Failure(
                Error{CookErrors::CatalogSealed.code});
        }
    }

    // Check for duplicate asset type + target claims
    for (const auto &existing : state_->entries)
    {
        if (existing.assetType != entry.assetType)
            continue;
        for (const auto &newTarget : entry.targets)
        {
            if (existing.Handles(existing.assetType, newTarget))
            {
                return Result<void>::Failure(
                    Error{CookErrors::DuplicateCooker.code});
            }
        }
    }

    state_->entries.push_back(std::move(entry));
    return Result<void>::Success();
}

Result<std::shared_ptr<const CookerCatalogSnapshot>> CookerCatalog::Publish()
{
    if (state_->sealed)
    {
        return Result<std::shared_ptr<const CookerCatalogSnapshot>>::Failure(
            Error{CookErrors::CatalogSealed.code});
    }

    // Sort entries deterministically: by asset type, then target, then contribution ID
    std::sort(state_->entries.begin(), state_->entries.end(),
              [](const CookerContribution &a, const CookerContribution &b) {
                  if (a.assetType.Value() != b.assetType.Value())
                      return a.assetType.Value() < b.assetType.Value();
                  // Compare by first target for stable ordering
                  const auto aTarget = a.targets.empty() ? "" : a.targets.front().Value();
                  const auto bTarget = b.targets.empty() ? "" : b.targets.front().Value();
                  if (aTarget != bTarget)
                      return aTarget < bTarget;
                  return a.contributionId < b.contributionId;
              });

    auto snapshot = std::make_shared<CookerCatalogSnapshot>(state_->entries);
    state_->published = snapshot;
    state_->sealed = true;
    return Result<std::shared_ptr<const CookerCatalogSnapshot>>::Success(snapshot);
}

std::shared_ptr<const CookerCatalogSnapshot> CookerCatalog::Snapshot() const noexcept
{
    return state_->published;
}

bool CookerCatalog::IsSealed() const noexcept
{
    return state_->sealed;
}

void CookerCatalog::Reset()
{
    if (state_->sealed)
        return;
    state_->entries.clear();
}

} // namespace Horo::Assets
