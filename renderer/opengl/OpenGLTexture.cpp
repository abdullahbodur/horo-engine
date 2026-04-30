// stb_image implementation — included once, here.
#define STB_IMAGE_IMPLEMENTATION
#include "renderer/opengl/OpenGLTexture.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <array>
#include <cstdint>
#include <memory>

#include "core/Logger.h"

namespace Horo {

struct OpenGLTexture::TextureStorage {
    unsigned int id = 0;
};

namespace {

GLenum TextureFormatToInternalFormat(TextureFormat format) {
    using enum TextureFormat;
    switch (format) {
        case RGBA8:            return GL_RGBA8;
        case RGB8:             return GL_RGB8;
        case R8:               return GL_R8;
        case Depth24Stencil8:  return GL_DEPTH24_STENCIL8;
    }
    return GL_RGBA8;
}

GLenum TextureFormatToDataFormat(TextureFormat format) {
    using enum TextureFormat;
    switch (format) {
        case RGBA8:            return GL_RGBA;
        case RGB8:             return GL_RGB;
        case R8:               return GL_RED;
        case Depth24Stencil8:  return GL_DEPTH_STENCIL;
    }
    return GL_RGBA;
}

GLenum TextureFormatToDataType(TextureFormat format) {
    using enum TextureFormat;
    switch (format) {
        case Depth24Stencil8:  return GL_UNSIGNED_INT_24_8;
        default:               return GL_UNSIGNED_BYTE;
    }
}

GLenum TextureFilterToGL(TextureFilter filter) {
    return filter == TextureFilter::Linear ? GL_LINEAR : GL_NEAREST;
}

GLenum TextureWrapToGL(TextureWrap wrap) {
    using enum TextureWrap;
    switch (wrap) {
        case Repeat:         return GL_REPEAT;
        case ClampToEdge:    return GL_CLAMP_TO_EDGE;
        case MirroredRepeat: return GL_MIRRORED_REPEAT;
    }
    return GL_REPEAT;
}

} // namespace

OpenGLTexture::OpenGLTexture() = default;

OpenGLTexture::~OpenGLTexture() {
    if (m_textureStorage && m_textureStorage->id)
        glDeleteTextures(1, &m_textureStorage->id);
}

OpenGLTexture::OpenGLTexture(OpenGLTexture&& o) noexcept
    : m_textureStorage(std::move(o.m_textureStorage)),
      m_width(o.m_width),
      m_height(o.m_height),
      m_spec(o.m_spec) {
    o.m_width  = 0;
    o.m_height = 0;
}

OpenGLTexture& OpenGLTexture::operator=(OpenGLTexture&& o) noexcept {
    if (this != &o) {
        if (m_textureStorage && m_textureStorage->id)
            glDeleteTextures(1, &m_textureStorage->id);
        m_textureStorage = std::move(o.m_textureStorage);
        m_width          = o.m_width;
        m_height         = o.m_height;
        m_spec           = o.m_spec;
        o.m_width        = 0;
        o.m_height       = 0;
    }
    return *this;
}

bool OpenGLTexture::IsValid() const {
    return m_textureStorage && m_textureStorage->id != 0;
}

unsigned int OpenGLTexture::GetNativeId() const {
    return m_textureStorage ? m_textureStorage->id : 0;
}

RenderTargetHandle OpenGLTexture::GetRenderTargetHandle(bool needsYFlip) const {
    return RenderTargetHandle::OpenGLTexture(GetNativeId(), needsYFlip);
}

bool OpenGLTexture::operator==(const ITexture& other) const {
    if (const auto* o = dynamic_cast<const OpenGLTexture*>(&other))
        return GetNativeId() == o->GetNativeId();
    return false;
}

OpenGLTexture OpenGLTexture::FromSpec(const TextureSpec& spec) {
    OpenGLTexture t;
    t.m_textureStorage = std::make_unique<TextureStorage>();
    t.m_width          = static_cast<int>(spec.width);
    t.m_height         = static_cast<int>(spec.height);
    t.m_spec           = spec;

    const GLenum internalFmt = TextureFormatToInternalFormat(spec.format);
    const GLenum dataFmt     = TextureFormatToDataFormat(spec.format);
    const GLenum dataType    = TextureFormatToDataType(spec.format);

    glGenTextures(1, &t.m_textureStorage->id);
    glBindTexture(GL_TEXTURE_2D, t.m_textureStorage->id);
    glTexImage2D(GL_TEXTURE_2D, 0,
                 static_cast<GLint>(internalFmt),
                 static_cast<GLsizei>(spec.width),
                 static_cast<GLsizei>(spec.height),
                 0, dataFmt, dataType, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    TextureFilterToGL(spec.filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    TextureFilterToGL(spec.filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, TextureWrapToGL(spec.wrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, TextureWrapToGL(spec.wrap));
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

OpenGLTexture OpenGLTexture::FromFile(const std::string& path, bool flipY) {
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
    int w;
    int h;
    int ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        LogWarn("OpenGLTexture::FromFile — failed to load '{}': {}", path,
                stbi_failure_reason());
        return CreateWhite1x1();
    }

    OpenGLTexture t;
    t.m_textureStorage = std::make_unique<TextureStorage>();
    t.m_width          = w;
    t.m_height         = h;
    t.m_spec           = { static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                           TextureFormat::RGBA8, TextureFilter::Linear,
                           TextureWrap::Repeat, true };

    glGenTextures(1, &t.m_textureStorage->id);
    glBindTexture(GL_TEXTURE_2D, t.m_textureStorage->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return t;
}

OpenGLTexture OpenGLTexture::CreateWhite1x1() {
    OpenGLTexture t;
    t.m_textureStorage = std::make_unique<TextureStorage>();
    t.m_width = t.m_height = 1;
    t.m_spec = { 1, 1, TextureFormat::RGBA8, TextureFilter::Nearest,
                 TextureWrap::Repeat, false };

    glGenTextures(1, &t.m_textureStorage->id);
    glBindTexture(GL_TEXTURE_2D, t.m_textureStorage->id);
    std::array<std::uint8_t, 4> white = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 white.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

void OpenGLTexture::Bind(uint32_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, GetNativeId());
}

void OpenGLTexture::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLTexture::SetData(const void* data, uint32_t /*size*/) { // NOSONAR: void* required by ITexture interface
    if (!m_textureStorage || !m_textureStorage->id)
        return;

    const GLenum dataFmt     = TextureFormatToDataFormat(m_spec.format);
    const GLenum dataType    = TextureFormatToDataType(m_spec.format);

    // Suppress unused-variable warning for internalFmt in non-debug builds
    (void)TextureFormatToInternalFormat(m_spec.format);

    glBindTexture(GL_TEXTURE_2D, m_textureStorage->id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    static_cast<GLsizei>(m_spec.width),
                    static_cast<GLsizei>(m_spec.height),
                    dataFmt, dataType, data);
    if (m_spec.generateMips)
        glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace Horo
