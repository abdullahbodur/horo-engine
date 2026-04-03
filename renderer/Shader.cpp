#include "renderer/Shader.h"

#include <glad/glad.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "core/Logger.h"

namespace Horo {

Shader::~Shader() {
  if (m_program)
    glDeleteProgram(m_program);
}

Shader::Shader(Shader&& o) noexcept
    : m_program(o.m_program), m_uniformCache(std::move(o.m_uniformCache)) {
  o.m_program = 0;
}

Shader& Shader::operator=(Shader&& o) noexcept {
  if (this != &o) {
    if (m_program)
      glDeleteProgram(m_program);
    m_program = o.m_program;
    m_uniformCache = std::move(o.m_uniformCache);
    o.m_program = 0;
  }
  return *this;
}

static std::string ReadFile(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open())
    throw std::runtime_error("Cannot open shader file: " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

Shader Shader::FromFiles(const std::string& vertPath, const std::string& fragPath) {
  return FromSource(ReadFile(vertPath), ReadFile(fragPath));
}

Shader Shader::FromSource(const std::string& vertSrc, const std::string& fragSrc) {
  Shader s;
  unsigned int vert = CompileShader(GL_VERTEX_SHADER, vertSrc);
  unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
  s.m_program = LinkProgram(vert, frag);
  glDeleteShader(vert);
  glDeleteShader(frag);
  return s;
}

unsigned int Shader::CompileShader(unsigned int type, const std::string& src) {
  unsigned int id = glCreateShader(type);
  const char* cs = src.c_str();
  glShaderSource(id, 1, &cs, nullptr);
  glCompileShader(id);

  int ok;
  glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(id, sizeof(log), nullptr, log);
    LOG_ERROR("Shader compile error:\n%s", log);
    glDeleteShader(id);
    return 0;
  }
  return id;
}

unsigned int Shader::LinkProgram(unsigned int vert, unsigned int frag) {
  unsigned int prog = glCreateProgram();
  glAttachShader(prog, vert);
  glAttachShader(prog, frag);
  glLinkProgram(prog);

  int ok;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
    LOG_ERROR("Shader link error:\n%s", log);
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

void Shader::Bind() const {
  glUseProgram(m_program);
}
void Shader::Unbind() const {
  glUseProgram(0);
}

int Shader::GetUniformLocation(const std::string& name) const {
  auto it = m_uniformCache.find(name);
  if (it != m_uniformCache.end())
    return it->second;

  int loc = glGetUniformLocation(m_program, name.c_str());
  m_uniformCache[name] = loc;
  return loc;
}

void Shader::SetInt(const std::string& name, int v) const {
  glUniform1i(GetUniformLocation(name), v);
}
void Shader::SetFloat(const std::string& name, float v) const {
  glUniform1f(GetUniformLocation(name), v);
}
void Shader::SetVec2(const std::string& name, float x, float y) const {
  glUniform2f(GetUniformLocation(name), x, y);
}
void Shader::SetVec3(const std::string& name, const Vec3& v) const {
  glUniform3f(GetUniformLocation(name), v.x, v.y, v.z);
}
void Shader::SetVec4(const std::string& name, const Vec4& v) const {
  glUniform4f(GetUniformLocation(name), v.x, v.y, v.z, v.w);
}

void Shader::SetMat3(const std::string& name, const Mat3& m) const {
  // Convert column-major Mat3 to flat float[9]
  float data[9];
  for (int col = 0; col < 3; col++)
    for (int row = 0; row < 3; row++)
      data[col * 3 + row] = m.m[col][row];
  glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, data);
}

void Shader::SetMat4(const std::string& name, const Mat4& m) const {
  glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, m.Data());
}

}  // namespace Horo
