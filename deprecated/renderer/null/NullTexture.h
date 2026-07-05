#pragma once

#include "renderer/ITexture.h"
#include "renderer/RenderTargetHandle.h"

namespace Horo {

class NullTexture final : public ITexture {
public:
    explicit NullTexture(const TextureSpec &spec = {}) : m_spec(spec) {}

    void Bind(uint32_t /*slot*/ = 0) const override { ++m_bindCount; }
    void Unbind()                    const override { ++m_unbindCount; }

    int GetWidth()  const override { return static_cast<int>(m_spec.width); }
    int GetHeight() const override { return static_cast<int>(m_spec.height); }
    const TextureSpec &GetSpec() const override { return m_spec; }

    bool IsValid() const override { return true; }

    void SetData(const void *, uint32_t) override { /* No-op: null renderer. */ } // NOSONAR

    RenderTargetHandle GetRenderTargetHandle(bool /*needsYFlip*/ = false) const override {
        return {};
    }

    int GetBindCount()   const { return m_bindCount; }
    int GetUnbindCount() const { return m_unbindCount; }

private:
    mutable int m_bindCount   = 0;
    mutable int m_unbindCount = 0;
    TextureSpec m_spec;
};

} // namespace Horo
