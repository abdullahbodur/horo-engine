#include "editor/AssetImportService.h"

#include <algorithm>
#include <functional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "core/Logger.h"
#include "core/StringHash.h"
#include "editor/AssetIdentity.h"

namespace Horo::Editor {
    namespace {
        void RemoveDependenciesOfKind(AssetMetadata *metadata,
                                      AssetDependencyKind kind) {
            if (!metadata)
                return;
            const auto toErase = std::ranges::remove_if(
                metadata->dependencies,
                [kind](const AssetDependencyRecord &dep) { return dep.kind == kind; });
            metadata->dependencies.erase(toErase.begin(), toErase.end());
        }

        void AppendUniqueDependency(AssetMetadata *metadata, AssetDependencyKind kind,
                                    const std::string &value) {
            if (!metadata || value.empty())
                return;
            const auto it = std::ranges::find_if(
                metadata->dependencies, [&](const AssetDependencyRecord &dep) {
                    return dep.kind == kind && dep.value == value;
                });
            if (it == metadata->dependencies.end())
                metadata->dependencies.emplace_back(kind, value);
        }

        bool LoadOrBuildMetadata(const std::string &assetId, const AssetDef &asset,
                                 AssetMetadata *outMetadata) {
            if (!outMetadata)
                return false;
            if (std::string error; LoadAssetMetadata(asset.guid, outMetadata, &error))
                return true;
            *outMetadata = BuildAssetMetadata(assetId, asset);
            outMetadata->assetGuid = asset.guid;
            outMetadata->displayName = asset.displayName;
            return true;
        }

        AssetImportDiagnostic MakeDiagnostic(AssetDiagnosticSeverity severity,
                                             std::string code, std::string message,
                                             std::string_view assetGuid,
                                             std::string_view sourcePath,
                                             std::string_view importerId) {
            AssetImportDiagnostic diagnostic;
            diagnostic.severity = severity;
            diagnostic.code = std::move(code);
            diagnostic.message = std::move(message);
            diagnostic.assetGuid = assetGuid;
            diagnostic.sourcePath = sourcePath;
            diagnostic.importerId = importerId;
            return diagnostic;
        }

        void LogDiagnostics(const std::vector<AssetImportDiagnostic> &diagnostics) {
            for (const AssetImportDiagnostic &diagnostic: diagnostics) {
                const char *importer = diagnostic.importerId.empty()
                                           ? "<unknown>"
                                           : diagnostic.importerId.c_str();
                const char *source = diagnostic.sourcePath.empty()
                                         ? "<none>"
                                         : diagnostic.sourcePath.c_str();
                if (diagnostic.severity == AssetDiagnosticSeverity::Info) {
                    LogInfo("[AssetImport][{}][{}] {} ({})", importer, diagnostic.code,
                            diagnostic.message, source);
                } else if (diagnostic.severity == AssetDiagnosticSeverity::Warning) {
                    LogWarn("[AssetImport][{}][{}] {} ({})", importer, diagnostic.code,
                            diagnostic.message, source);
                } else {
                    LogError("[AssetImport][{}][{}] {} ({})", importer, diagnostic.code,
                             diagnostic.message, source);
                }
            }
        }

        void SaveFailureMetadata(AssetMetadata *metadata, std::string_view reason,
                                 std::vector<AssetImportDiagnostic> diagnostics) {
            if (!metadata)
                return;
            metadata->lastImportSucceeded = false;
            metadata->lastImportReason = reason;
            metadata->diagnostics = std::move(diagnostics);
            SaveAssetMetadata(*metadata, nullptr);
        }
    } // namespace

    namespace {
        struct ReimportDepGraph {
            std::unordered_map<std::string, std::set<std::string, std::less<> >,
                StringHash, std::equal_to<> >
            reverseGraph;
            std::set<std::string, std::less<> > impacted;
            std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
            dependencyReason;
        };

