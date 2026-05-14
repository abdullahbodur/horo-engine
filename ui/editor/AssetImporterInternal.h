/** @file AssetImporterInternal.h
 *  @brief Pure helper functions shared between AssetImporterRegistry and its tests.
 *  @note Must not be included by any public header. */
#pragma once

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor::ImporterDetail {

/** @brief Detects the file format suffix from a small image byte signature.
 *  @return Lowercase suffix including the leading dot, or empty when nothing matches.
 */
inline std::string SniffImageExtension(const std::vector<unsigned char> &bytes) {
    const auto starts = [&](std::initializer_list<unsigned char> magic) {
        if (bytes.size() < magic.size())
            return false;
        std::size_t i = 0;
        for (unsigned char b : magic) {
            if (bytes[i++] != b)
                return false;
        }
        return true;
    };
    if (starts({0x89, 0x50, 0x4E, 0x47}))
        return ".png";
    if (starts({0xFF, 0xD8, 0xFF}))
        return ".jpg";
    if (starts({0x42, 0x4D}))
        return ".bmp";
    if (bytes.size() > 12 && bytes[0] == 0x52 && bytes[1] == 0x49 &&
        bytes[2] == 0x46 && bytes[3] == 0x46 && bytes[8] == 0x57 &&
        bytes[9] == 0x45 && bytes[10] == 0x42 && bytes[11] == 0x50)
        return ".webp";
    if (bytes.size() > 11 && bytes[0] == '#' && bytes[1] == '?' &&
        bytes[2] == 'R' && bytes[3] == 'A')
        return ".hdr";
    return {};
}

/** @brief Returns @p baseName with its extension replaced by @p ext, or with
 *         @p ext appended when @p baseName has no extension.
 */
inline std::string EnsureExtension(std::string baseName, const std::string &ext) {
    if (ext.empty())
        return baseName;
    namespace fs = std::filesystem;
    fs::path p(baseName);
    if (p.extension().empty())
        return baseName + ext;
    p.replace_extension(ext);
    return p.string();
}

/** @brief True when @p filename is safe as a leaf inside the managed asset directory. */
inline bool IsSafeBasename(std::string_view filename) {
    if (filename.empty() || filename == "." || filename == "..")
        return false;
    for (char ch : filename) {
        if (ch == '/' || ch == '\\')
            return false;
    }
    return true;
}

/** @brief Sanitises an FBX-derived filename hint into a single-segment basename. */
inline std::string SanitiseTextureBasename(std::string_view raw) {
    if (raw.empty())
        return std::string{"texture.png"};
    const std::filesystem::path candidate(raw);
    std::string base = candidate.filename().string();
    for (char &ch : base) {
        if (ch == '/' || ch == '\\' || ch == ':')
            ch = '_';
    }
    if (base.empty() || base == "." || base == "..")
        return std::string{"texture.png"};
    return base;
}

} // namespace Horo::Editor::ImporterDetail
