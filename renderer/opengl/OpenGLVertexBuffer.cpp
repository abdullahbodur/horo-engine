#include "renderer/opengl/OpenGLVertexBuffer.h"

#include <glad/glad.h>

#include <utility>

namespace Horo {

// --- OpenGLVertexBuffer ------------------------------------------------------

OpenGLVertexBuffer::OpenGLVertexBuffer(const void* data, uint32_t size) {
    glGenBuffers(1, &m_rendererId);
    glBindBuffer(GL_ARRAY_BUFFER, m_rendererId);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size) {
    glGenBuffers(1, &m_rendererId);
    glBindBuffer(GL_ARRAY_BUFFER, m_rendererId);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

OpenGLVertexBuffer::~OpenGLVertexBuffer() {
    if (m_rendererId)
        glDeleteBuffers(1, &m_rendererId);
}

OpenGLVertexBuffer::OpenGLVertexBuffer(OpenGLVertexBuffer&& o) noexcept
    : m_rendererId(o.m_rendererId), m_layout(std::move(o.m_layout)) {
    o.m_rendererId = 0;
}

OpenGLVertexBuffer& OpenGLVertexBuffer::operator=(OpenGLVertexBuffer&& o) noexcept {
    if (this != &o) {
        if (m_rendererId)
            glDeleteBuffers(1, &m_rendererId);
        m_rendererId   = o.m_rendererId;
        m_layout       = std::move(o.m_layout);
        o.m_rendererId = 0;
    }
    return *this;
}

void OpenGLVertexBuffer::Bind() const {
    glBindBuffer(GL_ARRAY_BUFFER, m_rendererId);
}

void OpenGLVertexBuffer::Unbind() const {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLVertexBuffer::SetData(const void* data, uint32_t size) {
    glBindBuffer(GL_ARRAY_BUFFER, m_rendererId);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size), data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace Horo
