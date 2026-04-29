#pragma once

#include <glad/glad.h>

namespace Horo::OpenGLContext {

// Load all OpenGL entry-points via the GLFW proc-address bridge.
// Must be called once, on the thread that owns the current GL context,
// after glfwMakeContextCurrent(). Returns true on success.
bool InitGlad(GLADloadproc getProcAddress);

// Return GL_VERSION / GL_RENDERER strings (valid after InitGlad succeeds).
const char *GetGLVersion();
const char *GetGLRenderer();

} // namespace Horo::OpenGLContext
