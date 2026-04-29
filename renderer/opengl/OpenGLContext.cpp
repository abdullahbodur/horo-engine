#include "renderer/opengl/OpenGLContext.h"

#include <glad/glad.h>

namespace Horo::OpenGLContext {

bool InitGlad(GLADloadproc getProcAddress) {
    return gladLoadGLLoader(getProcAddress) != 0;
}

const char *GetGLVersion() {
    return reinterpret_cast<const char *>(glGetString(GL_VERSION)); // NOSONAR: OpenGL API returns const GLubyte*
}

const char *GetGLRenderer() {
    return reinterpret_cast<const char *>(glGetString(GL_RENDERER)); // NOSONAR: OpenGL API returns const GLubyte*
}

} // namespace Horo::OpenGLContext
