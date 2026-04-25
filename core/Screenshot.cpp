#include "core/Screenshot.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

#include "core/Logger.h"
#include "renderer/Renderer.h"

namespace Monolith {
// Write a 24-bit BMP from bottom-up BGR pixel data (matches glReadPixels GL_BGR
// output).
static bool WriteBMP(const std::string &path, int w, int h,
                     const std::vector<uint8_t> &pixels) {
  // BMP rows must be padded to a multiple of 4 bytes
  const int rowStride = w * 3;
  const int rowPadded = (rowStride + 3) & ~3;
  const int dataSize = rowPadded * h;
  const int fileSize = 14 + 40 + dataSize;

  FILE *f = nullptr;
#ifdef _WIN32
  fopen_s(&f, path.c_str(), "wb");
#else
  f = std::fopen(path.c_str(), "wb");
#endif
  if (!f)
    return false;

  // ---- File header (14 bytes) ----
  std::array<uint8_t, 14> fh{};
  fh[0] = 'B';
  fh[1] = 'M';
  fh[2] = static_cast<uint8_t>(fileSize);
  fh[3] = static_cast<uint8_t>(fileSize >> 8);
  fh[4] = static_cast<uint8_t>(fileSize >> 16);
  fh[5] = static_cast<uint8_t>(fileSize >> 24);
  fh[10] = 54; // pixel data offset = 14 + 40
  std::fwrite(fh.data(), 1, fh.size(), f);

  // ---- DIB header (BITMAPINFOHEADER, 40 bytes) ----
  std::array<uint8_t, 40> dh{};
  dh[0] = 40; // biSize
  dh[4] = static_cast<uint8_t>(w);
  dh[5] = static_cast<uint8_t>(w >> 8);
  dh[6] = static_cast<uint8_t>(w >> 16);
  dh[7] = static_cast<uint8_t>(w >> 24);
  dh[8] = static_cast<uint8_t>(h);
  dh[9] = static_cast<uint8_t>(h >> 8);
  dh[10] = static_cast<uint8_t>(h >> 16);
  dh[11] = static_cast<uint8_t>(h >> 24);
  dh[12] = 1;  // biPlanes
  dh[14] = 24; // biBitCount (24-bit BGR)
  dh[20] = static_cast<uint8_t>(dataSize);
  dh[21] = static_cast<uint8_t>(dataSize >> 8);
  dh[22] = static_cast<uint8_t>(dataSize >> 16);
  dh[23] = static_cast<uint8_t>(dataSize >> 24);
  std::fwrite(dh.data(), 1, dh.size(), f);

  // ---- Pixel data (rows already bottom-up from glReadPixels) ----
  std::vector<uint8_t> row(static_cast<size_t>(rowPadded), 0);
  for (int y = 0; y < h; ++y) {
    std::memcpy(row.data(), pixels.data() + y * rowStride,
                static_cast<size_t>(rowStride));
    std::fwrite(row.data(), 1, static_cast<size_t>(rowPadded), f);
  }

  std::fclose(f);
  return true;
}

static std::string BuildTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
  std::tm localTime{};
#ifdef _WIN32
  localtime_s(&localTime, &nowTime);
#else
  localtime_r(&nowTime, &localTime);
#endif

  std::ostringstream stream;
  stream << std::put_time(&localTime, "%Y%m%d_%H%M%S");
  return stream.str();
}

std::string Screenshot::Save(int w, int h, const std::string &folder) {
  if (w <= 0 || h <= 0)
    return {};

  if (!Renderer::GetBackendCapabilities().supportsReadback) {
    LogWarn(
        "Screenshot: active render backend does not support readback capture.");
    return {};
  }

  std::vector<uint8_t> pixels;
  if (std::string readbackError;
      !Renderer::ReadbackColorBgr8(w, h, pixels, &readbackError)) {
    LogError("Screenshot: color readback failed ({})", readbackError);
    return {};
  }

  const std::string path = folder + "/screenshot_" + BuildTimestamp() + ".bmp";

  if (!WriteBMP(path, w, h, pixels)) {
    LogError("Screenshot: failed to write '{}'", path);
    return {};
  }

  LogInfo("Screenshot saved: {}", path);
  return path;
}
} // namespace Monolith
