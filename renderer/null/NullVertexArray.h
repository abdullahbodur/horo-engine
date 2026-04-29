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
    mutable int bindCount   = 0;
    mutable int unbindCount = 0;

    void Bind()   const override { ++bindCount; }
    void Unbind() const override { ++unbindCount; }

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

private:
    std::vector<std::shared_ptr<IVertexBuffer>> m_vertexBuffers;
    std::shared_ptr<IIndexBuffer>               m_indexBuffer;
};

} // namespace Horo
