#pragma once

namespace Horo::Editor {
    /** @brief Active transform interaction selected by editor presentation surfaces. */
    enum class EditorTransformTool {
        Select,
        Move,
        Rotate,
        Scale,
    };

    /** @brief Orientation basis used by editor transform interactions. */
    enum class EditorTransformSpace {
        Local,
        World,
    };
}  // namespace Horo::Editor
