// stb_image implementation — included once, here.
#define STB_IMAGE_IMPLEMENTATION
#include "renderer/Texture.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <array>
#include <cstdint>
#include <memory>

#include "core/Logger.h"

namespace Horo {
    struct Texture::TextureStorage {
        unsigned int id = 0;
    };

    Texture::Texture() = default;

    Texture::~Texture() {
        if (m_textureStorage && m_textureStorage->id)
            glDeleteTextures(1, &m_textureStorage->id);
    }

    Texture::Texture(Texture &&o) noexcept
        : m_textureStorage(std::move(o.m_textureStorage)), m_width(o.m_width),
          m_height(o.m_height) {
    }

    Texture &Texture::operator=(Texture &&o) noexcept {
        if (this != &o) {
            if (m_textureStorage && m_textureStorage->id)
                glDeleteTextures(1, &m_textureStorage->id);
            m_textureStorage = std::move(o.m_textureStorage);
            m_width = o.m_width;
            m_height = o.m_height;
        }
        return *this;
    }

    bool Texture::IsValid() const {
        return m_textureStorage && m_textureStorage->id != 0;
    }

    unsigned int Texture::GetNativeId() const {
        return m_textureStorage ? m_textureStorage->id : 0;
    }

    RenderTargetHandle Texture::GetRenderTargetHandle(bool needsYFlip) const {
        return RenderTargetHandle::OpenGLTexture(GetNativeId(), needsYFlip);
    }

    Texture Texture::FromFile(const std::string &path, bool flipY) {
        stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
        int w;
        int h;
        int ch;
        unsigned char *data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!data) {
            LogWarn("Texture::FromFile — failed to load '{}': {}", path,
                    stbi_failure_reason());
            return CreateWhite1x1();
        }

        Texture t;
        t.m_textureStorage = std::make_unique<TextureStorage>();
        glGenTextures(1, &t.m_textureStorage->id);
        glBindTexture(GL_TEXTURE_2D, t.m_textureStorage->id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        t.m_width = w;
        t.m_height = h;
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);
        return t;
    }

    Texture Texture::CreateWhite1x1() {
        Texture t;
        t.m_textureStorage = std::make_unique<TextureStorage>();
        glGenTextures(1, &t.m_textureStorage->id);
        glBindTexture(GL_TEXTURE_2D, t.m_textureStorage->id);
        std::array<std::uint8_t, 4> white = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     white.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        t.m_width = t.m_height = 1;
        glBindTexture(GL_TEXTURE_2D, 0);
        return t;
    }

    void Texture::Bind(unsigned int slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, GetNativeId());
    }

    void Texture::Unbind() const {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
} // namespace Horo
