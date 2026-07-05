/** @file LauncherEditorShellInternal.h
 *  @brief Internal helpers shared by launcher editor shell implementation and tests. */
#pragma once

#include "math/Vec3.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Launcher {
/** @brief Resolves the runtime scale for an editor object, including asset render scale metadata.
 *  @param object Editor-side scene object using authoring scale values.
 *  @return Scale value to write into the live runtime TransformComponent. */
Vec3 ResolveRuntimeScaleFromEditorObject(const Editor::SceneObject &object);
} // namespace Horo::Launcher
