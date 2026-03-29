// stb_image implementation — included once, here.
#define STB_IMAGE_IMPLEMENTATION
#include "renderer/Texture.h"

#include <glad/glad.h>
#include <stb_image.h>

#include "core/Logger.h"

namespace Monolith {

Texture::~Texture() {
  if (m_id)
    glDeleteTextures(1, &m_id);
}

Texture::Texture(Texture&& o) noexcept : m_id(o.m_id), m_width(o.m_width), m_height(o.m_height) {
  o.m_id = 0;
}

Texture& Texture::operator=(Texture&& o) noexcept {
  if (this != &o) {
    if (m_id)
      glDeleteTextures(1, &m_id);
    m_id = o.m_id;
    m_width = o.m_width;
    m_height = o.m_height;
    o.m_id = 0;
  }
  return *this;
}

Texture Texture::FromFile(const std::string& path, bool flipY) {
  stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
  int w, h, ch;
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
  if (!data) {
    LOG_WARN("Texture::FromFile — failed to load '%s': %s", path.c_str(), stbi_failure_reason());
    return CreateWhite1x1();
  }

  Texture t;
  glGenTextures(1, &t.m_id);
  glBindTexture(GL_TEXTURE_2D, t.m_id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
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
  glGenTextures(1, &t.m_id);
  glBindTexture(GL_TEXTURE_2D, t.m_id);
  unsigned char white[4] = {255, 255, 255, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  t.m_width = t.m_height = 1;
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

void Texture::Bind(unsigned int slot) const {
  glActiveTexture(GL_TEXTURE0 + slot);
  glBindTexture(GL_TEXTURE_2D, m_id);
}

void Texture::Unbind() const {
  glBindTexture(GL_TEXTURE_2D, 0);
}

}  // namespace Monolith
