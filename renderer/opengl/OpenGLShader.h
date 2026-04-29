#pragma once
#include <memory>
#include <stdexcept>
#include <string>

#include "math/Mat3.h"
#include "math/Mat4.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "renderer/IShader.h"

namespace Horo {

class ShaderException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class OpenGLShader : public IShader {
public:
    OpenGLShader();
    ~OpenGLShader() override;

    OpenGLShader(const OpenGLShader&)            = delete;
    OpenGLShader& operator=(const OpenGLShader&) = delete;

    OpenGLShader(OpenGLShader&& o) noexcept;
    OpenGLShader& operator=(OpenGLShader&& o) noexcept;

    static OpenGLShader FromFiles(const std::string& vertPath,
                                  const std::string& fragPath);
    static OpenGLShader FromSource(const std::string& vertSrc,
                                   const std::string& fragSrc);

    // IShader interface (non-const, as required by the interface)
    void Bind()   const override;
    void Unbind() const override;

    void SetInt(const std::string& name, int value)          override;
    void SetFloat(const std::string& name, float value)      override;
    void SetVec2(const std::string& name, float x, float y)  override;
    void SetVec3(const std::string& name, const Vec3& v)     override;
    void SetVec4(const std::string& name, const Vec4& v)     override;
    void SetMat3(const std::string& name, const Mat3& m)     override;
    void SetMat4(const std::string& name, const Mat4& m)     override;
    void SetMat4Array(const std::string& name, int count,
                      const float* data)                     override;
    bool IsValid() const override;

    // Const overloads for backward compatibility — existing call sites use
    // const Shader& / const Shader* and will continue to compile when
    //   using Shader = OpenGLShader;
    // TODO(renderer-abstraction): Goal 5 will remove these overloads once all
    //   call sites are ported to IShader&.
    void SetInt(const std::string& name, int value)          const;
    void SetFloat(const std::string& name, float value)      const;
    void SetVec2(const std::string& name, float x, float y)  const;
    void SetVec3(const std::string& name, const Vec3& v)     const;
    void SetVec4(const std::string& name, const Vec4& v)     const;
    void SetMat3(const std::string& name, const Mat3& m)     const;
    void SetMat4(const std::string& name, const Mat4& m)     const;
    void SetMat4Array(const std::string& name, int count,
                      const float* data)                     const;

    unsigned int GetProgramID() const;

private:
    struct ProgramStorage;
    std::unique_ptr<ProgramStorage> m_programStorage;

    int GetUniformLocation(const std::string& name) const;

    static unsigned int CompileShader(unsigned int type, const std::string& src);
    static unsigned int LinkProgram(unsigned int vert, unsigned int frag);
};

} // namespace Horo
