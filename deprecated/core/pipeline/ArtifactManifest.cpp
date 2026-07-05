/** @file ArtifactManifest.cpp
 *  @brief Artifact manifest generation and serialisation implementation.
 */
#include "core/pipeline/ArtifactManifest.h"

#include "core/archive/HashVerifier.h"
#include "core/BuildVersion.h"
#include "core/pipeline/ReleaseDraft.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <format>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

// Detect compiler identity.
#if defined(_MSC_VER)
#  define HORO_COMPILER_STRING "MSVC " HORO_STRINGIFY(_MSC_VER)
#elif defined(__clang_version__)
#  define HORO_COMPILER_STRING "Clang " __clang_version__
#elif defined(__GNUC__)
#  define HORO_COMPILER_STRING "GCC " HORO_STRINGIFY(__GNUC__) "." HORO_STRINGIFY(__GNUC_MINOR__)
#else
#  define HORO_COMPILER_STRING "Unknown"
#endif

#define HORO_STRINGIFY_IMPL(x) #x
#define HORO_STRINGIFY(x) HORO_STRINGIFY_IMPL(x)

namespace Horo::Build {
namespace {

/** @brief Reads the entire contents of a file into a string.
 *  @return File contents, or empty string on failure. */
std::string ReadFileToString(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good())
        return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

/** @brief Converts a byte array to a lowercase hex string. */
std::string BytesToHex(const uint8_t *data, size_t size) {
    static constexpr const char kHex[] = "0123456789abcdef";
    std::string out(size * 2, '\0');
    for (size_t i = 0; i < size; ++i) {
        out[i * 2]     = kHex[data[i] >> 4];
        out[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

/** @brief Quotes a shell argument for the host shell. */
std::string ShellQuote(const std::string &value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"')
            quoted += "\\\"";
        else
            quoted += c;
    }
    quoted += '"';
    return quoted;
#else
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += '\'';
    return quoted;
#endif
}

/** @brief Runs `git rev-parse HEAD` from a directory and returns the result.
 *  @return 40-char hex SHA, or empty string on failure. */
std::string RunGitRevParse(const std::filesystem::path &repoPath) {
    const auto dotGit = repoPath / ".git";
    if (std::error_code ec; !std::filesystem::exists(dotGit, ec))
        return {};

    // Avoid writing into the process-global temp directory: manifest generation
    // may run concurrently from multiple builds, and /tmp is commonly writable
    // by other users. Read git's stdout directly instead.
    const std::string cmd = std::format("git -C {} rev-parse HEAD 2>/dev/null",
                                        ShellQuote(repoPath.string()));
#ifdef _WIN32
    FILE *pipe = _popen(cmd.c_str(), "r");
#else
    FILE *pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe)
        return {};

    std::string sha;
    std::array<char, 128> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        sha += buffer.data();

#ifdef _WIN32
    const int rc = _pclose(pipe);
#else
    const int rc = pclose(pipe);
#endif
    if (rc != 0)
        return {};

    // Trim trailing whitespace/newlines.
    while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r'))
        sha.pop_back();

    // Validate: must be exactly 40 lowercase hex chars.
    if (sha.size() != 40)
        return {};
    for (char c : sha) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return {};
    }
    return sha;
}

/** @brief Reads an environment variable. */
std::string ReadEnv(const char *name) {
#if defined(_WIN32)
    char *value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
        return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    if (const char *value = std::getenv(name))
        return value;
    return {};
#endif
}

/** @brief Serialises manifest contents info to JSON. */
nlohmann::json ContentsToJson(const ManifestContentsInfo &c) {
    nlohmann::json j;
    j["asset_count"] = c.assetCount;
    j["archive_path"] = c.archivePath;
    j["archive_size_bytes"] = c.archiveSizeBytes;
    j["archive_sha256"] = c.archiveSha256;
    return j;
}

/** @brief Parses manifest contents info from JSON. */
bool ContentsFromJson(const nlohmann::json &j, ManifestContentsInfo &c) {
    if (!j.is_object())
        return false;
    c.assetCount = j.value("asset_count", 0U);
    c.archivePath = j.value("archive_path", std::string{});
    c.archiveSizeBytes = j.value("archive_size_bytes", 0ULL);
    c.archiveSha256 = j.value("archive_sha256", std::string{});
    return true;
}

/** @brief Serialises manifest artifact info to JSON. */
nlohmann::json ArtifactToJson(const ManifestArtifactInfo &a) {
    nlohmann::json j;
    j["name"] = a.name;
    j["version"] = a.version;
    j["build_number"] = a.buildNumber;
    j["release_channel"] = a.releaseChannel;
    return j;
}

/** @brief Parses manifest artifact info from JSON. */
bool ArtifactFromJson(const nlohmann::json &j, ManifestArtifactInfo &a) {
    if (!j.is_object())
        return false;
    a.name = j.value("name", std::string{});
    a.version = j.value("version", std::string{});
    a.buildNumber = j.value("build_number", std::string{});
    a.releaseChannel = j.value("release_channel", std::string{});
    return true;
}

/** @brief Serialises manifest build info to JSON. */
nlohmann::json BuildToJson(const ManifestBuildInfo &b) {
    nlohmann::json j;
    j["git_sha"] = b.gitSha;
    j["platform"] = b.platform;
    j["arch"] = b.arch;
    j["compiler"] = b.compiler;
    j["config"] = b.config;
    j["timestamp"] = b.timestamp;
    return j;
}

/** @brief Parses manifest build info from JSON. */
bool BuildFromJson(const nlohmann::json &j, ManifestBuildInfo &b) {
    if (!j.is_object())
        return false;
    b.gitSha = j.value("git_sha", std::string{});
    b.platform = j.value("platform", std::string{});
    b.arch = j.value("arch", std::string{});
    b.compiler = j.value("compiler", std::string{});
    b.config = j.value("config", std::string{});
    b.timestamp = j.value("timestamp", std::string{});
    return true;
}

} // namespace

// ============================================================================
// Detection
// ============================================================================

/** @copydoc DetectPlatform */
std::string DetectPlatform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

/** @copydoc DetectArchitecture */
std::string DetectArchitecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "x86_64";
#endif
}

