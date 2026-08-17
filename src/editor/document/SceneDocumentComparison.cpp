#include "editor/document/SceneDocumentComparison.h"

#include "editor/document/SceneDocumentPersistence.h"

#include <unordered_map>

namespace Horo::Editor {
    namespace {
        const ErrorDomainId SceneComparisonDomain{"horo.editor.scene_comparison"};
        const ErrorCodeDescriptor SceneComparisonUnavailable{
            .domain = SceneComparisonDomain,
            .code = ErrorCode{"scene_comparison.unavailable"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "The canonical scene is unavailable for comparison.",
        };

        [[nodiscard]] SceneObjectDifferenceFields CompareObjectFields(const SceneObjectSnapshot &document,
                                                                      const SceneObjectSnapshot &disk) {
            return {
                .name = document.name != disk.name,
                .parent = document.parent != disk.parent,
                .transform = document.localTransform != disk.localTransform,
                .primitive = document.primitiveMesh != disk.primitiveMesh,
                .asset = document.meshAsset != disk.meshAsset,
                .components = document.components != disk.components,
                .editorState = document.editorState != disk.editorState,
            };
        }
    }  // namespace

    /** @copydoc CompareSceneDocuments */
    SceneDocumentComparison CompareSceneDocuments(const SceneDocumentSnapshot &document, const SceneDocumentSnapshot &disk) {
        std::unordered_map<std::uint64_t, const SceneObjectSnapshot *> diskObjects;
        diskObjects.reserve(disk.objects.size());
        for (const SceneObjectSnapshot &object : disk.objects)
            diskObjects.emplace(object.id.value, &object);

        SceneDocumentComparison comparison;
        comparison.objects.reserve(document.objects.size() + disk.objects.size());
        for (const SceneObjectSnapshot &documentObject : document.objects) {
            const auto diskObject = diskObjects.find(documentObject.id.value);
            if (diskObject == diskObjects.end()) {
                comparison.objects.push_back({
                    .id = documentObject.id,
                    .kind = SceneObjectComparisonKind::RemovedFromDisk,
                    .documentName = documentObject.name,
                });
                ++comparison.removedFromDisk;
                continue;
            }

            const SceneObjectDifferenceFields fields = CompareObjectFields(documentObject, *diskObject->second);
            if (fields.Any()) {
                comparison.objects.push_back({
                    .id = documentObject.id,
                    .kind = SceneObjectComparisonKind::Modified,
                    .documentName = documentObject.name,
                    .diskName = diskObject->second->name,
                    .fields = fields,
                });
                ++comparison.modified;
            }
            diskObjects.erase(diskObject);
        }

        for (const SceneObjectSnapshot &diskObject : disk.objects) {
            if (!diskObjects.contains(diskObject.id.value))
                continue;
            comparison.objects.push_back({
                .id = diskObject.id,
                .kind = SceneObjectComparisonKind::AddedOnDisk,
                .diskName = diskObject.name,
            });
            ++comparison.addedOnDisk;
        }
        return comparison;
    }

    /** @copydoc LoadSceneDocumentComparison */
    Result<SceneDocumentComparison> LoadSceneDocumentComparison(const SceneDocumentComparisonRequest &request) {
        if (!request.absoluteProjectRoot.is_absolute() || !request.absoluteScenePath.is_absolute()) {
            return Result<SceneDocumentComparison>::Failure(MakeError(SceneComparisonUnavailable));
        }

        Result<std::optional<LoadedProjectScene>> loaded = LoadProjectDefaultScene(request.absoluteProjectRoot);
        if (loaded.HasError())
            return Result<SceneDocumentComparison>::Failure(loaded.ErrorValue());
        if (!loaded.Value().has_value() ||
            loaded.Value()->absolutePath.lexically_normal() != request.absoluteScenePath.lexically_normal()) {
            return Result<SceneDocumentComparison>::Failure(MakeError(SceneComparisonUnavailable));
        }

        SceneDocumentSnapshot disk{
            .objects = std::move(loaded).Value()->objects,
        };
        SceneDocumentComparison comparison = CompareSceneDocuments(request.document, disk);
        comparison.absoluteScenePath = request.absoluteScenePath.string();
        return Result<SceneDocumentComparison>::Success(std::move(comparison));
    }
}  // namespace Horo::Editor
