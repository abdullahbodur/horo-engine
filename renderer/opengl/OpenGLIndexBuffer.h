#pragma once
#include <cstdint>

#include "renderer/IIndexBuffer.h"

namespace Horo {

class OpenGLIndexBuffer : public IIndexBuffer {
public:
    OpenGLIndexBuffer(const uint32_t* indices, uint32_t count);
    ~OpenGLIndexBuffer() override;

    OpenGLIndexBuffer(const OpenGLIndexBuffer&)            = delete;
    OpenGLIndexBuffer& operator=(const OpenGLIndexBuffer&) = delete;

    OpenGLIndexBuffer(OpenGLIndexBuffer&& o) noexcept;
    OpenGLIndexBuffer& operator=(OpenGLIndexBuffer&& o) noexcept;

    // IIndexBuffer interface
    void Bind()   const override;
    void Unbind() const override;

    uint32_t GetCount() const override { return m_count; }

private:
    uint32_t m_rendererId = 0;
    uint32_t m_count      = 0;
};

} // namespace Horo
