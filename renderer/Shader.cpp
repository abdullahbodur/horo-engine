#include "renderer/Shader.h"

#include <glad/glad.h>

#include <array>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
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

    struct Shader::ProgramStorage {
        unsigned int program = 0;
        std::unordered_map<std::string, int, StringHash, std::equal_to<> >
        uniformCache;
    };

    Shader::Shader() = default;

    Shader::~Shader() {
        if (m_programStorage && m_programStorage->program)
            glDeleteProgram(m_programStorage->program);
    }

    Shader::Shader(Shader &&o) noexcept
        : m_programStorage(std::move(o.m_programStorage)) {
    }

    Shader &Shader::operator=(Shader &&o) noexcept {
        if (this != &o) {
            if (m_programStorage && m_programStorage->program)
                glDeleteProgram(m_programStorage->program);
            m_programStorage = std::move(o.m_programStorage);
        }
        return *this;
    }

    bool Shader::IsValid() const {
        return m_programStorage && m_programStorage->program != 0;
    }

    unsigned int Shader::GetProgramID() const {
        return m_programStorage ? m_programStorage->program : 0;
    }

    static std::string ReadFile(const std::string &path) {
        std::ifstream f(path);
        if (!f.is_open())
            throw ShaderException("Cannot open shader file: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    Shader Shader::FromFiles(const std::string &vertPath,
                             const std::string &fragPath) {
        return FromSource(ReadFile(vertPath), ReadFile(fragPath));
    }

    Shader Shader::FromSource(const std::string &vertSrc,
                              const std::string &fragSrc) {
        Shader s;
        s.m_programStorage = std::make_unique<ProgramStorage>();
        unsigned int vert = CompileShader(GL_VERTEX_SHADER, vertSrc);
        unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
        s.m_programStorage->program = LinkProgram(vert, frag);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return s;
    }

    unsigned int Shader::CompileShader(unsigned int type, const std::string &src) {
        unsigned int id = glCreateShader(type);
        const char *cs = src.c_str();
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

    unsigned int Shader::LinkProgram(unsigned int vert, unsigned int frag) {
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

    void Shader::Bind() const {
        glUseProgram(GetProgramID());
    }

    void Shader::Unbind() const {
        glUseProgram(0);
    }

    int Shader::GetUniformLocation(const std::string &name) const {
        if (!m_programStorage)
            return -1;

        if (auto it = m_programStorage->uniformCache.find(name);
            it != m_programStorage->uniformCache.end())
            return it->second;

        int loc = glGetUniformLocation(m_programStorage->program, name.c_str());
        m_programStorage->uniformCache[name] = loc;
        return loc;
    }

    void Shader::SetInt(const std::string &name, int v) const {
        glUniform1i(GetUniformLocation(name), v);
    }

    void Shader::SetFloat(const std::string &name, float v) const {
        glUniform1f(GetUniformLocation(name), v);
    }

    void Shader::SetVec2(const std::string &name, float x, float y) const {
        glUniform2f(GetUniformLocation(name), x, y);
    }

    void Shader::SetVec3(const std::string &name, const Vec3 &v) const {
        glUniform3f(GetUniformLocation(name), v.x, v.y, v.z);
    }

    void Shader::SetVec4(const std::string &name, const Vec4 &v) const {
        glUniform4f(GetUniformLocation(name), v.x, v.y, v.z, v.w);
    }

    void Shader::SetMat3(const std::string &name, const Mat3 &m) const {
        // Convert column-major Mat3 to flat float array
        std::array<float, 9> data{};
        for (int col = 0; col < 3; col++)
            for (int row = 0; row < 3; row++)
                data[static_cast<size_t>(col * 3 + row)] = m.m[col][row];
        glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, data.data());
    }

    void Shader::SetMat4(const std::string &name, const Mat4 &m) const {
        glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, m.Data());
    }

    void Shader::SetMat4Array(const std::string &name, int count,
                              const float *data) const {
        int loc = GetUniformLocation(name + "[0]");
        if (loc == -1)
            return;
        glUniformMatrix4fv(loc, count, GL_FALSE, data);
    }
} // namespace Horo
