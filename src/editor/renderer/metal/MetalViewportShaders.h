#pragma once

namespace Horo::Editor {
    /** @brief Returns the immutable Metal shader source used by the editor viewport pipelines. */
    [[nodiscard]] const char *MetalViewportShaderSource() noexcept;
}  // namespace Horo::Editor
