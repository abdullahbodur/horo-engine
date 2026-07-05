#pragma once
#include <memory>
#include <string>

#include "renderer/ITexture.h"
#include "renderer/RenderTargetHandle.h"

namespace Horo {

class OpenGLTexture : public ITexture {
public:
    OpenGLTexture();
    ~OpenGLTexture() override;

    OpenGLTexture(const OpenGLTexture&)            = delete;
    OpenGLTexture& operator=(const OpenGLTexture&) = delete;

    OpenGLTexture(OpenGLTexture&& o) noexcept;
    OpenGLTexture& operator=(OpenGLTexture&& o) noexcept;

    static OpenGLTexture FromSpec(const TextureSpec& spec);
    static OpenGLTexture FromFile(const std::string& path, bool flipY = true);
    static OpenGLTexture CreateWhite1x1();

    // ITexture interface
    void Bind(uint32_t slot = 0) const override;
    void Unbind()                const override;

    int GetWidth()  const override { return m_width;  }
    int GetHeight() const override { return m_height; }
    const TextureSpec& GetSpec() const override { return m_spec; }

    bool IsValid() const override;

    void SetData(const void* data, uint32_t size) override;

    RenderTargetHandle GetRenderTargetHandle(bool needsYFlip = false) const override;

    // Legacy escape hatch for editor integration until offscreen/ImGui texture
    // presentation is fully backend-neutral.
    // NOTE(renderer-abstraction): Goal 5 will replace this call site.
    unsigned int GetNativeId() const;

private:
    struct TextureStorage;
    std::unique_ptr<TextureStorage> m_textureStorage;
    int m_width  = 0;
    int m_height = 0;
    TextureSpec m_spec;
};

} // namespace Horo
