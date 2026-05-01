#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "renderer/IVertexArray.h"
#include "renderer/IIndexBuffer.h"
#include "renderer/IVertexBuffer.h"

namespace Horo {

class NullVertexArray final : public IVertexArray {
public:
    void Bind()   const override { ++m_bindCount; }
    void Unbind() const override { ++m_unbindCount; }

    void AddVertexBuffer(std::shared_ptr<IVertexBuffer> vb) override {
        m_vertexBuffers.push_back(std::move(vb));
    }
    void SetIndexBuffer(std::shared_ptr<IIndexBuffer> ib) override {
        m_indexBuffer = std::move(ib);
    }

    const std::vector<std::shared_ptr<IVertexBuffer>> &GetVertexBuffers() const override {
        return m_vertexBuffers;
    }
    const std::shared_ptr<IIndexBuffer> &GetIndexBuffer() const override {
        return m_indexBuffer;
    }

    void DrawIndexed(uint32_t) const override { /* No-op: null renderer. */ }
    void DrawArrays(uint32_t) const override { /* No-op: null renderer. */ }
    void DrawArraysLines(uint32_t) const override { /* No-op: null renderer. */ }

    int GetBindCount()   const { return m_bindCount; }
    int GetUnbindCount() const { return m_unbindCount; }

private:
    mutable int                                 m_bindCount   = 0;
    mutable int                                 m_unbindCount = 0;
    std::vector<std::shared_ptr<IVertexBuffer>> m_vertexBuffers;
    std::shared_ptr<IIndexBuffer>               m_indexBuffer;
};

} // namespace Horo
