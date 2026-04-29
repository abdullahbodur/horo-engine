#pragma once
#include <cstdint>
#include <vector>

namespace Horo {

enum class FramebufferTextureFormat {
    None = 0,
    RGBA8,
    RED_INTEGER,
    DEPTH24STENCIL8
};

struct FramebufferTextureSpec {
    FramebufferTextureFormat format = FramebufferTextureFormat::None;
};

struct FramebufferAttachmentSpec {
    std::vector<FramebufferTextureSpec> attachments;
};

struct FramebufferSpec {
    uint32_t                 width           = 0;
    uint32_t                 height          = 0;
    uint32_t                 samples         = 1;
    FramebufferAttachmentSpec attachmentSpec;
    bool                     swapChainTarget = false;
};

class IFramebuffer {
public:
    virtual ~IFramebuffer() = default;

    virtual void Bind()   = 0;
    virtual void Unbind() = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;

    // Returns the native texture handle for the given color attachment (e.g.
    // an OpenGL texture ID cast to uint32_t).
    virtual uint32_t GetColorAttachmentId(uint32_t index = 0) const = 0;

    virtual int  ReadPixel(uint32_t attachmentIndex, int x, int y)   = 0;
    virtual void ClearAttachment(uint32_t attachmentIndex, int value) = 0;

    virtual const FramebufferSpec& GetSpec() const = 0;
};

} // namespace Horo
