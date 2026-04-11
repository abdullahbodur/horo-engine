#include "core/Screenshot.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include "core/Logger.h"
#include "renderer/Renderer.h"

namespace Monolith {

// Write a 24-bit BMP from bottom-up BGR pixel data (matches glReadPixels GL_BGR output).
static bool WriteBMP(const std::string& path, int w, int h, const std::vector<uint8_t>& pixels) {
  // BMP rows must be padded to a multiple of 4 bytes
  const int rowStride = w * 3;
  const int rowPadded = (rowStride + 3) & ~3;
  const int dataSize = rowPadded * h;
  const int fileSize = 14 + 40 + dataSize;

  FILE* f = nullptr;
#ifdef _WIN32
  fopen_s(&f, path.c_str(), "wb");
#else
  f = std::fopen(path.c_str(), "wb");
#endif
  if (!f)
    return false;

  // ---- File header (14 bytes) ----
  uint8_t fh[14] = {};
  fh[0] = 'B';
  fh[1] = 'M';
  fh[2] = static_cast<uint8_t>(fileSize);
  fh[3] = static_cast<uint8_t>(fileSize >> 8);
  fh[4] = static_cast<uint8_t>(fileSize >> 16);
  fh[5] = static_cast<uint8_t>(fileSize >> 24);
  fh[10] = 54;  // pixel data offset = 14 + 40
  std::fwrite(fh, 1, 14, f);

  // ---- DIB header (BITMAPINFOHEADER, 40 bytes) ----
  uint8_t dh[40] = {};
  dh[0] = 40;  // biSize
  dh[4] = static_cast<uint8_t>(w);
  dh[5] = static_cast<uint8_t>(w >> 8);
  dh[6] = static_cast<uint8_t>(w >> 16);
  dh[7] = static_cast<uint8_t>(w >> 24);
  dh[8] = static_cast<uint8_t>(h);
  dh[9] = static_cast<uint8_t>(h >> 8);
  dh[10] = static_cast<uint8_t>(h >> 16);
  dh[11] = static_cast<uint8_t>(h >> 24);
  dh[12] = 1;   // biPlanes
  dh[14] = 24;  // biBitCount (24-bit BGR)
  dh[20] = static_cast<uint8_t>(dataSize);
  dh[21] = static_cast<uint8_t>(dataSize >> 8);
  dh[22] = static_cast<uint8_t>(dataSize >> 16);
  dh[23] = static_cast<uint8_t>(dataSize >> 24);
  std::fwrite(dh, 1, 40, f);

  // ---- Pixel data (rows already bottom-up from glReadPixels) ----
  std::vector<uint8_t> row(static_cast<size_t>(rowPadded), 0);
  for (int y = 0; y < h; ++y) {
    std::memcpy(row.data(), pixels.data() + y * rowStride, static_cast<size_t>(rowStride));
    std::fwrite(row.data(), 1, static_cast<size_t>(rowPadded), f);
  }

  std::fclose(f);
  return true;
}

std::string Screenshot::Save(int w, int h, const std::string& folder) {
  if (w <= 0 || h <= 0)
    return {};

  if (!Renderer::GetBackendCapabilities().supportsReadback) {
    LOG_WARN("Screenshot: active render backend does not support readback capture.");
    return {};
  }

  std::vector<uint8_t> pixels;
  std::string readbackError;
  if (!Renderer::ReadbackColorBgr8(w, h, pixels, &readbackError)) {
    LOG_ERROR("Screenshot: color readback failed (%s)", readbackError.c_str());
    return {};
  }

  // Build timestamped filename
  std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);

  std::string path = folder + "/screenshot_" + stamp + ".bmp";

  if (!WriteBMP(path, w, h, pixels)) {
    LOG_ERROR("Screenshot: failed to write '%s'", path.c_str());
    return {};
  }

  LOG_INFO("Screenshot saved: %s", path.c_str());
  return path;
}

}  // namespace Monolith