        void BuildReimportLookupMaps(
            const SceneDocument *doc,
            std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
            &assetIdByGuid,
            std::unordered_map<std::string, AssetMetadata, StringHash, std::equal_to<> >
            &metadataByGuid) {
            for (const auto &[assetKey, assetDef]: doc->assets) {
                assetIdByGuid[assetDef.guid] = assetKey;
                AssetMetadata metadata;
                if (LoadOrBuildMetadata(assetKey, assetDef, &metadata))
                    metadataByGuid[assetDef.guid] = std::move(metadata);
            }
        }

        ReimportDepGraph BuildReimportDepGraph(
            const std::string &rootAssetGuid,
            const std::unordered_map<std::string, AssetMetadata, StringHash,
                std::equal_to<> > &metadataByGuid) {
            ReimportDepGraph graph;
            for (const auto &[assetGuid, depMeta]: metadataByGuid) {
                for (const AssetDependencyRecord &dep: depMeta.dependencies) {
                    if (dep.kind != AssetDependencyKind::DownstreamAsset || dep.value.empty())
                        continue;
                    graph.reverseGraph[dep.value].insert(assetGuid);
                }
            }
            std::vector<std::string> queue{rootAssetGuid};
            graph.impacted.insert(rootAssetGuid);
            while (!queue.empty()) {
                const std::string current = queue.back();
                queue.pop_back();
                auto dependentsIt = graph.reverseGraph.find(current);
                if (dependentsIt == graph.reverseGraph.end())
                    continue;
                for (const std::string &dependentGuid: dependentsIt->second) {
                    if (graph.impacted.insert(dependentGuid).second) {
                        queue.push_back(dependentGuid);
                        graph.dependencyReason[dependentGuid] = current;
                    }
                }
            }
            return graph;
        }

        std::vector<std::string> ComputeImportOrder(
            const std::set<std::string, std::less<> > &impacted,
            const std::unordered_map<std::string, std::set<std::string, std::less<> >,
                StringHash, std::equal_to<> > &reverseGraph) {
            std::unordered_map<std::string, std::set<std::string, std::less<> >,
                        StringHash, std::equal_to<> >
                    adjacency;
            std::unordered_map<std::string, size_t, StringHash, std::equal_to<> > indegree;
            for (const std::string &guid: impacted)
                indegree[guid] = 0;
            for (const auto &[guid, dependents]: reverseGraph) {
                if (!impacted.contains(guid))
                    continue;
                for (const std::string &dependentGuid: dependents) {
                    if (!impacted.contains(dependentGuid))
                        continue;
                    adjacency[guid].insert(dependentGuid);
                    ++indegree[dependentGuid];
                }
            }
            std::set<std::string, std::less<> > ready;
            for (const auto &[guid, inDeg]: indegree) {
                if (inDeg == 0)
                    ready.insert(guid);
            }
            std::vector<std::string> order;
            while (!ready.empty()) {
                const std::string current = *ready.begin();
                ready.erase(ready.begin());
                order.push_back(current);
                for (const std::string &dependentGuid: adjacency[current]) {
                    size_t &count = indegree[dependentGuid];
                    if (count > 0)
                        --count;
                    if (count == 0)
                        ready.insert(dependentGuid);
                }
            }
            return order;
        }
    } // namespace

    AssetImportResult
    AssetImportService::RunImporter(const AssetImporter &importer,
                                    const AssetImportRequest &request) const {
        AssetImportResult result = importer.Import(request);
        if (!result.ok) {
            if (result.diagnostics.empty()) {
                result.diagnostics.push_back(MakeDiagnostic(
                    AssetDiagnosticSeverity::Error, "asset.import.failed",
                    result.error.empty() ? "Import failed." : result.error,
                    request.assetGuid, request.sourcePath, importer.ImporterId()));
            }
            result.metadata.diagnostics = result.diagnostics;
            LogDiagnostics(result.diagnostics);
            return result;
        }

        EnsureAssetIdentity(request.assetId, &result.asset);
        result.metadata.assetId = request.assetId;
        result.metadata.assetGuid = request.assetGuid;
        result.metadata.displayName = result.asset.displayName;
        result.metadata.lastImportSucceeded = true;
        result.metadata.diagnostics = result.diagnostics;
        return result;
    }

