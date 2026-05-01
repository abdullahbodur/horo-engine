// GL implementation moved to renderer/opengl/OpenGLShader.cpp.
// Shader is now a type alias for OpenGLShader (see renderer/Shader.h).
// TODO(renderer-abstraction): Goal 5 will replace remaining Shader call sites
//   with IShader-based references.
#include "renderer/Shader.h"
