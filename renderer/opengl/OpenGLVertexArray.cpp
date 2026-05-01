#include "renderer/opengl/OpenGLVertexArray.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <cassert>
#include <utility>

#include "renderer/opengl/OpenGLVertexBuffer.h"

namespace Horo {

namespace {

GLenum ShaderDataTypeToGLBaseType(ShaderDataType type) {
    using enum ShaderDataType;
    switch (type) {
        case Float:
        case Float2:
        case Float3:
        case Float4:
        case Mat3:
        case Mat4:   return GL_FLOAT;
        case Int:
        case Int2:
        case Int3:
        case Int4:   return GL_INT;
        case Bool:   return GL_UNSIGNED_BYTE; // bool is 1 byte; GL_BOOL is not valid for vertex attrib pointers
        case None:   return GL_NONE;
    }
    return GL_NONE;
}

} // namespace

OpenGLVertexArray::OpenGLVertexArray() {
    glGenVertexArrays(1, &m_rendererId);
}

OpenGLVertexArray::~OpenGLVertexArray() {
    if (m_rendererId && glfwGetCurrentContext())
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
        using enum ShaderDataType;
        switch (element.type) {
            case Float:
            case Float2:
            case Float3:
            case Float4: {
                glEnableVertexAttribArray(m_vertexBufferIndex);
                glVertexAttribPointer(
                    m_vertexBufferIndex,
                    static_cast<GLint>(element.GetComponentCount()),
                    ShaderDataTypeToGLBaseType(element.type),
                    element.normalized ? GL_TRUE : GL_FALSE,
                    static_cast<GLsizei>(layout.GetStride()),
                    reinterpret_cast<const void*>(element.offset)); // NOSONAR: required by OpenGL API
                ++m_vertexBufferIndex;
                break;
            }
            case Int:
            case Int2:
            case Int3:
            case Int4:
            case Bool: {
                glEnableVertexAttribArray(m_vertexBufferIndex);
                glVertexAttribIPointer(
                    m_vertexBufferIndex,
                    static_cast<GLint>(element.GetComponentCount()),
                    ShaderDataTypeToGLBaseType(element.type),
                    static_cast<GLsizei>(layout.GetStride()),
                    reinterpret_cast<const void*>(element.offset)); // NOSONAR: required by OpenGL API
                ++m_vertexBufferIndex;
                break;
            }
            case Mat3:
            case Mat4: {
                // Matrix attributes occupy multiple consecutive attribute slots.
                // Each column is a separate vec3/vec4 attribute.
                const uint32_t cols = (element.type == Mat4) ? 4u : 3u;
                for (uint32_t col = 0; col < cols; ++col) {
                    glEnableVertexAttribArray(m_vertexBufferIndex);
                    glVertexAttribPointer(
                        m_vertexBufferIndex,
                        static_cast<GLint>(cols),   // 3 or 4 components per column
                        GL_FLOAT,
                        element.normalized ? GL_TRUE : GL_FALSE,
                        static_cast<GLsizei>(layout.GetStride()),
                        reinterpret_cast<const void*>( // NOSONAR: required by OpenGL API
                            element.offset + sizeof(float) * cols * col));
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