    AssetImportResult AssetImportService::ImportAssetFromSource(
        const std::string &sourcePath, const std::string &assetId,
        const std::string &assetGuid, const std::string &displayName,
        const std::unordered_map<std::string, std::string, StringHash,
            std::equal_to<> > &settings) const {
        AssetImportResult result;
        const AssetImporter *importer = m_registry.FindByExtension(sourcePath);
        if (!importer) {
            result.error = "No importer registered for this file type.";
            result.diagnostics.push_back(MakeDiagnostic(
                AssetDiagnosticSeverity::Error, "asset.importer.not_found",
                result.error, assetGuid, sourcePath, {}));
            LogDiagnostics(result.diagnostics);
            return result;
        }

        const AssetImportRequest request{
            assetId, assetGuid, displayName, sourcePath,
            settings
        };
        result = RunImporter(*importer, request);
        if (!result.ok)
            return result;

        if (!SaveAssetMetadata(result.metadata, &result.error)) {
            result.ok = false;
            result.diagnostics.push_back(MakeDiagnostic(
                AssetDiagnosticSeverity::Error, "asset.metadata.save_failed",
                result.error, assetGuid, sourcePath, importer->ImporterId()));
            LogDiagnostics(result.diagnostics);
        }
        return result;
    }

    bool AssetImportService::ImportTextureForAsset(const std::string &sourcePath,
                                                   const std::string &assetId,
                                                   AssetDef *asset,
                                                   std::string *outError) const {
        if (outError)
            outError->clear();
        if (!asset) {
            if (outError)
                *outError = "Asset is required.";
            return false;
        }

        EnsureAssetIdentity(assetId, asset);
        const AssetImporter *importer = m_registry.FindById("builtin.texture_copy");
        if (!importer) {
            if (outError)
                *outError = "Texture importer is not registered.";
            return false;
        }

        AssetMetadata existingMetadata;
        LoadOrBuildMetadata(assetId, *asset, &existingMetadata);
        existingMetadata.settings["albedoSourcePath"] = sourcePath;

        const AssetImportRequest request{
            assetId, asset->guid, asset->displayName,
            sourcePath, existingMetadata.settings
        };
        AssetImportResult result = RunImporter(*importer, request);
        if (!result.ok) {
            if (outError)
                *outError = result.error;
            SaveFailureMetadata(&existingMetadata, "Texture override import failed.",
                                result.diagnostics);
            return false;
        }

        asset->albedoMap = result.asset.albedoMap;
        existingMetadata.assetId = assetId;
        existingMetadata.assetGuid = asset->guid;
        existingMetadata.displayName = asset->displayName;
        existingMetadata.settings = result.metadata.settings;
        existingMetadata.producedFiles = result.metadata.producedFiles;
        existingMetadata.lastImportSucceeded = true;
        existingMetadata.lastImportReason = "Texture override imported.";
        existingMetadata.diagnostics = result.diagnostics;
        RemoveDependenciesOfKind(&existingMetadata,
                                 AssetDependencyKind::ProducedOutput);
        AppendUniqueDependency(&existingMetadata, AssetDependencyKind::Source,
                               sourcePath);
        for (const std::string &produced: result.metadata.producedFiles)
            AppendUniqueDependency(&existingMetadata,
                                   AssetDependencyKind::ProducedOutput, produced);

        if (!SaveAssetMetadata(existingMetadata, outError))
            return false;
        return true;
    }

    bool AssetImportService::SaveMetadataForAsset(const std::string &assetId,
                                                  const AssetDef &asset,
                                                  std::string *outError) const {
        AssetMetadata metadata;
        LoadOrBuildMetadata(assetId, asset, &metadata);

        metadata.assetId = assetId;
        metadata.assetGuid = asset.guid;
        metadata.displayName = asset.displayName;
        metadata.producedFiles = BuildAssetMetadata(assetId, asset).producedFiles;
        RemoveDependenciesOfKind(&metadata, AssetDependencyKind::ProducedOutput);
        for (const std::string &produced: metadata.producedFiles)
            AppendUniqueDependency(&metadata, AssetDependencyKind::ProducedOutput,
                                   produced);
        return SaveAssetMetadata(metadata, outError);
    }

