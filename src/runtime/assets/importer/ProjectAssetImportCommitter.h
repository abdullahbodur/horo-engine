#pragma once

/**
 * @file ProjectAssetImportCommitter.h
 * @brief Minimal project-storage committer that writes editor payloads to disk.
 */

#include "Horo/Assets/AssetImportOperation.h"
#include "Horo/Assets/AssetRegistry.h"

#include <memory>

namespace Horo::Assets
{

/**
 * @brief Commits validated import batches to the project's Assets/ directory.
 *
 * For each item with a valid result, writes the editor payload to:
 *   <projectRoot>/Assets/<displayName><targetExtension>
 *
 * Logs every write so the user can see where assets land.
 */
class ProjectAssetImportCommitter final : public IAssetImportCommitter
{
  public:
    explicit ProjectAssetImportCommitter(AssetRegistry *registry = nullptr) noexcept : registry_(registry)
    {
    }

    [[nodiscard]] Result<void> Commit(PreparedAssetImportBatch batch,
                                      IAssetIdGenerator &idGenerator,
                                      const CancellationToken &cancellation) override;

  private:
    AssetRegistry *registry_{};
};

/** @brief Convenience factory. */
[[nodiscard]] inline std::unique_ptr<ProjectAssetImportCommitter> MakeProjectCommitter(
    AssetRegistry *registry = nullptr)
{
    return std::make_unique<ProjectAssetImportCommitter>(registry);
}

} // namespace Horo::Assets
