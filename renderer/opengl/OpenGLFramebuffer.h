#pragma once
#include <cstdint>
#include <vector>

#include "renderer/IFramebuffer.h"

namespace Horo {

class OpenGLFramebuffer : public IFramebuffer {
public:
    explicit OpenGLFramebuffer(const FramebufferSpec& spec);
    ~OpenGLFramebuffer() override;

    OpenGLFramebuffer(const OpenGLFramebuffer&)            = delete;
    OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;

    OpenGLFramebuffer(OpenGLFramebuffer&& o) noexcept;
    OpenGLFramebuffer& operator=(OpenGLFramebuffer&& o) noexcept;

    // IFramebuffer interface
    void Bind()   override;
    void Unbind() override;
    void Resize(uint32_t width, uint32_t height) override;

    uint32_t GetColorAttachmentId(uint32_t index = 0) const override;

    int  ReadPixel(uint32_t attachmentIndex, int x, int y)   override;
    void ClearAttachment(uint32_t attachmentIndex, int value) override;

    const FramebufferSpec& GetSpec() const override { return m_spec; }

private:
    void Create();
    void Release();

    FramebufferSpec           m_spec;
    uint32_t                  m_framebufferId = 0;
    std::vector<uint32_t>     m_colorAttachments;
    uint32_t                  m_depthAttachment = 0;

    std::vector<FramebufferTextureSpec> m_colorAttachmentSpecs;
    FramebufferTextureSpec              m_depthAttachmentSpec;
};

} // namespace Horo
