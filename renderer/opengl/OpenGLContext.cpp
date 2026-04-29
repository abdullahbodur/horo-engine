#include "renderer/opengl/OpenGLContext.h"

#include <glad/glad.h>

namespace Horo::OpenGLContext {

bool InitGlad(void *(*getProcAddress)(const char *)) {
    return gladLoadGLLoader(
               reinterpret_cast<GLADloadproc>(getProcAddress)) != 0;
}

const char *GetGLVersion() {
    return reinterpret_cast<const char *>(glGetString(GL_VERSION));
}

const char *GetGLRenderer() {
    return reinterpret_cast<const char *>(glGetString(GL_RENDERER));
}

} // namespace Horo::OpenGLContext
