#include "editor/document/RuntimeSceneConversion.h"

namespace Horo::Editor
{
    /** @copydoc ConvertSceneDocumentToRuntime */
    Result<Runtime::RuntimeSceneDefinition> ConvertSceneDocumentToRuntime(const SceneDocumentSnapshot& document,
                                                                          const Runtime::SceneDefinitionId sceneId)
    {
        Runtime::SceneDefinitionBuilder builder{sceneId, Runtime::SceneDefinitionRevision{document.state.value}};
        for (const SceneObjectSnapshot& object : document.objects)
        {
            Runtime::RuntimeComponentSet components{
                object.components.camera, object.components.light,
                object.components.triggerVolume, object.components.audioSource
            };
            builder.Add(Runtime::RuntimeEntityDefinition{
                Runtime::SceneObjectId{object.id.value},
                object.parent
                    ? std::optional{Runtime::SceneObjectId{object.parent->value}}
                    : std::nullopt,
                object.localTransform, object.primitiveMesh, std::move(components)
            });
        }
        return std::move(builder).Build();
    }
} // namespace Horo::Editor
