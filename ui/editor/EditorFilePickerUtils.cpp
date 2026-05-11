/**
 * @file EditorFilePickerUtils.cpp
 * @brief Native file-picker implementations for OBJ and texture imports.
 *
 * Windows uses @c GetOpenFileNameW with UTF-8 conversion. macOS shells out to
 * @c osascript choose file (OBJ filters are validated after pick). Other hosts
 * return empty paths until a picker is wired.
 */
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

#include <cstdio>
#include <string>

namespace Horo::Editor {
#if defined(_WIN32)
/** @brief Converts a null-terminated UTF-16 path from the common-dialog API into UTF-8.
 *  @param path Wide-character path buffer from @c OPENFILENAMEW.
 *  @return UTF-8 string, or empty when conversion fails.
 */
static std::string WidePathToUtf8(const wchar_t *path) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0)
    return {};

  std::string utf8(static_cast<size_t>(size - 1), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), size, nullptr,
                          nullptr) <= 0)
    return {};
  return utf8;
}
#endif

#if defined(__APPLE__)
// SONAR-OFF
/** @brief Runs @p cmd with @c popen and returns trimmed line-oriented stdout.
 *  @param cmd Shell command line (typically @c osascript … choose file …).
 *  @return POSIX path string, or empty when the pipe fails or user cancels.
 */
static std::string ReadPathFromOsascript(const char *cmd) {
  FILE *pipe = popen(cmd, "r");
  if (!pipe)
    return {};
  std::string buf(1024, '\0');
  std::string out;
  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
    out += buf.data();
  pclose(pipe);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
    out.pop_back();
  return out;
}
// SONAR-ON
#endif

// SONAR-OFF
/** @copydoc PickObjFilePath */
std::string PickObjFilePath() {
#if defined(_WIN32)
  wchar_t filePath[MAX_PATH] = {}; // NOSONAR: cpp:S3003 Windows OPENFILENAMEW
  // requires a wchar_t[] output buffer
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = L"OBJ Files\0*.obj\0All Files\0*.*\0";
  ofn.lpstrFile = filePath;
  ofn.nMaxFile = sizeof(filePath) / sizeof(filePath[0]);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&ofn))
    return WidePathToUtf8(filePath);
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
/** @copydoc PickTextureFilePath */
std::string PickTextureFilePath() {
#ifdef _WIN32
  wchar_t filePath[MAX_PATH] = {}; // NOSONAR: cpp:S3003 Windows OPENFILENAMEW
  // requires a wchar_t[] output buffer
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.webp\0"
                    L"PNG\0*.png\0"
                    L"JPEG\0*.jpg;*.jpeg\0"
                    L"All Files\0*.*\0";
  ofn.lpstrFile = filePath;
  ofn.nMaxFile = sizeof(filePath) / sizeof(filePath[0]);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&ofn))
    return WidePathToUtf8(filePath);
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
