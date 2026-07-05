#pragma once

#include "renderer/IVertexBuffer.h"

namespace Horo {

class NullVertexBuffer final : public IVertexBuffer {
public:
    void Bind()   const override { /* No-op: null renderer. */ }
    void Unbind() const override { /* No-op: null renderer. */ }

    void SetData(const void *, uint32_t) override { /* No-op: null renderer. */ } // NOSONAR

    const BufferLayout &GetLayout() const override { return m_layout; }
    void SetLayout(const BufferLayout &layout)    override { m_layout = layout; }

private:
    BufferLayout m_layout;
};

} // namespace Horo