    AssetReimportResult AssetImportService::ReimportAssetWithDependents(
        SceneDocument *doc, const std::string &rootAssetGuid,
        const std::string &reason) const {
        AssetReimportResult result;
        if (!doc) {
            result.error = "Scene document is required.";
            result.records.emplace_back(std::string{}, std::string{}, reason, false,
                                        result.error);
            return result;
        }

        std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
                assetIdByGuid;
        std::unordered_map<std::string, AssetMetadata, StringHash, std::equal_to<> >
                metadataByGuid;
        BuildReimportLookupMaps(doc, assetIdByGuid, metadataByGuid);

        if (!assetIdByGuid.contains(rootAssetGuid)) {
            result.error = "Asset metadata not found for reimport.";
            result.records.emplace_back(std::string{}, rootAssetGuid, reason, false,
                                        result.error);
            return result;
        }

        ReimportDepGraph depGraph =
                BuildReimportDepGraph(rootAssetGuid, metadataByGuid);
        result.order = ComputeImportOrder(depGraph.impacted, depGraph.reverseGraph);

        if (result.order.size() != depGraph.impacted.size()) {
            result.error =
                    "Asset dependency cycle detected during reimport propagation.";
            if (AssetMetadata rootMetadata;
                LoadAssetMetadata(rootAssetGuid, &rootMetadata, nullptr)) {
                SaveFailureMetadata(
                    &rootMetadata, reason,
                    {
                        MakeDiagnostic(AssetDiagnosticSeverity::Error,
                                       "asset.reimport.dependency_cycle", result.error,
                                       rootAssetGuid, rootMetadata.sourcePath,
                                       rootMetadata.importerId)
                    });
            }
            LogDiagnostics({
                MakeDiagnostic(AssetDiagnosticSeverity::Error,
                               "asset.reimport.dependency_cycle",
                               result.error, rootAssetGuid, {}, {})
            });
            result.records.emplace_back(assetIdByGuid[rootAssetGuid], rootAssetGuid,
                                        reason, false, result.error);
            return result;
        }

        for (const std::string &guid: result.order) {
            const std::string recordReason =
                    (guid == rootAssetGuid)
                        ? reason
                        : "Dependency changed: " + depGraph.dependencyReason[guid];
            if (!ReimportSingleAsset(guid, recordReason, doc, result))
                return result;
        }

        result.ok = true;
        return result;
    }

