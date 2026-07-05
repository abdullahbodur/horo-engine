#pragma once

// OpenGL shader implementation.  Shader inherits from OpenGLShader so that
// existing forward declarations (`class Shader;`) and call sites remain valid.
// NOTE(renderer-abstraction): Goal 5 will collapse call sites to IShader&.
#include "renderer/opengl/OpenGLShader.h"

namespace Horo {

class Shader : public OpenGLShader {
public:
    Shader() = default;

    // Converting move-constructor from the base factory result.
    explicit Shader(OpenGLShader&& base) : OpenGLShader(std::move(base)) {}

    static Shader FromFiles(const std::string& vertPath,
                            const std::string& fragPath) {
        return Shader(OpenGLShader::FromFiles(vertPath, fragPath));
    }
    static Shader FromSource(const std::string& vertSrc,
                             const std::string& fragSrc) {
        return Shader(OpenGLShader::FromSource(vertSrc, fragSrc));
    }
};

} // namespace Horo
