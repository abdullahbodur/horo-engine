#pragma once

// OpenGL texture implementation.  Texture inherits from OpenGLTexture so that
// existing forward declarations (`class Texture;`) and call sites remain valid.
// NOTE(renderer-abstraction): Goal 5 will collapse call sites to ITexture&.
#include "renderer/opengl/OpenGLTexture.h"

namespace Horo {

class Texture : public OpenGLTexture {
public:
    Texture() = default;

    // Converting move-constructor from the base factory result.
    explicit Texture(OpenGLTexture&& base) : OpenGLTexture(std::move(base)) {}

    static Texture FromFile(const std::string& path, bool flipY = true) {
        return Texture(OpenGLTexture::FromFile(path, flipY));
    }
    static Texture CreateWhite1x1() {
        return Texture(OpenGLTexture::CreateWhite1x1());
    }
};

} // namespace Horo
