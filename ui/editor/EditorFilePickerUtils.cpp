#include "ui/editor/EditorFilePickerUtils.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on
#endif

#include <algorithm>
#include <cstdio>
#include <string>

namespace Horo::Editor {
namespace {
#if defined(__APPLE__)
// SONAR-OFF
std::string ReadPathFromOsascript(const char *cmd) {
  FILE *pipe = popen(cmd, "r");
  if (!pipe)
    return {};
  std::string buf(1024, '\0');
  std::string out;
  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
    out += buf.data();
  pclose(pipe);
  std::erase_if(out, [](char c) { return c == '\n' || c == '\r'; });
  return out;
}
// SONAR-ON
#endif
} // namespace

// SONAR-OFF
std::string PickObjFilePath() {
#if defined(_WIN32)
  char filePath[MAX_PATH] = {}; // NOSONAR: cpp:S3003 Windows OPENFILENAMEA
  // requires a char[] output buffer
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "OBJ Files\0*.obj\0All Files\0*.*\0";
  ofn.lpstrFile = filePath;
  ofn.nMaxFile = sizeof(filePath);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn))
    return filePath;
  return {};
#elif defined(__APPLE__)
  // Avoid `of type {"obj"}` — it often fails on modern macOS; we validate
  // extension in code.
  return ReadPathFromOsascript(
      R"(/usr/bin/osascript -e 'try' -e 'POSIX path of (choose file with prompt "Select OBJ file")' -e 'on error' -e 'return ""' -e 'end try' 2>/dev/null)");
#else
  return {};
#endif
}
// SONAR-ON

// SONAR-OFF
std::string PickTextureFilePath() {
#ifdef _WIN32
  char filePath[MAX_PATH] = {}; // NOSONAR: cpp:S3003 Windows OPENFILENAMEA
  // requires a char[] output buffer
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.webp\0"
                    "PNG\0*.png\0"
                    "JPEG\0*.jpg;*.jpeg\0"
                    "All Files\0*.*\0";
  ofn.lpstrFile = filePath;
  ofn.nMaxFile = sizeof(filePath);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn))
    return filePath;
  return {};
#elif defined(__APPLE__)
  return ReadPathFromOsascript(
      R"(/usr/bin/osascript -e 'try' -e 'POSIX path of (choose file with prompt "Select texture image")' -e 'on error' -e 'return ""' -e 'end try' 2>/dev/null)");
#else
  return {};
#endif
}
// SONAR-ON

} // namespace Horo::Editor
