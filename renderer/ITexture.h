#pragma once
#include <cstdint>

#include "renderer/RenderTargetHandle.h"

namespace Horo {

enum class TextureFormat { RGBA8, RGB8, R8, Depth24Stencil8 };
enum class TextureFilter { Linear, Nearest };
enum class TextureWrap   { Repeat, ClampToEdge, MirroredRepeat };

struct TextureSpec {
    uint32_t      width        = 1;
    uint32_t      height       = 1;
    TextureFormat format       = TextureFormat::RGBA8;
    TextureFilter filter       = TextureFilter::Linear;
    TextureWrap   wrap         = TextureWrap::Repeat;
    bool          generateMips = true;
};

class ITexture {
public:
    virtual ~ITexture() = default;

    virtual void Bind(uint32_t slot = 0) const = 0;
    virtual void Unbind()                const = 0;

    virtual int GetWidth()  const = 0;
    virtual int GetHeight() const = 0;
    virtual const TextureSpec& GetSpec() const = 0;

    virtual bool IsValid() const = 0;

    // Upload pixel data after creation.
    virtual void SetData(const void* data, uint32_t size) = 0;

    // Backend-agnostic handle for ImGui / render-target presentation.
    virtual RenderTargetHandle GetRenderTargetHandle(bool needsYFlip = false) const = 0;

    virtual bool operator==(const ITexture& other) const = 0;
};

} // namespace Horo
