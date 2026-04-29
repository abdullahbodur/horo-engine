#pragma once
#include <cstdint>

#include "renderer/IVertexBuffer.h"

namespace Horo {

class NullVertexBuffer final : public IVertexBuffer {
public:
    void Bind()   const override {}
    void Unbind() const override {}

    void SetData(const void *, uint32_t) override {}

    const BufferLayout &GetLayout() const override { return m_layout; }
    void SetLayout(const BufferLayout &layout)    override { m_layout = layout; }

private:
    BufferLayout m_layout;
};

} // namespace Horo
