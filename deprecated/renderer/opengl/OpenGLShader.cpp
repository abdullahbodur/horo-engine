#include "renderer/opengl/OpenGLShader.h"

#include <glad/glad.h>

#include <array>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "core/Logger.h"

namespace Horo {

struct StringHash {
    using is_transparent = void;
    std::size_t operator()(const std::string_view sv) const {
        return std::hash<std::string_view>{}(sv);
    }
};

struct OpenGLShader::ProgramStorage {
    unsigned int program = 0;
    std::unordered_map<std::string, int, StringHash, std::equal_to<>>
        uniformCache;
};

OpenGLShader::OpenGLShader() = default;

OpenGLShader::~OpenGLShader() {
    if (m_programStorage && m_programStorage->program)
        glDeleteProgram(m_programStorage->program);
}

OpenGLShader::OpenGLShader(OpenGLShader&& o) noexcept
    : m_programStorage(std::move(o.m_programStorage)) {}

OpenGLShader& OpenGLShader::operator=(OpenGLShader&& o) noexcept {
    if (this != &o) {
        if (m_programStorage && m_programStorage->program)
            glDeleteProgram(m_programStorage->program);
        m_programStorage = std::move(o.m_programStorage);
    }
    return *this;
}

bool OpenGLShader::IsValid() const {
    return m_programStorage && m_programStorage->program != 0;
}

unsigned int OpenGLShader::GetProgramID() const {
    return m_programStorage ? m_programStorage->program : 0;
}

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw ShaderException("Cannot open shader file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

OpenGLShader OpenGLShader::FromFiles(const std::string& vertPath,
                                     const std::string& fragPath) {
    return FromSource(ReadFile(vertPath), ReadFile(fragPath));
}

OpenGLShader OpenGLShader::FromSource(const std::string& vertSrc,
                                      const std::string& fragSrc) {
    OpenGLShader s;
    s.m_programStorage = std::make_unique<ProgramStorage>();
    unsigned int vert = CompileShader(GL_VERTEX_SHADER, vertSrc);
    unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (vert && frag)
        s.m_programStorage->program = LinkProgram(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);
    return s;
}

unsigned int OpenGLShader::CompileShader(unsigned int type,
                                         const std::string& src) {
    unsigned int id = glCreateShader(type);
    const char* cs  = src.c_str();
    glShaderSource(id, 1, &cs, nullptr);
    glCompileShader(id);

    int ok;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::string log(1024, '\0');
        glGetShaderInfoLog(id, static_cast<GLsizei>(log.size()), nullptr,
                           log.data());
        LogError("Shader compile error:\n{}", log.c_str());
        glDeleteShader(id);
        return 0;
    }
    return id;
}

unsigned int OpenGLShader::LinkProgram(unsigned int vert, unsigned int frag) {
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    int ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::string log(1024, '\0');
        glGetProgramInfoLog(prog, static_cast<GLsizei>(log.size()), nullptr,
                            log.data());
        LogError("Shader link error:\n{}", log.c_str());
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void OpenGLShader::Bind() const {
    glUseProgram(GetProgramID());
}

void OpenGLShader::Unbind() const {
    glUseProgram(0);
}

int OpenGLShader::GetUniformLocation(const std::string& name) const {
    if (!m_programStorage)
        return -1;

    if (auto it = m_programStorage->uniformCache.find(name);
        it != m_programStorage->uniformCache.end())
        return it->second;

    int loc = glGetUniformLocation(m_programStorage->program, name.c_str());
    m_programStorage->uniformCache[name] = loc;
    return loc;
}

// --- Non-const IShader overrides ---

void OpenGLShader::SetInt(const std::string& name, int value) {
    glUniform1i(GetUniformLocation(name), value);
}

void OpenGLShader::SetFloat(const std::string& name, float value) {
    glUniform1f(GetUniformLocation(name), value);
}

void OpenGLShader::SetVec2(const std::string& name, float x, float y) {
    glUniform2f(GetUniformLocation(name), x, y);
}

void OpenGLShader::SetVec3(const std::string& name, const Vec3& v) {
    glUniform3f(GetUniformLocation(name), v.x, v.y, v.z);
}

void OpenGLShader::SetVec4(const std::string& name, const Vec4& v) {
    glUniform4f(GetUniformLocation(name), v.x, v.y, v.z, v.w);
}

void OpenGLShader::SetMat3(const std::string& name, const Mat3& m) {
    std::array<float, 9> data{};
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            data[static_cast<size_t>(col * 3 + row)] = m.m[col][row];
    glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, data.data());
}

void OpenGLShader::SetMat4(const std::string& name, const Mat4& m) {
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, m.Data());
}

void OpenGLShader::SetMat4Array(const std::string& name, int count,
                                const float* data) {
    int loc = GetUniformLocation(name + "[0]");
    if (loc == -1)
        return;
    glUniformMatrix4fv(loc, count, GL_FALSE, data);
}

} // namespace Horo
