#include "editor/document/RuntimeSceneConversion.h"

namespace Horo::Editor {
    /** @copydoc ConvertSceneDocumentToRuntime */
    Result<Runtime::RuntimeSceneDefinition> ConvertSceneDocumentToRuntime(const SceneDocumentSnapshot &document,
                                                                          const Runtime::SceneDefinitionId sceneId) {
        Runtime::SceneDefinitionBuilder builder{sceneId, Runtime::SceneDefinitionRevision{document.state.value}};
        for (const SceneObjectSnapshot &object : document.objects) {
            const Runtime::RuntimeComponentSet components{object.components.camera, object.components.light,
                                                          object.components.triggerVolume, object.components.audioSource,
                                                          object.components.behaviors};
            builder.Add(Runtime::RuntimeEntityDefinition{
                .object = Runtime::SceneObjectId{object.id.value},
                .parent = object.parent ? std::optional{Runtime::SceneObjectId{object.parent->value}} : std::nullopt,
                .localTransform = object.localTransform,
                .primitiveMesh = object.primitiveMesh,
                .components = components,
            });
        }
        return std::move(builder).Build();
    }
}  // namespace Horo::Editor
