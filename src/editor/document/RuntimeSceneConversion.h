#pragma once

/**
 * @file RuntimeSceneConversion.h
 * @brief Editor-owned conversion from authoritative documents to immutable runtime definitions.
 */

#include "Horo/Runtime/Scene/RuntimeSceneDefinition.h"
#include "editor/document/SceneDocument.h"

namespace Horo::Editor
{
/**
 * @brief Converts one committed authoring snapshot to a validated backend-neutral runtime definition.
 * @param document Immutable committed document snapshot.
 * @param sceneId Stable logical identity of this editor preview scene.
 * @return Immutable definition or the first typed validation diagnostic.
 */
[[nodiscard]] Result<Runtime::RuntimeSceneDefinition> ConvertSceneDocumentToRuntime(
    const SceneDocumentSnapshot &document, Runtime::SceneDefinitionId sceneId);
} // namespace Horo::Editor
