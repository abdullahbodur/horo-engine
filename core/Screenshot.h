#pragma once
#include <string>

namespace Horo {
    // Captures the current OpenGL framebuffer and writes it to a BMP file.
    // Call from the render thread (after SwapBuffers or just before it).
    class Screenshot {
    public:
        // Save a BMP to `folder` (e.g. "C:/Users/foo/Downloads").
        // Filename is auto-generated: screenshot_YYYYMMDD_HHMMSS.bmp
        // Returns the full path written, or empty string on failure.
        static std::string Save(int viewportW, int viewportH,
                                const std::string &folder);
    };
} // namespace Horo
