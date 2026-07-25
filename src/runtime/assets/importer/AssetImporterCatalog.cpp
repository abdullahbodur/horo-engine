/**
 * @copydoc AssetImporter.h
 */

#include "Horo/Assets/AssetImporter.h"

#include "../AssetErrors.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Assets
{

// ---------------------------------------------------------------------------
// AssetImporterContribution
// ---------------------------------------------------------------------------

bool AssetImporterContribution::HandlesExtension(std::string_view extension) const noexcept
{
    return std::find(fileExtensions.begin(), fileExtensions.end(), extension) != fileExtensions.end();
}

// ---------------------------------------------------------------------------
// AssetImporterCatalogSnapshot
// ---------------------------------------------------------------------------

const IAssetImporter *AssetImporterCatalogSnapshot::FindByExtension(std::string_view extension) const noexcept
{
    for (const auto &entry : entries_)
    {
        if (entry.HandlesExtension(extension))
            return entry.strategy.get();
    }
    return nullptr;
}

const AssetImporterContribution *AssetImporterCatalogSnapshot::FindById(std::string_view contributionId) const noexcept
{
    for (const auto &entry : entries_)
    {
        if (entry.contributionId == contributionId)
            return &entry;
    }
    return nullptr;
}

const AssetImporterContribution *AssetImporterCatalogSnapshot::FindContributionByExtension(
    std::string_view extension) const noexcept
{
    for (const auto &entry : entries_)
    {
        if (entry.HandlesExtension(extension))
            return &entry;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// AssetImporterCatalog
// ---------------------------------------------------------------------------

struct AssetImporterCatalog::State
{
    std::vector<AssetImporterContribution> entries;
    std::shared_ptr<const AssetImporterCatalogSnapshot> published;
    bool sealed{false};
};

AssetImporterCatalog::AssetImporterCatalog() : state_(std::make_unique<State>()) {}
AssetImporterCatalog::~AssetImporterCatalog() = default;

Result<void> AssetImporterCatalog::Register(AssetImporterContribution entry)
{
    if (state_->sealed)
        return Result<void>::Failure(Error{CookErrors::CatalogSealed.code});

    // Check for duplicate contribution ID
    for (const auto &existing : state_->entries)
    {
        if (existing.contributionId == entry.contributionId)
            return Result<void>::Failure(Error{CookErrors::DuplicateCooker.code});
    }

    state_->entries.push_back(std::move(entry));
    return Result<void>::Success();
}

Result<std::shared_ptr<const AssetImporterCatalogSnapshot>> AssetImporterCatalog::Publish()
{
    if (state_->sealed)
        return Result<std::shared_ptr<const AssetImporterCatalogSnapshot>>::Failure(
            Error{CookErrors::CatalogSealed.code});

    // Sort entries by first extension, then contribution ID
    std::sort(state_->entries.begin(), state_->entries.end(),
              [](const AssetImporterContribution &a, const AssetImporterContribution &b) {
                  const auto aExt = a.fileExtensions.empty() ? "" : a.fileExtensions.front();
                  const auto bExt = b.fileExtensions.empty() ? "" : b.fileExtensions.front();
                  if (aExt != bExt)
                      return aExt < bExt;
                  return a.contributionId < b.contributionId;
              });

    auto snapshot = std::make_shared<AssetImporterCatalogSnapshot>(state_->entries);
    state_->published = snapshot;
    state_->sealed = true;
    return Result<std::shared_ptr<const AssetImporterCatalogSnapshot>>::Success(snapshot);
}

std::shared_ptr<const AssetImporterCatalogSnapshot> AssetImporterCatalog::Snapshot() const noexcept
{
    return state_->published;
}

bool AssetImporterCatalog::IsSealed() const noexcept
{
    return state_->sealed;
}

void AssetImporterCatalog::Reset()
{
    if (state_->sealed)
        return;
    state_->entries.clear();
}

} // namespace Horo::Assets