    bool AssetImportService::ReimportSingleAsset(
        const std::string &guid, const std::string &recordReason,
        SceneDocument *doc, AssetReimportResult &result) const {
        auto assetIt = std::ranges::find_if(doc->assets, [&](const auto &entry) {
            return entry.second.guid == guid;
        });
        if (assetIt == doc->assets.end()) {
            result.records.emplace_back(std::string{}, guid, std::string{}, false,
                                        "Asset not found in document.");
            result.error = "Asset not found in document during reimport.";
            return false;
        }

        AssetMetadata metadata;
        LoadOrBuildMetadata(assetIt->first, assetIt->second, &metadata);

        if (metadata.importerId.empty() || metadata.sourcePath.empty()) {
            const std::string error = "Asset has no importer metadata or source path.";
            SaveFailureMetadata(
                &metadata, recordReason,
                {
                    MakeDiagnostic(AssetDiagnosticSeverity::Error,
                                   "asset.reimport.metadata_missing", error, guid,
                                   metadata.sourcePath, metadata.importerId)
                });
            LogDiagnostics(metadata.diagnostics);
            result.records.emplace_back(assetIt->first, guid, recordReason, false,
                                        error);
            result.error = error;
            return false;
        }

        const AssetImporter *importer = m_registry.FindById(metadata.importerId);
        if (!importer)
            importer = m_registry.FindByExtension(metadata.sourcePath);
        if (!importer) {
            const std::string error = "Registered importer not found.";
            SaveFailureMetadata(
                &metadata, recordReason,
                {
                    MakeDiagnostic(AssetDiagnosticSeverity::Error,
                                   "asset.reimport.importer_missing", error, guid,
                                   metadata.sourcePath, metadata.importerId)
                });
            LogDiagnostics(metadata.diagnostics);
            result.records.emplace_back(assetIt->first, guid, recordReason, false,
                                        error);
            result.error = error;
            return false;
        }

        AssetImportRequest request{
            assetIt->first, assetIt->second.guid,
            assetIt->second.displayName, metadata.sourcePath,
            metadata.settings
        };
        AssetImportResult importResult = RunImporter(*importer, request);
        if (!importResult.ok) {
            SaveFailureMetadata(&metadata, recordReason, importResult.diagnostics);
            result.records.emplace_back(assetIt->first, guid, recordReason, false,
                                        importResult.error);
            result.error = importResult.error;
            return false;
        }

        if (const auto albedoIt = metadata.settings.find("albedoSourcePath");
            albedoIt != metadata.settings.end() && !albedoIt->second.empty()) {
            const std::string &albedoPath = albedoIt->second;
            const AssetImporter *textureImporter =
                    m_registry.FindById("builtin.texture_copy");
            if (textureImporter) {
                AssetImportRequest textureRequest{
                    assetIt->first, assetIt->second.guid,
                    assetIt->second.displayName, albedoPath,
                    metadata.settings
                };
                AssetImportResult textureResult =
                        RunImporter(*textureImporter, textureRequest);
                if (textureResult.ok) {
                    importResult.asset.albedoMap = textureResult.asset.albedoMap;
                    importResult.metadata.producedFiles.insert(
                        importResult.metadata.producedFiles.end(),
                        textureResult.metadata.producedFiles.begin(),
                        textureResult.metadata.producedFiles.end());
                    importResult.diagnostics.insert(importResult.diagnostics.end(),
                                                    textureResult.diagnostics.begin(),
                                                    textureResult.diagnostics.end());
                }
            }
        }

        AssetMetadata updatedMetadata = metadata;
        updatedMetadata.assetId = assetIt->first;
        updatedMetadata.assetGuid = assetIt->second.guid;
        updatedMetadata.displayName = importResult.asset.displayName;
        updatedMetadata.importerId = metadata.importerId;
        updatedMetadata.sourcePath = metadata.sourcePath;
        updatedMetadata.settings = metadata.settings;
        updatedMetadata.producedFiles = importResult.metadata.producedFiles;
        updatedMetadata.lastImportSucceeded = true;
        updatedMetadata.lastImportReason = recordReason;
        updatedMetadata.diagnostics = importResult.diagnostics;
        RemoveDependenciesOfKind(&updatedMetadata,
                                 AssetDependencyKind::ProducedOutput);
        RemoveDependenciesOfKind(&updatedMetadata, AssetDependencyKind::Source);
        AppendUniqueDependency(&updatedMetadata, AssetDependencyKind::Source,
                               metadata.sourcePath);
        for (const std::string &produced: updatedMetadata.producedFiles)
            AppendUniqueDependency(&updatedMetadata,
                                   AssetDependencyKind::ProducedOutput, produced);

        assetIt->second.mesh = importResult.asset.mesh;
        assetIt->second.renderScale = importResult.asset.renderScale;
        assetIt->second.albedoMap = importResult.asset.albedoMap;
        assetIt->second.displayName = importResult.asset.displayName;

        if (std::string saveError; !SaveAssetMetadata(updatedMetadata, &saveError)) {
            result.records.emplace_back(assetIt->first, guid, recordReason, false,
                                        saveError);
            result.error = saveError;
            return false;
        }

        result.records.emplace_back(assetIt->first, guid, recordReason, true,
                                    std::string{});
        return true;
    }
} // namespace Horo::Editor
