#include "renderer/opengl/OpenGLVertexArray.h"

#include <glad/glad.h>

#include <cassert>
#include <utility>

#include "renderer/opengl/OpenGLVertexBuffer.h"

namespace Horo {

namespace {

GLenum ShaderDataTypeToGLBaseType(ShaderDataType type) {
    switch (type) {
        case ShaderDataType::Float:
        case ShaderDataType::Float2:
        case ShaderDataType::Float3:
        case ShaderDataType::Float4:
        case ShaderDataType::Mat3:
        case ShaderDataType::Mat4:   return GL_FLOAT;
        case ShaderDataType::Int:
        case ShaderDataType::Int2:
        case ShaderDataType::Int3:
        case ShaderDataType::Int4:   return GL_INT;
        case ShaderDataType::Bool:   return GL_BOOL;
        case ShaderDataType::None:   return GL_NONE;
    }
    return GL_NONE;
}

} // namespace

OpenGLVertexArray::OpenGLVertexArray() {
    glGenVertexArrays(1, &m_rendererId);
}

OpenGLVertexArray::~OpenGLVertexArray() {
    if (m_rendererId)
        glDeleteVertexArrays(1, &m_rendererId);
}

OpenGLVertexArray::OpenGLVertexArray(OpenGLVertexArray&& o) noexcept
    : m_rendererId(o.m_rendererId),
      m_vertexBufferIndex(o.m_vertexBufferIndex),
      m_vertexBuffers(std::move(o.m_vertexBuffers)),
      m_indexBuffer(std::move(o.m_indexBuffer)) {
    o.m_rendererId         = 0;
    o.m_vertexBufferIndex  = 0;
}

OpenGLVertexArray& OpenGLVertexArray::operator=(OpenGLVertexArray&& o) noexcept {
    if (this != &o) {
        if (m_rendererId)
            glDeleteVertexArrays(1, &m_rendererId);
        m_rendererId        = o.m_rendererId;
        m_vertexBufferIndex = o.m_vertexBufferIndex;
        m_vertexBuffers     = std::move(o.m_vertexBuffers);
        m_indexBuffer       = std::move(o.m_indexBuffer);
        o.m_rendererId         = 0;
        o.m_vertexBufferIndex  = 0;
    }
    return *this;
}

void OpenGLVertexArray::Bind() const {
    glBindVertexArray(m_rendererId);
}

void OpenGLVertexArray::Unbind() const {
    glBindVertexArray(0);
}

void OpenGLVertexArray::AddVertexBuffer(
    std::shared_ptr<IVertexBuffer> vertexBuffer) {
    assert(!vertexBuffer->GetLayout().GetElements().empty());

    glBindVertexArray(m_rendererId);
    vertexBuffer->Bind();

    const auto& layout = vertexBuffer->GetLayout();
    for (const auto& element : layout) {
        switch (element.type) {
            case ShaderDataType::Float:
            case ShaderDataType::Float2:
            case ShaderDataType::Float3:
            case ShaderDataType::Float4: {
                glEnableVertexAttribArray(m_vertexBufferIndex);
                glVertexAttribPointer(
                    m_vertexBufferIndex,
                    static_cast<GLint>(element.GetComponentCount()),
                    ShaderDataTypeToGLBaseType(element.type),
                    element.normalized ? GL_TRUE : GL_FALSE,
                    static_cast<GLsizei>(layout.GetStride()),
                    reinterpret_cast<const void*>(element.offset));
                ++m_vertexBufferIndex;
                break;
            }
            case ShaderDataType::Int:
            case ShaderDataType::Int2:
            case ShaderDataType::Int3:
            case ShaderDataType::Int4:
            case ShaderDataType::Bool: {
                glEnableVertexAttribArray(m_vertexBufferIndex);
                glVertexAttribIPointer(
                    m_vertexBufferIndex,
                    static_cast<GLint>(element.GetComponentCount()),
                    ShaderDataTypeToGLBaseType(element.type),
                    static_cast<GLsizei>(layout.GetStride()),
                    reinterpret_cast<const void*>(element.offset));
                ++m_vertexBufferIndex;
                break;
            }
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4: {
                // Matrix attributes occupy multiple consecutive attribute slots
                const uint32_t count = element.GetComponentCount();
                for (uint32_t col = 0; col < count; ++col) {
                    glEnableVertexAttribArray(m_vertexBufferIndex);
                    glVertexAttribPointer(
                        m_vertexBufferIndex,
                        static_cast<GLint>(count),
                        GL_FLOAT,
                        element.normalized ? GL_TRUE : GL_FALSE,
                        static_cast<GLsizei>(layout.GetStride()),
                        reinterpret_cast<const void*>(
                            element.offset + sizeof(float) * count * col));
                    glVertexAttribDivisor(m_vertexBufferIndex, 1);
                    ++m_vertexBufferIndex;
                }
                break;
            }
            default:
                break;
        }
    }

    m_vertexBuffers.push_back(std::move(vertexBuffer));
    glBindVertexArray(0);
}

void OpenGLVertexArray::SetIndexBuffer(
    std::shared_ptr<IIndexBuffer> indexBuffer) {
    glBindVertexArray(m_rendererId);
    indexBuffer->Bind();
    m_indexBuffer = std::move(indexBuffer);
    glBindVertexArray(0);
}

void OpenGLVertexArray::DrawIndexed(uint32_t count) const {
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT,
                   nullptr);
}

void OpenGLVertexArray::DrawArrays(uint32_t count) const {
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
}

void OpenGLVertexArray::DrawArraysLines(uint32_t count) const {
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(count));
}

} // namespace Horo