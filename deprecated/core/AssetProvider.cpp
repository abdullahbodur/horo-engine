/**
 * @file AssetProvider.cpp
 * @brief Runtime asset byte loading from filesystem or .horo release archives.
 */
#include "core/AssetProvider.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/ProjectPath.h"
#include "core/archive/Packager.h"
#include "core/crypto/AESContext.h"

namespace Horo {
namespace {

/** @brief Reads an entire regular file into memory. */
std::optional<std::vector<uint8_t>> ReadFileBytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open())
        return std::nullopt;

    const std::streamoff size = in.tellg();
    if (size < 0)
        return std::nullopt;
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.seekg(0, std::ios::beg);
    if (!data.empty())
        in.read(reinterpret_cast<char *>(data.data()),
                static_cast<std::streamsize>(data.size()));
    if (!in.good() && !in.eof())
        return std::nullopt;
    return data;
}

/** @brief Derives the archive key from HORO_RELEASE_ARCHIVE_PASSWORD. */
bool ConfigureArchivePassword(Archive::Packager *archive) {
    const char *password = std::getenv("HORO_RELEASE_ARCHIVE_PASSWORD");
    if (!password || password[0] == '\0')
        return false;

    constexpr const char *kEngineSalt = "horo-engine-horopak-v1";
    constexpr uint32_t kIterations = 100'000;
    std::array<uint8_t, 32> key{};
    if (!Crypto::DeriveKeyPbkdf2(
            password, std::char_traits<char>::length(password),
            reinterpret_cast<const uint8_t *>(kEngineSalt),
            std::char_traits<char>::length(kEngineSalt), kIterations,
            key.data(), key.size()))
        return false;

    archive->SetEncryptionEnabled(true);
    archive->SetEncryptionKey(key);
    return true;
}

/** @brief Candidate archive paths for a packaged release. */
std::vector<std::filesystem::path> ArchiveCandidates() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path root = ProjectPath::Root();

    std::vector<std::filesystem::path> candidates;
    const auto addCandidate = [&candidates](std::filesystem::path path) {
        if (path.empty())
            return;
        path = path.lexically_normal();
        if (std::find(candidates.begin(), candidates.end(), path) == candidates.end())
            candidates.push_back(std::move(path));
    };

    addCandidate(cwd / "assets.horo");
    addCandidate(cwd / ".." / "Resources" / "assets.horo");
    addCandidate(root / "assets.horo");
    addCandidate(root / ".." / "Resources" / "assets.horo");
    return candidates;
}

/** @brief Returns archive-local lookup names for a logical project asset path. */
std::vector<std::string> ArchiveLookupNames(std::string_view logicalPath) {
    std::vector<std::string> names;
    const auto addName = [&names](std::string name) {
        if (name.empty())
            return;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(std::move(name));
    };

    std::string path{logicalPath};
    std::replace(path.begin(), path.end(), '\\', '/');
    addName(path);

    constexpr std::string_view kAssetsPrefix = "assets/";
    if (path.starts_with(kAssetsPrefix))
        addName(path.substr(kAssetsPrefix.size()));

    // Packaged builds can still carry source-authored absolute scene paths such
    // as `/repo/assets/scenes/level.json`, while the release archive is packed
    // from the copied `assets/` directory and stores `scenes/level.json`.  Do
    // not require the original source root to exist at runtime; strip the first
    // assets segment and try both archive naming conventions.
    constexpr std::string_view kAssetsSegment = "/assets/";
    if (const size_t assetsPos = path.find(kAssetsSegment);
        assetsPos != std::string::npos) {
        const std::string assetRelative = path.substr(assetsPos + 1);
        addName(assetRelative);
        if (assetRelative.starts_with(kAssetsPrefix))
            addName(assetRelative.substr(kAssetsPrefix.size()));
    }

    return names;
}

/** @brief Writes an error string when the caller requested diagnostics. */
void SetAssetError(std::string *outError, std::string message) {
    if (outError)
        *outError = std::move(message);
}

/** @brief Attempts to read a project-resolved asset from the filesystem. */
std::optional<std::vector<uint8_t>> TryReadFilesystemAsset(std::string_view logicalPath) {
    const std::filesystem::path filePath =
        ProjectPath::Resolve(std::string(logicalPath));
    if (std::error_code ec; !std::filesystem::is_regular_file(filePath, ec) || ec)
        return std::nullopt;
    return ReadFileBytes(filePath);
}

/** @brief Attempts to read a logical asset from one candidate archive. */
std::optional<std::vector<uint8_t>> TryReadArchiveAsset(
    const std::filesystem::path &archivePath, std::string_view logicalPath,
    std::string *outError) {
    Horo::Archive::Packager archive;
    ConfigureArchivePassword(&archive);
    const Horo::Archive::PackResult openResult =
        archive.Open(archivePath.generic_string());
    if (openResult != Horo::Archive::PackResult::Ok) {
        SetAssetError(outError, "Failed to open asset archive '" +
                                    archivePath.string() + "'.");
        return std::nullopt;
    }

    for (const std::string &archiveName: ArchiveLookupNames(logicalPath)) {
        std::vector<uint8_t> data;
        const Horo::Archive::PackResult extractResult =
            archive.Extract(archiveName, data);
        if (extractResult == Horo::Archive::PackResult::Ok)
            return data;
    }

    SetAssetError(outError, "Asset not found in archive '" +
                                archivePath.string() + "': " +
                                std::string(logicalPath));
    return std::nullopt;
}

/** @brief Attempts to read a logical asset from all known archive locations. */
std::optional<std::vector<uint8_t>> TryReadArchiveCandidates(
    std::string_view logicalPath, std::string *outError) {
    for (const std::filesystem::path &archivePath: ArchiveCandidates()) {
        if (archivePath.empty())
            continue;
        if (std::error_code ec;
            !std::filesystem::is_regular_file(archivePath, ec) || ec)
            continue;
        if (auto data = TryReadArchiveAsset(archivePath, logicalPath, outError))
            return data;
    }
    return std::nullopt;
}

} // namespace

/** @copydoc ReadAssetBytes */
std::optional<std::vector<uint8_t>> ReadAssetBytes(std::string_view logicalPath,
                                                   std::string *outError) {
    if (outError)
        outError->clear();
    if (logicalPath.empty()) {
        SetAssetError(outError, "Asset path is empty.");
        return std::nullopt;
    }

    if (auto data = TryReadFilesystemAsset(logicalPath))
        return data;
    if (auto data = TryReadArchiveCandidates(logicalPath, outError))
        return data;

    if (outError && outError->empty())
        SetAssetError(outError, "Asset not found on disk or in assets.horo: " +
                                    std::string(logicalPath));
    return std::nullopt;
}

/** @copydoc ReadAssetText */
std::optional<std::string> ReadAssetText(std::string_view logicalPath,
                                         std::string *outError) {
    auto bytes = ReadAssetBytes(logicalPath, outError);
    if (!bytes)
        return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

} // namespace Horo
