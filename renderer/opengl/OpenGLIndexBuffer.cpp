#include "renderer/opengl/OpenGLIndexBuffer.h"

#include <glad/glad.h>

#include <utility>

namespace Horo {

OpenGLIndexBuffer::OpenGLIndexBuffer(const uint32_t* indices, uint32_t count)
    : m_count(count) {
    glGenBuffers(1, &m_rendererId);
    // Bind to GL_ELEMENT_ARRAY_BUFFER to configure EBO, but do not bind it
    // globally here — the VAO captures the EBO binding.
    glBindBuffer(GL_ARRAY_BUFFER, m_rendererId);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(count * sizeof(uint32_t)),
                 indices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

OpenGLIndexBuffer::~OpenGLIndexBuffer() {
    if (m_rendererId)
        glDeleteBuffers(1, &m_rendererId);
}

OpenGLIndexBuffer::OpenGLIndexBuffer(OpenGLIndexBuffer&& o) noexcept
    : m_rendererId(o.m_rendererId), m_count(o.m_count) {
    o.m_rendererId = 0;
    o.m_count      = 0;
}

OpenGLIndexBuffer& OpenGLIndexBuffer::operator=(OpenGLIndexBuffer&& o) noexcept {
    if (this != &o) {
        if (m_rendererId)
            glDeleteBuffers(1, &m_rendererId);
        m_rendererId   = o.m_rendererId;
        m_count        = o.m_count;
        o.m_rendererId = 0;
        o.m_count      = 0;
    }
    return *this;
}

void OpenGLIndexBuffer::Bind() const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_rendererId);
}

void OpenGLIndexBuffer::Unbind() const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

} // namespace Horo
