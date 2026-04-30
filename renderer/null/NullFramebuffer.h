#pragma once
#include <cstdint>

#include "renderer/IFramebuffer.h"

namespace Horo {

class NullFramebuffer final : public IFramebuffer {
public:
    explicit NullFramebuffer(const FramebufferSpec &spec = {}) : m_spec(spec) {}

    void Bind()   override {}
    void Unbind() override {}
    void Resize(uint32_t w, uint32_t h) override {
        m_spec.width  = w;
        m_spec.height = h;
    }

    uint32_t GetColorAttachmentId(uint32_t /*index*/ = 0) const override { return 0; }
    int      ReadPixel(uint32_t /*index*/, int /*x*/, int /*y*/) override { return 0; }
    void     ClearAttachment(uint32_t /*index*/, int /*value*/) override {}

    const FramebufferSpec &GetSpec() const override { return m_spec; }

private:
    FramebufferSpec m_spec;
};

} // namespace Horo
