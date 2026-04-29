#pragma once
#include <memory>
#include <vector>

#include "renderer/IIndexBuffer.h"
#include "renderer/IVertexBuffer.h"

namespace Horo {

class IVertexArray {
public:
    virtual ~IVertexArray() = default;

    virtual void Bind()   const = 0;
    virtual void Unbind() const = 0;

    virtual void AddVertexBuffer(std::shared_ptr<IVertexBuffer> vertexBuffer) = 0;
    virtual void SetIndexBuffer(std::shared_ptr<IIndexBuffer>  indexBuffer)   = 0;

    virtual const std::vector<std::shared_ptr<IVertexBuffer>>& GetVertexBuffers() const = 0;
    virtual const std::shared_ptr<IIndexBuffer>&               GetIndexBuffer()   const = 0;
};

} // namespace Horo
