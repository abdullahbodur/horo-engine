#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "renderer/IVertexArray.h"

namespace Horo {

class OpenGLVertexArray : public IVertexArray {
public:
    OpenGLVertexArray();
    ~OpenGLVertexArray() override;

    OpenGLVertexArray(const OpenGLVertexArray&)            = delete;
    OpenGLVertexArray& operator=(const OpenGLVertexArray&) = delete;

    OpenGLVertexArray(OpenGLVertexArray&& o) noexcept;
    OpenGLVertexArray& operator=(OpenGLVertexArray&& o) noexcept;

    // IVertexArray interface
    void Bind()   const override;
    void Unbind() const override;

    void AddVertexBuffer(std::shared_ptr<IVertexBuffer> vertexBuffer) override;
    void SetIndexBuffer(std::shared_ptr<IIndexBuffer>  indexBuffer)   override;

    const std::vector<std::shared_ptr<IVertexBuffer>>& GetVertexBuffers() const override { return m_vertexBuffers; }
    const std::shared_ptr<IIndexBuffer>&               GetIndexBuffer()   const override { return m_indexBuffer;   }

    void DrawIndexed(uint32_t count) const override;
    void DrawArrays(uint32_t count)      const override;
    void DrawArraysLines(uint32_t count, float lineWidth = 1.0f) const override;

private:
    uint32_t m_rendererId = 0;
    uint32_t m_vertexBufferIndex = 0;

    std::vector<std::shared_ptr<IVertexBuffer>> m_vertexBuffers;
    std::shared_ptr<IIndexBuffer>               m_indexBuffer;
};

} // namespace Horo
