#pragma once
#include <string>

namespace Horo {

class Texture {
 public:
  Texture() = default;
  ~Texture();

  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&& o) noexcept;
  Texture& operator=(Texture&& o) noexcept;

  static Texture FromFile(const std::string& path, bool flipY = true);
  static Texture CreateWhite1x1();

  void Bind(unsigned int slot = 0) const;
  void Unbind() const;

  bool IsValid() const { return m_id != 0; }
  int GetWidth() const { return m_width; }
  int GetHeight() const { return m_height; }

 private:
  unsigned int m_id = 0;
  int m_width = 0;
  int m_height = 0;
};

}  // namespace Horo
