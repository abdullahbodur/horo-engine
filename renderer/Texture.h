#pragma once
#include <memory>
#include <string>

namespace Monolith {

class Texture {
 public:
  Texture();
  ~Texture();

  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&& o) noexcept;
  Texture& operator=(Texture&& o) noexcept;

  static Texture FromFile(const std::string& path, bool flipY = true);
  static Texture CreateWhite1x1();

  void Bind(unsigned int slot = 0) const;
  void Unbind() const;

  bool IsValid() const;
  int GetWidth() const { return m_width; }
  int GetHeight() const { return m_height; }
  // Temporary OpenGL escape hatch used by editor integration until offscreen/ImGui
  // texture presentation is fully backend-neutral.
  unsigned int GetNativeId() const;

 private:
  struct TextureStorage;
  std::unique_ptr<TextureStorage> m_textureStorage;
  int m_width = 0;
  int m_height = 0;
};

}  // namespace Monolith
