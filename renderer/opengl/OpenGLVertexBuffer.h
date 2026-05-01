#pragma once
#include <cstdint>

#include "renderer/IVertexBuffer.h"

namespace Horo {

class OpenGLVertexBuffer : public IVertexBuffer {
public:
    // Create a static VBO pre-filled with data.
    OpenGLVertexBuffer(const void* data, uint32_t size);

    // Create a dynamic VBO with reserved capacity (no initial data).
    explicit OpenGLVertexBuffer(uint32_t size);

    ~OpenGLVertexBuffer() override;

    OpenGLVertexBuffer(const OpenGLVertexBuffer&)            = delete;
    OpenGLVertexBuffer& operator=(const OpenGLVertexBuffer&) = delete;

    OpenGLVertexBuffer(OpenGLVertexBuffer&& o) noexcept;
    OpenGLVertexBuffer& operator=(OpenGLVertexBuffer&& o) noexcept;

    // IVertexBuffer interface
    void Bind()   const override;
    void Unbind() const override;

    void SetData(const void* data, uint32_t size) override;
    const BufferLayout& GetLayout()                const override { return m_layout; }
    void SetLayout(const BufferLayout& layout)           override { m_layout = layout; }

private:
    uint32_t    m_rendererId = 0;
    uint32_t    m_capacity   = 0;
    BufferLayout m_layout;
};

} // namespace Horo
