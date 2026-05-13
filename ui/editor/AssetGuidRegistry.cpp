/** @file AssetGuidRegistry.cpp
 *  @brief Implementation of the in-memory GUID → AssetMetadata index.
 *
 *  The registry caches @c AssetMetadata records keyed by GUID and maintains a
 *  reverse-dependency index built from the @c DownstreamAsset edges in each
 *  record. Refreshing from the filesystem walks
 *  @c ProjectPath::Root()/assets/models for @c asset.meta.json sidecars; the
 *  scan tolerates malformed or empty sidecars by recording warnings and
 *  skipping the offending entry.
 */
#include "ui/editor/AssetGuidRegistry.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <system_error>

#include "core/ProjectPath.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
    namespace {
        /** @brief Returns the root managed-assets directory or empty when no project is set. */
        std::filesystem::path ManagedAssetsRoot() {
            const std::filesystem::path projectRoot = ProjectPath::Root();
            if (projectRoot.empty())
                return {};
            return projectRoot / "assets" / "models";
        }

        /** @brief True when @p p is a non-empty asset.meta.json sidecar. */
        bool IsAssetMetadataFile(const std::filesystem::directory_entry &entry) {
            std::error_code ec;
            return entry.is_regular_file(ec) && !ec &&
                   entry.path().filename() == "asset.meta.json";
        }
    } // namespace

    /** @copydoc AssetGuidRegistry::RefreshFromFilesystem */
    AssetGuidRegistryRefreshResult AssetGuidRegistry::RefreshFromFilesystem() {
        AssetGuidRegistryRefreshResult result;
        m_byGuid.clear();
        m_dependents.clear();
        m_lastSource = AssetGuidRegistryRefreshSource::Filesystem;

        const std::filesystem::path root = ManagedAssetsRoot();
        if (root.empty()) {
            result.warnings.emplace_back(
                "ProjectPath::Root() is empty; no assets directory to scan.");
            return result;
        }
        if (std::error_code rootEc;
            !std::filesystem::is_directory(root, rootEc) || rootEc) {
            // No managed dir yet; not an error - empty registry is a valid state.
            return result;
        }

        for (std::error_code iterEc;
             const std::filesystem::directory_entry &entry:
             std::filesystem::recursive_directory_iterator(
                 root,
                 std::filesystem::directory_options::skip_permission_denied,
                 iterEc)) {
            if (iterEc) {
                result.warnings.emplace_back(
                    std::format("Directory iteration error: {}", iterEc.message()));
                iterEc.clear();
                continue;
            }
            if (!IsAssetMetadataFile(entry))
                continue;
            ++result.scanned;

            // The parent directory name is the GUID for managed assets.
            const std::string assetGuid = entry.path().parent_path().filename().string();
            if (assetGuid.empty()) {
                ++result.skipped;
                result.warnings.emplace_back(std::format(
                    "Sidecar at {} has no GUID directory; skipping.",
                    entry.path().generic_string()));
                continue;
            }

            AssetMetadata metadata;
            if (std::string loadError;
                !LoadAssetMetadata(assetGuid, &metadata, &loadError)) {
                ++result.skipped;
                result.warnings.emplace_back(
                    std::format("Failed to load sidecar for GUID {}: {}",
                                assetGuid, loadError));
                continue;
            }
            if (metadata.assetGuid.empty())
                metadata.assetGuid = assetGuid;
            IndexMetadata(std::move(metadata));
            ++result.loaded;
        }

        return result;
    }

    /** @copydoc AssetGuidRegistry::RefreshFromDocument */
    AssetGuidRegistryRefreshResult
    AssetGuidRegistry::RefreshFromDocument(const SceneDocument &doc) {
        AssetGuidRegistryRefreshResult result;
        m_byGuid.clear();
        m_dependents.clear();
        m_lastSource = AssetGuidRegistryRefreshSource::Document;

        for (const auto &[assetId, assetDef]: doc.assets) {
            if (assetDef.guid.empty()) {
                ++result.skipped;
                result.warnings.emplace_back(std::format(
                    "Asset '{}' has empty GUID; skipping.", assetId));
                continue;
            }
            ++result.scanned;
            AssetMetadata metadata;
            if (std::string loadError;
                !LoadAssetMetadata(assetDef.guid, &metadata, &loadError)) {
                ++result.skipped;
                result.warnings.emplace_back(
                    std::format("Failed to load sidecar for asset '{}' (GUID {}): {}",
                                assetId, assetDef.guid, loadError));
                continue;
            }
            if (metadata.assetGuid.empty())
                metadata.assetGuid = assetDef.guid;
            if (metadata.assetId.empty())
                metadata.assetId = assetId;
            IndexMetadata(std::move(metadata));
            ++result.loaded;
        }
        return result;
    }

    /** @copydoc AssetGuidRegistry::Insert */
    bool AssetGuidRegistry::Insert(AssetMetadata metadata) {
        if (metadata.assetGuid.empty())
            return false;
        // Drop any existing entry's dependents contribution before re-indexing.
        UnindexDependents(metadata.assetGuid);
        IndexMetadata(std::move(metadata));
        return true;
    }

    /** @copydoc AssetGuidRegistry::Invalidate */
    bool AssetGuidRegistry::Invalidate(std::string_view assetGuid) {
        const auto it = m_byGuid.find(assetGuid);
        if (it == m_byGuid.end())
            return false;
        // Capture the key as std::string so UnindexDependents can hash a heterogeneous lookup.
        const std::string guidKey(assetGuid);
        UnindexDependents(guidKey);
        m_byGuid.erase(it);
        return true;
    }

    /** @copydoc AssetGuidRegistry::Clear */
    void AssetGuidRegistry::Clear() {
        m_byGuid.clear();
        m_dependents.clear();
        m_lastSource = AssetGuidRegistryRefreshSource::Unknown;
    }

    /** @copydoc AssetGuidRegistry::LookupByGuid */
    const AssetMetadata *
    AssetGuidRegistry::LookupByGuid(std::string_view assetGuid) const {
        const auto it = m_byGuid.find(assetGuid);
        if (it == m_byGuid.end())
            return nullptr;
        return &it->second;
    }

    /** @copydoc AssetGuidRegistry::Dependents */
    std::vector<std::string>
    AssetGuidRegistry::Dependents(std::string_view assetGuid) const {
        std::vector<std::string> out;
        const auto it = m_dependents.find(assetGuid);
        if (it == m_dependents.end())
            return out;
        out.reserve(it->second.size());
        for (const std::string &dependentGuid: it->second)
            out.push_back(dependentGuid);
        std::ranges::sort(out);
        return out;
    }

    /** @copydoc AssetGuidRegistry::IndexMetadata */
    void AssetGuidRegistry::IndexMetadata(AssetMetadata metadata) {
        const std::string guidKey = metadata.assetGuid;
        if (guidKey.empty())
            return;

        // Update reverse-dependents index for every DownstreamAsset edge this entry declares.
        for (const AssetDependencyRecord &dep: metadata.dependencies) {
            if (dep.kind != AssetDependencyKind::DownstreamAsset || dep.value.empty())
                continue;
            m_dependents[dep.value].insert(guidKey);
        }
        m_byGuid[guidKey] = std::move(metadata);
    }

    /** @copydoc AssetGuidRegistry::UnindexDependents */
    void AssetGuidRegistry::UnindexDependents(const std::string &assetGuid) {
        const auto it = m_byGuid.find(assetGuid);
        if (it == m_byGuid.end())
            return;
        for (const AssetDependencyRecord &dep: it->second.dependencies) {
            if (dep.kind != AssetDependencyKind::DownstreamAsset || dep.value.empty())
                continue;
            const auto setIt = m_dependents.find(dep.value);
            if (setIt == m_dependents.end())
                continue;
            setIt->second.erase(assetGuid);
            if (setIt->second.empty())
                m_dependents.erase(setIt);
        }
    }

    /** @copydoc AssetGuidRegistry::RebuildDependentsIndex */
    void AssetGuidRegistry::RebuildDependentsIndex() {
        m_dependents.clear();
        for (const auto &[assetGuid, metadata]: m_byGuid) {
            for (const AssetDependencyRecord &dep: metadata.dependencies) {
                if (dep.kind != AssetDependencyKind::DownstreamAsset || dep.value.empty())
                    continue;
                m_dependents[dep.value].insert(assetGuid);
            }
        }
    }
} // namespace Horo::Editor