/** @copydoc DetectCompiler */
std::string DetectCompiler() {
    return HORO_COMPILER_STRING;
}

/** @copydoc ResolveGitSha */
std::string ResolveGitSha() {
    // 1. Environment variable override.
    const std::string envSha = ReadEnv("HORO_GIT_SHA");
    if (!envSha.empty() && envSha.size() == 40)
        return envSha;

    // 2. Try current working directory.
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        std::string sha = RunGitRevParse(cwd);
        if (!sha.empty())
            return sha;
    }

    return "unknown";
}

/** @copydoc ResolveGitSha */
std::string ResolveGitSha(const std::filesystem::path &repoPath) {
    const std::string envSha = ReadEnv("HORO_GIT_SHA");
    if (!envSha.empty() && envSha.size() == 40)
        return envSha;

    std::string sha = RunGitRevParse(repoPath);
    return sha.empty() ? "unknown" : sha;
}

/** @copydoc ComputeSha256Hex */
std::string ComputeSha256Hex(const std::filesystem::path &filePath) {
    uint8_t digest[Horo::Archive::kSha256Size];
    if (!Horo::Archive::ComputeFileSHA256(filePath, digest))
        return {};
    return BytesToHex(digest, Horo::Archive::kSha256Size);
}

// ============================================================================
// Serialisation
// ============================================================================

/** @copydoc SerializeManifest */
std::string SerializeManifest(const ArtifactManifest &manifest) {
    nlohmann::json root;
    root["manifest_version"] = manifest.manifestVersion;
    root["engine_version"] = manifest.engineVersion;
    root["artifact"] = ArtifactToJson(manifest.artifact);
    root["build"] = BuildToJson(manifest.build);
    root["contents"] = ContentsToJson(manifest.contents);
    return root.dump(2) + "\n";
}

/** @copydoc DeserializeManifest */
bool DeserializeManifest(std::string_view json, ArtifactManifest &outManifest) {
    try {
        const nlohmann::json root = nlohmann::json::parse(json);
        if (!root.is_object())
            return false;

        outManifest.manifestVersion = root.value("manifest_version", "1");
        outManifest.engineVersion = root.value("engine_version", std::string{});

        if (const auto it = root.find("artifact");
            it != root.end() && !ArtifactFromJson(*it, outManifest.artifact))
            return false;

        if (const auto it = root.find("build");
            it != root.end() && !BuildFromJson(*it, outManifest.build))
            return false;

        if (const auto it = root.find("contents");
            it != root.end() && !ContentsFromJson(*it, outManifest.contents))
            return false;

        return true;
    } catch (const nlohmann::json::exception &) {
        return false;
    }
}

/** @copydoc WriteManifestFile */
bool WriteManifestFile(const std::filesystem::path &path,
                       const ArtifactManifest &manifest) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.good())
        return false;
    out << SerializeManifest(manifest);
    return out.good();
}

/** @copydoc ReadManifestFile */
bool ReadManifestFile(const std::filesystem::path &path,
                      ArtifactManifest &outManifest) {
    const std::string json = ReadFileToString(path);
    if (json.empty())
        return false;
    return DeserializeManifest(json, outManifest);
}

/** @copydoc GenerateManifest */
ArtifactManifest GenerateManifest(const ManifestArtifactInfo &artifact,
                                  std::string_view config,
                                  const ManifestContentsInfo &contents,
                                  std::string_view targetOS,
                                  std::string_view targetArch) {
    ArtifactManifest m;
    m.manifestVersion = ArtifactManifest::kManifestVersion;
    m.engineVersion = EngineVersion();
    m.artifact = artifact;
    m.build.gitSha = ResolveGitSha();
    m.build.platform = std::string(targetOS);
    m.build.arch = std::string(targetArch);
    m.build.compiler = DetectCompiler();
    m.build.config = std::string(config);
    m.build.timestamp = CurrentTimestamp();
    m.contents = contents;
    return m;
}

} // namespace Horo::Build
