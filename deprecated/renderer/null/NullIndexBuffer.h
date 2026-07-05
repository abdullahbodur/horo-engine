#pragma once

#include "renderer/IIndexBuffer.h"

namespace Horo {

class NullIndexBuffer final : public IIndexBuffer {
public:
    explicit NullIndexBuffer(uint32_t count = 0) : m_count(count) {}

    void Bind()   const override { /* No-op: null renderer. */ }
    void Unbind() const override { /* No-op: null renderer. */ }

    uint32_t GetCount() const override { return m_count; }

private:
    uint32_t m_count = 0;
};

} // namespace Horo
