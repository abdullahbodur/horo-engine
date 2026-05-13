/** @file AssetGuidRegistry.h
 *  @brief In-memory index of imported-asset GUIDs to cached metadata and reverse dependents.
 *
 *  The registry is the lookup-side complement of the on-disk @c asset.meta.json sidecars
 *  written by @ref AssetMetadata. It scans the project's managed asset directories
 *  (@c assets/models/<guid>/) once and provides O(1) GUID lookups plus a precomputed
 *  reverse index of @c DownstreamAsset dependencies. Callers that previously rebuilt
 *  per-call lookup maps (e.g. @ref AssetImportService::ReimportAssetWithDependents)
 *  should query the registry instead.
 */
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/StringHash.h"
#include "ui/editor/AssetMetadata.h"

namespace Horo::Editor {
    struct SceneDocument;

    /** @brief Snapshot reason describing what populated the registry, for diagnostics. */
    enum class AssetGuidRegistryRefreshSource {
        Unknown,    /**< No refresh has run yet. */
        Filesystem, /**< Populated by walking the managed asset directory tree. */
        Document,   /**< Populated from a SceneDocument's assets and their sidecars. */
    };

    /** @brief Outcome of a refresh pass over the on-disk sidecars. */
    struct AssetGuidRegistryRefreshResult {
        size_t scanned = 0;        /**< Number of sidecar files visited. */
        size_t loaded = 0;         /**< Number of sidecars parsed successfully. */
        size_t skipped = 0;        /**< Sidecars skipped due to parse errors or empty GUID. */
        std::vector<std::string>
        warnings;                 /**< Non-fatal issues encountered during the scan. */
    };

    /**
     * @brief In-memory GUID → metadata index with reverse-dependency lookup.
     *
     * Ownership: callers own the registry instance; it holds no global state.
     * Lifetime: the registry must outlive any @c AssetMetadata pointer returned
     *           by @ref LookupByGuid; the pointer is invalidated by any mutating
     *           call (Refresh*, Insert, Invalidate, Clear).
     * Thread-safety: not thread-safe; access from a single thread.
     */
    class AssetGuidRegistry {
    public:
        /** @brief Constructs an empty registry. */
        AssetGuidRegistry() = default;

        /** @brief Walks the project's managed-asset tree and reloads every sidecar.
         *
         *  Scans @c ProjectPath::Root()/assets/models for @c asset.meta.json files
         *  and replaces all cached entries. Existing entries not present on disk
         *  are dropped.
         *  @return Aggregate counters describing what was loaded or skipped.
         */
        AssetGuidRegistryRefreshResult RefreshFromFilesystem();

        /** @brief Reloads entries for every asset listed in the document.
         *
         *  Iterates @p doc.assets and calls @ref LoadAssetMetadata for each GUID,
         *  overwriting any cached entry. Assets without a GUID are skipped.
         *  Existing entries for GUIDs not present in the document are dropped.
         *  @param doc Scene document providing the authoritative asset set.
         *  @return Aggregate counters describing what was loaded or skipped.
         */
        AssetGuidRegistryRefreshResult RefreshFromDocument(const SceneDocument &doc);

        /** @brief Inserts or replaces a single entry without touching the filesystem.
         *
         *  Used by import flows that already have an in-memory @c AssetMetadata and
         *  want the registry to reflect it without an extra disk read.
         *  @param metadata Metadata to cache. Must have a non-empty @c assetGuid.
         *  @return True when the entry was inserted; false when @p metadata.assetGuid is empty.
         */
        bool Insert(AssetMetadata metadata);

        /** @brief Drops a single GUID's entry from the cache.
         *
         *  Used after deleting an asset on disk or before triggering a reload.
         *  @param assetGuid GUID to evict.
         *  @return True when an entry was removed; false when no entry existed.
         */
        bool Invalidate(std::string_view assetGuid);

        /** @brief Removes every cached entry. */
        void Clear();

        /** @brief Returns the cached metadata for @p assetGuid or @c nullptr if absent.
         *
         *  The pointer is invalidated by any mutating call on the registry.
         *  @param assetGuid GUID to look up.
         *  @return Borrowed pointer to cached metadata, or null when not present.
         */
        const AssetMetadata *LookupByGuid(std::string_view assetGuid) const;

        /** @brief Returns the GUIDs that depend on @p assetGuid via @c DownstreamAsset edges.
         *
         *  Reverse-index built from the @c dependencies vector of every cached
         *  metadata entry. Edges that point at unknown GUIDs are still returned
         *  so callers can detect dangling references.
         *  @param assetGuid GUID whose dependents are requested.
         *  @return Sorted, deduplicated list of dependent GUIDs (empty when none).
         */
        std::vector<std::string> Dependents(std::string_view assetGuid) const;

        /** @brief Returns the number of cached entries. */
        size_t Size() const { return m_byGuid.size(); }

        /** @brief Returns true when no entries are cached. */
        bool Empty() const { return m_byGuid.empty(); }

        /** @brief Returns an indication of how the registry was last populated. */
        AssetGuidRegistryRefreshSource LastRefreshSource() const { return m_lastSource; }

    private:
        /** @brief Map of GUID to its cached metadata entry. */
        std::unordered_map<std::string, AssetMetadata, StringHash, std::equal_to<> >
        m_byGuid;

        /** @brief Reverse index: target GUID → set of GUIDs that depend on it. */
        std::unordered_map<std::string,
                           std::unordered_set<std::string, StringHash, std::equal_to<> >,
                           StringHash, std::equal_to<> > m_dependents;

        /** @brief Last refresh source for diagnostic reporting. */
        AssetGuidRegistryRefreshSource m_lastSource =
                AssetGuidRegistryRefreshSource::Unknown;

        /** @brief Adds @p metadata to both the GUID map and the reverse-dependents index. */
        void IndexMetadata(AssetMetadata metadata);

        /** @brief Removes @p assetGuid's contribution to the reverse-dependents index. */
        void UnindexDependents(const std::string &assetGuid);

        /** @brief Rebuilds @ref m_dependents from @ref m_byGuid. */
        void RebuildDependentsIndex();
    };
} // namespace Horo::Editor
