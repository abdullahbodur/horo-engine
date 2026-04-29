#include "renderer/opengl/OpenGLVertexBuffer.h"

#include <glad/glad.h>

#include <utility>

namespace Horo {

// --- BufferElement -----------------------------------------------------------

uint32_t ShaderDataTypeSize(ShaderDataType type) {
    switch (type) {
        case ShaderDataType::Float:  return 4;
        case ShaderDataType::Float2: return 4 * 2;
        case ShaderDataType::Float3: return 4 * 3;
        case ShaderDataType::Float4: return 4 * 4;
        case ShaderDataType::Mat3:   return 4 * 3 * 3;
        case ShaderDataType::Mat4:   return 4 * 4 * 4;
        case ShaderDataType::Int:    return 4;
        case ShaderDataType::Int2:   return 4 * 2;
        case ShaderDataType::Int3:   return 4 * 3;
        case ShaderDataType::Int4:   return 4 * 4;
        case ShaderDataType::Bool:   return 1;
        case ShaderDataType::None:   return 0;
    }
    return 0;
}

BufferElement::BufferElement(ShaderDataType type, const std::string& name,
                             bool normalized)
    : name(name), type(type), size(ShaderDataTypeSize(type)), offset(0),
      normalized(normalized) {}

uint32_t BufferElement::GetComponentCount() const {
    switch (type) {
        case ShaderDataType::Float:  return 1;
        case ShaderDataType::Float2: return 2;
        case ShaderDataType::Float3: return 3;
        case ShaderDataType::Float4: return 4;
        case ShaderDataType::Mat3:   return 3 * 3;
        case ShaderDataType::Mat4:   return 4 * 4;
        case ShaderDataType::Int:    return 1;
        case ShaderDataType::Int2:   return 2;
        case ShaderDataType::Int3:   return 3;
        case ShaderDataType::Int4:   return 4;
        case ShaderDataType::Bool:   return 1;
        case ShaderDataType::None:   return 0;
    }
    return 0;
}

// --- BufferLayout ------------------------------------------------------------

BufferLayout::BufferLayout(std::initializer_list<BufferElement> elements)
    : m_Elements(elements) {
    CalculateOffsetsAndStride();
}

void BufferLayout::CalculateOffsetsAndStride() {
    size_t   offset = 0;
    m_Stride        = 0;
    for (auto& element : m_Elements) {
        element.offset  = offset;
        offset         += element.size;
        m_Stride       += element.size;
    }
}

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
