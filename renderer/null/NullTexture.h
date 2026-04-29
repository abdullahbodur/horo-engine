#pragma once
#include <cstdint>

#include "renderer/ITexture.h"
#include "renderer/RenderTargetHandle.h"

namespace Horo {

class NullTexture final : public ITexture {
public:
    mutable int bindCount   = 0;
    mutable int unbindCount = 0;

    explicit NullTexture(const TextureSpec &spec = {}) : m_spec(spec) {}

    void Bind(uint32_t /*slot*/ = 0) const override { ++bindCount; }
    void Unbind()                    const override { ++unbindCount; }

    int GetWidth()  const override { return static_cast<int>(m_spec.width); }
    int GetHeight() const override { return static_cast<int>(m_spec.height); }
    const TextureSpec &GetSpec() const override { return m_spec; }

    bool IsValid() const override { return true; }

    void SetData(const void *, uint32_t) override {}

    RenderTargetHandle GetRenderTargetHandle(bool /*needsYFlip*/ = false) const override {
        return {};
    }

    bool operator==(const ITexture &other) const override {
        return this == &other;
    }

private:
    TextureSpec m_spec;
};

} // namespace Horo
