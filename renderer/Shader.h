#pragma once
#include <string>
#include <unordered_map>

#include "math/Mat3.h"
#include "math/Mat4.h"
#include "math/Vec3.h"
#include "math/Vec4.h"

namespace Horo {

class Shader {
 public:
  Shader() = default;
  ~Shader();

  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;
  Shader(Shader&& o) noexcept;
  Shader& operator=(Shader&& o) noexcept;

  // Load from files on disk
  static Shader FromFiles(const std::string& vertPath, const std::string& fragPath);

  // Load from inline source strings
  static Shader FromSource(const std::string& vertSrc, const std::string& fragSrc);

  void Bind() const;
  void Unbind() const;

  bool IsValid() const { return m_program != 0; }
  unsigned int GetProgramID() const { return m_program; }

  // Uniform setters
  void SetInt(const std::string& name, int v) const;
  void SetFloat(const std::string& name, float v) const;
  void SetVec2(const std::string& name, float x, float y) const;
  void SetVec3(const std::string& name, const Vec3& v) const;
  void SetVec4(const std::string& name, const Vec4& v) const;
  void SetMat3(const std::string& name, const Mat3& m) const;
  void SetMat4(const std::string& name, const Mat4& m) const;

 private:
  unsigned int m_program = 0;
  mutable std::unordered_map<std::string, int> m_uniformCache;

  int GetUniformLocation(const std::string& name) const;
  static unsigned int CompileShader(unsigned int type, const std::string& src);
  static unsigned int LinkProgram(unsigned int vert, unsigned int frag);
};

}  // namespace Horo
