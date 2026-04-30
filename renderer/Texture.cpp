// GL implementation moved to renderer/opengl/OpenGLTexture.cpp.
// Texture is now a type alias for OpenGLTexture (see renderer/Texture.h).
// NOTE(renderer-abstraction): Goal 5 will replace remaining Texture call sites
//   with ITexture-based references.
#include "renderer/Texture.h"
