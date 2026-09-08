#include "editor/document/RuntimeSceneConversion.h"

namespace Horo::Editor {
    namespace {
        template <typename Component> [[nodiscard]] std::optional<Component> ActiveComponent(const std::optional<Component> &component) {
            return component.has_value() && component->enabled ? component : std::nullopt;
        }
    }  // namespace

    /** @copydoc ConvertSceneDocumentToRuntime */
    Result<Runtime::RuntimeSceneDefinition> ConvertSceneDocumentToRuntime(const SceneDocumentSnapshot &document,
                                                                          const Runtime::SceneDefinitionId sceneId) {
        Runtime::SceneDefinitionBuilder builder{sceneId, Runtime::SceneDefinitionRevision{document.state.value}};
        for (const SceneObjectSnapshot &object : document.objects) {
            const Runtime::RuntimeComponentSet components{
                .camera = ActiveComponent(object.components.camera),
                .light = ActiveComponent(object.components.light),
                .triggerVolume = ActiveComponent(object.components.triggerVolume),
                .audioSource = ActiveComponent(object.components.audioSource),
                .behaviors = object.components.behaviors,
            };
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
