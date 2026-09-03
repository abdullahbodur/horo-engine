#pragma once

#include <array>
#include <glad/gl.h>

namespace Horo::Editor {
    /** @brief Restores host-owned OpenGL bindings and fixed-function state on scope exit. */
    class OpenGLStateSnapshot final {
    public:
        /** @brief Captures the OpenGL state mutated by an editor viewport pass. */
        OpenGLStateSnapshot() noexcept;

        /** @brief Restores the captured OpenGL state. */
        ~OpenGLStateSnapshot();

        OpenGLStateSnapshot(const OpenGLStateSnapshot &) = delete;
        OpenGLStateSnapshot &operator=(const OpenGLStateSnapshot &) = delete;

    private:
        GLint drawFramebuffer_{0};
        GLint readFramebuffer_{0};
        GLint program_{0};
        GLint vertexArray_{0};
        GLint arrayBuffer_{0};
        GLint depthFunction_{0};
        GLint cullFaceMode_{0};
        GLint activeTexture_{0};
        GLint shadowTexture_{0};
        std::array<GLint, 4> viewport_{};
        std::array<GLfloat, 4> clearColor_{};
        std::array<GLboolean, 4> colorMask_{};
        GLboolean depthMask_{GL_TRUE};
        GLboolean depthTestEnabled_{GL_FALSE};
        GLboolean scissorTestEnabled_{GL_FALSE};
        GLboolean blendEnabled_{GL_FALSE};
        GLboolean cullFaceEnabled_{GL_FALSE};
    };
}  // namespace Horo::Editor
