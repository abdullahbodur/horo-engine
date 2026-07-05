/** @file horopak_main.cpp
 *  @brief Entry point for the horopak CLI archive tool.
 *
 *  Provides subcommand-based .horo archive manipulation:
 *  - pack:   Bundle a project directory into a .horo archive.
 *  - unpack: Extract a .horo archive to a directory.
 *  - list:   Print the table of contents of a .horo archive.
 *  - info:   Print archive header metadata.
 *  - hash:   Compute streaming SHA-256 checksum of any file.
 *
 *  Dependencies: core/archive/Packager.h, core/archive/HashVerifier.h,
 *                core/crypto/AESContext.h
 *  No editor, renderer, or window-system linkage required.
 */

#include "core/archive/HoroFormat.h"
#include "core/archive/HashVerifier.h"
#include "core/archive/Packager.h"
#include "core/crypto/AESContext.h"

#include "core/BuildVersion.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ── CLI argument parsing ─────────────────────────────────────────────────

/** @brief Parsed subcommand. */
enum class Subcommand {
    None,
    Pack,
    Unpack,
    List,
    Info,
    Hash,
    Manifest,
};

/** @brief Parsed CLI arguments for the horopak tool. */
struct CliArgs {
    Subcommand subcommand = Subcommand::None;
    std::string projectRoot;
    std::string output;
    std::string input;
    std::string outputDir;
    std::string password;
    int compressionLevel = 1;
    bool generateChecksum = false;
    bool showHelp = false;
    bool showVersion = false;
    bool parseError = false;
};

/** @brief Derive a 32-byte AES-256 key from a password using PBKDF2.
 *
 *  Uses a fixed salt (engine identity) and 100,000 iterations.
 *  The salt is derived from a static engine tag to ensure deterministic
 *  key derivation: same password always yields the same key for horopak.
 *
 *  @param password  UTF-8 password string.
 *  @param outKey    Output buffer of 32 bytes.
 *  @return True on success. */
bool DeriveKeyFromPassword(const std::string& password,
                           std::array<uint8_t, 32>& outKey) {
    if (password.empty()) {
        return false;
    }

    // Fixed salt derived from engine identity tag.
    // This is intentionally deterministic — horopak archives use a stable
    // key derivation so that the same password always decrypts the archive.
    // Production deployments should use a random per-archive salt stored
    // in the archive header (Phase 2+).
    constexpr const char* kEngineSalt = "horo-engine-horopak-v1";
    constexpr uint32_t kIterations = 100'000;

    return Horo::Crypto::DeriveKeyPbkdf2(
        password.data(), password.size(),
        reinterpret_cast<const uint8_t*>(kEngineSalt), std::strlen(kEngineSalt),
        kIterations,
        outKey.data(), outKey.size());
}

/** @brief Print usage information to stdout. */
void PrintUsage(const char* programName) {
    std::cout
        << "horopak — Horo Engine archive packer/unpacker\n"
        << "Usage:\n"
        << "  " << programName << " pack   --project-root <dir> --output <file.horo> [options]\n"
        << "  " << programName << " unpack --input <file.horo> --output-dir <dir> [options]\n"
        << "  " << programName << " list   --input <file.horo>\n"
        << "  " << programName << " info   --input <file.horo>\n"
        << "  " << programName << " hash   --input <file> [--output <file.sha256>]\n"
        << "\n"
        << "Subcommands:\n"
        << "  pack     Bundle a project directory into a .horo archive.\n"
        << "  unpack   Extract a .horo archive to a directory.\n"
        << "  list     List all asset paths in a .horo archive.\n"
        << "  info     Print archive header metadata.\n"
        << "  hash     Compute SHA-256 checksum of any file (streaming).\n"
        << "\n"
        << "Pack options:\n"
        << "  --project-root <dir>   Root directory to scan for assets (required).\n"
        << "  --output <file>        Output .horo archive path (required).\n"
        << "  --password <pw>        Encrypt archive with AES-256-CTR.\n"
        << "  --compression <0-12>   LZ4 compression level (default: 1, 0 = off).\n"
        << "  --checksum             Write a .sha256 sidecar file alongside the archive.\n"
        << "\n"
        << "Unpack options:\n"
        << "  --input <file>         Input .horo archive path (required).\n"
        << "  --output-dir <dir>     Directory to extract assets into (required).\n"
        << "  --password <pw>        Decryption password.\n"
        << "\n"
        << "List/Info options:\n"
        << "  --input <file>         Input .horo archive path (required).\n"
        << "\n"
        << "Hash options:\n"
        << "  --input <file>         File to compute SHA-256 for (required).\n"
        << "  --output <file>        Write hex digest to .sha256 file (optional).\n"
        << "                         If omitted, prints '<hex>  <filename>' to stdout.\n"
        << "\n"
        << "Common options:\n"
        << "  --help, -h             Show this help message.\n"
        << "  --version, -V          Print version and exit.\n";
}

/** @brief Parse CLI arguments into a CliArgs struct.
 *
 *  Returns a default-constructed CliArgs (with showHelp = true) on parse
 *  errors or when help is requested. */
CliArgs ParseArgs(int argc, char** argv) {
    CliArgs args;

    if (argc < 2) {
        args.parseError = true;
        args.showHelp = true;
        return args;
    }

    // First positional argument is the subcommand.
    const std::string_view cmd(argv[1]);
    if (cmd == "pack") {
        args.subcommand = Subcommand::Pack;
    } else if (cmd == "unpack") {
        args.subcommand = Subcommand::Unpack;
    } else if (cmd == "list") {
        args.subcommand = Subcommand::List;
    } else if (cmd == "info") {
        args.subcommand = Subcommand::Info;
    } else if (cmd == "hash") {
        args.subcommand = Subcommand::Hash;
    } else if (cmd == "manifest") {
        args.subcommand = Subcommand::Manifest;
    } else if (cmd == "--help" || cmd == "-h") {
        args.showHelp = true;
        return args;
    } else if (cmd == "--version" || cmd == "-V") {
        args.showVersion = true;
        return args;
    } else {
        std::cerr << "Error: Unknown subcommand '" << cmd << "'\n";
        args.parseError = true;
        args.showHelp = true;
        return args;
    }

    // Parse flag arguments.
    for (int i = 2; i < argc; ++i) {
        if (!argv[i]) continue;

        const std::string_view arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            args.showHelp = true;
            return args;
        } else if (arg == "--project-root") {
            if (i + 1 < argc && argv[i + 1]) {
                args.projectRoot = argv[++i];
            } else {
                std::cerr << "Error: --project-root requires a value\n";
                args.parseError = true;
                args.showHelp = true;
                return args;
            }
        } else if (arg == "--output") {
            if (i + 1 < argc && argv[i + 1]) {
                args.output = argv[++i];
            } else {
                std::cerr << "Error: --output requires a value\n";
                args.parseError = true;
                args.showHelp = true;
                return args;
            }
        } else if (arg == "--input") {
            if (i + 1 < argc && argv[i + 1]) {
                args.input = argv[++i];
            } else {
                std::cerr << "Error: --input requires a value\n";
                args.parseError = true;
                args.showHelp = true;
                return args;
            }
        } else if (arg == "--output-dir") {
            if (i + 1 < argc && argv[i + 1]) {
                args.outputDir = argv[++i];
            } else {
                std::cerr << "Error: --output-dir requires a value\n";
                args.parseError = true;
                args.showHelp = true;
                return args;
            }
        } else if (arg == "--password") {
            if (i + 1 < argc && argv[i + 1]) {
                args.password = argv[++i];
            } else {
                std::cerr << "Error: --password requires a value\n";
                args.parseError = true;
                args.showHelp = true;
                return args;
            }
        } else if (arg == "--compression") {
            if (i + 1 < argc && argv[i + 1]) {
                const int level = std::atoi(argv[++i]);
                if (level < 0 || level > 12) {
                    std::cerr << "Error: --compression must be 0-12\n";
                    args.parseError = true;
                    args.showHelp = true;
                    return args;
                }
                args.compressionLevel = level;
            } else {
                std::cerr << "Error: --compression requires a value\n";
                args.parseError = true;
                args.showHelp = true;
                return args;
            }
        } else if (arg == "--checksum") {
            args.generateChecksum = true;
        } else if (arg.starts_with("--project-root=")) {
            args.projectRoot = std::string(arg.substr(15));
        } else if (arg.starts_with("--output=")) {
            args.output = std::string(arg.substr(9));
        } else if (arg.starts_with("--input=")) {
            args.input = std::string(arg.substr(8));
        } else if (arg.starts_with("--output-dir=")) {
            args.outputDir = std::string(arg.substr(13));
        } else if (arg.starts_with("--password=")) {
            args.password = std::string(arg.substr(11));
        } else if (arg.starts_with("--compression=")) {
            const int level = std::atoi(arg.data() + 14);
            if (level < 0 || level > 12) {
                std::cerr << "Error: --compression must be 0-12\n";
                args.parseError = true;
                args.showHelp = true;
                return args;
            }
            args.compressionLevel = level;
        } else {
            std::cerr << "Error: Unknown argument '" << arg << "'\n";
            args.parseError = true;
            args.showHelp = true;
            return args;
        }
    }

    return args;
}

/** @brief Validate that required arguments for a subcommand are present.
 *  @return True if all required arguments are set. */
bool ValidateArgs(const CliArgs& args) {
    switch (args.subcommand) {
    case Subcommand::Pack:
        if (args.projectRoot.empty()) {
            std::cerr << "Error: --project-root is required for pack\n";
            return false;
        }
        if (args.output.empty()) {
            std::cerr << "Error: --output is required for pack\n";
            return false;
        }
        if (!std::filesystem::is_directory(args.projectRoot)) {
            std::cerr << "Error: --project-root '" << args.projectRoot
                      << "' is not a directory\n";
            return false;
        }
        break;
    case Subcommand::Unpack:
        if (args.input.empty()) {
            std::cerr << "Error: --input is required for unpack\n";
            return false;
        }
        if (args.outputDir.empty()) {
            std::cerr << "Error: --output-dir is required for unpack\n";
            return false;
        }
        break;
    case Subcommand::List:
    case Subcommand::Info:
        if (args.input.empty()) {
            std::cerr << "Error: --input is required for list/info\n";
            return false;
        }
        break;
    case Subcommand::Hash:
        if (args.input.empty()) {
            std::cerr << "Error: --input is required for hash\n";
            return false;
        }
        break;
    default:
        return false;
    }
    return true;
}

// ── Hash helpers (used by CmdPack and CmdHash) ────────────────────────────

/** @brief Convert a 32-byte SHA-256 digest to a lowercase hex string.
 *
 *  Produces exactly 64 hex characters, no separators, no null terminator
 *  overhead beyond what std::string provides. */
std::string DigestToHex(const Horo::Archive::Sha256Digest& digest) {
    static constexpr char kHexChars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (const uint8_t byte : digest) {
        hex.push_back(kHexChars[byte >> 4]);
        hex.push_back(kHexChars[byte & 0x0F]);
    }
    return hex;
}

/** @brief Write a .sha256 checksum file in standard format: "<hex>  <filename>".
 *
 *  @param file_path  Path to the file whose checksum we're writing.
 *  @param digest     Pre-computed SHA-256 digest.
 *  @return true on success. */
bool WriteChecksumFile(const std::filesystem::path& file_path,
                       const Horo::Archive::Sha256Digest& digest) {
    const auto hex = DigestToHex(digest);
    const auto filename = file_path.filename().string();

    const auto checksum_path =
        std::filesystem::path(file_path).concat(".sha256");

    std::ofstream out(checksum_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << hex << "  " << filename << "\n";
    return out.good();
}

// ── Subcommand implementations ───────────────────────────────────────────

/** @brief Recursively collect all file paths under a directory.
 *
 *  Skips directories and symlinks.  Files are stored with paths relative
 *  to the given root. */
std::vector<std::string> CollectFiles(const std::filesystem::path& root) {
    std::vector<std::string> files;
    std::error_code ec;

    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (ec) continue;

        // Compute relative path with forward slashes (canonical archive format).
        std::string rel = std::filesystem::relative(it->path(), root, ec)
                              .generic_string();
        if (ec) continue;

        files.push_back(std::move(rel));
    }

    // Sort for deterministic archive output.
    std::sort(files.begin(), files.end());
    return files;
}

/** @brief Asset data provider that reads files from a project root directory.
 *
 *  Given an asset path (relative to the project root), reads the file and
 *  fills out_data. */
class ProjectAssetProvider {
public:
    explicit ProjectAssetProvider(std::filesystem::path root)
        : m_root(std::move(root)) {}

    bool operator()(const std::string& assetPath,
                    std::vector<uint8_t>& outData) const {
        const auto fullPath = m_root / assetPath;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(fullPath, ec) || ec) {
            std::cerr << "Error: Asset not found: " << fullPath << "\n";
            return false;
        }

        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open asset: " << fullPath << "\n";
            return false;
        }

        const auto size = file.tellg();
        if (size <= 0) {
            outData.clear();
            return true;
        }

        outData.resize(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(outData.data()), size);

        if (!file.good()) {
            std::cerr << "Error: Failed to read asset: " << fullPath << "\n";
            return false;
        }

        return true;
    }

private:
    std::filesystem::path m_root;
};

/** @brief Convert a PackResult to a human-readable string. */
const char* PackResultToString(Horo::Archive::PackResult result) {
    using Horo::Archive::PackResult;
    switch (result) {
    case PackResult::Ok:                 return "Ok";
    case PackResult::InvalidInput:       return "Invalid input";
    case PackResult::InvalidPath:        return "Invalid path";
    case PackResult::InvalidMagic:       return "Invalid magic bytes";
    case PackResult::UnsupportedVersion: return "Unsupported archive version";
    case PackResult::CompressionFailed:  return "Compression failed";
    case PackResult::DecompressionFailed:return "Decompression failed";
    case PackResult::EncryptionFailed:   return "Encryption failed";
    case PackResult::HashMismatch:       return "Hash mismatch";
    case PackResult::IoError:            return "I/O error";
    case PackResult::InvalidTOC:         return "Invalid table of contents";
    }
    return "Unknown error";
}

/** @brief Execute the 'pack' subcommand. */
int CmdPack(const CliArgs& args) {
    using namespace Horo::Archive;

    std::cout << "Scanning project root: " << args.projectRoot << "\n";
    const auto files = CollectFiles(args.projectRoot);
    if (files.empty()) {
        std::cerr << "Error: No files found in project root\n";
        return 1;
    }
    std::cout << "Found " << files.size() << " asset(s)\n";

    Packager packer;
    packer.SetCompressionLevel(args.compressionLevel);

    // Configure encryption if password is provided.
    if (!args.password.empty()) {
        std::array<uint8_t, 32> key{};
        if (!DeriveKeyFromPassword(args.password, key)) {
            std::cerr << "Error: Failed to derive encryption key from password\n";
            return 1;
        }
        packer.SetEncryptionEnabled(true);
        packer.SetEncryptionKey(key);
        std::cout << "Encryption: enabled (AES-256-CTR)\n";
    } else {
        std::cout << "Encryption: disabled\n";
    }

    if (args.compressionLevel > 0) {
        std::cout << "Compression: LZ4 level " << args.compressionLevel << "\n";
    } else {
        std::cout << "Compression: off\n";
    }

    // Register all assets.
    for (const auto& path : files) {
        const PackResult result = packer.AddAsset(path);
        if (result != PackResult::Ok) {
            std::cerr << "Error: Failed to register asset '" << path
                      << "': " << PackResultToString(result) << "\n";
            return 1;
        }
    }
    std::cout << "Registered " << packer.AssetCount() << " asset(s)\n";

    // Write archive.
    std::cout << "Writing archive to: " << args.output << "\n";
    const ProjectAssetProvider provider(args.projectRoot);
    const PackResult result = packer.Write(args.output, provider);
    if (result != PackResult::Ok) {
        std::cerr << "Error: Failed to write archive: "
                  << PackResultToString(result) << "\n";
        return 1;
    }

    // Report archive size.
    std::error_code ec;
    const auto archiveSize =
        std::filesystem::file_size(args.output, ec);
    if (!ec) {
        std::cout << "Archive written successfully ("
                  << archiveSize << " bytes)\n";
    } else {
        std::cout << "Archive written successfully\n";
    }

    // Generate SHA-256 checksum sidecar if requested.
    if (args.generateChecksum) {
        Horo::Archive::Sha256Digest digest{};
        if (!Horo::Archive::ComputeFileSHA256(args.output, digest.data())) {
            std::cerr << "Warning: Failed to compute SHA-256 checksum for "
                      << args.output << "\n";
            // Non-fatal — archive was written successfully.
        } else if (!WriteChecksumFile(args.output, digest)) {
            std::cerr << "Warning: Failed to write .sha256 checksum file for "
                      << args.output << "\n";
            // Non-fatal.
        } else {
            std::cout << "Checksum: "
                      << DigestToHex(digest) << "\n";
        }
    }

    return 0;
}

/** @brief Execute the 'unpack' subcommand. */
int CmdUnpack(const CliArgs& args) {
    using namespace Horo::Archive;

    Packager packer;

    // Configure decryption if password is provided.
    if (!args.password.empty()) {
        std::array<uint8_t, 32> key{};
        if (!DeriveKeyFromPassword(args.password, key)) {
            std::cerr << "Error: Failed to derive decryption key from password\n";
            return 1;
        }
        packer.SetEncryptionEnabled(true);
        packer.SetEncryptionKey(key);
    }

    // Open the archive.
    std::cout << "Opening archive: " << args.input << "\n";
    PackResult result = packer.Open(args.input);
    if (result != PackResult::Ok) {
        std::cerr << "Error: Failed to open archive: "
                  << PackResultToString(result) << "\n";
        return 1;
    }

    // List what we're about to extract.
    std::vector<std::string> assetPaths;
    result = packer.ListAssets(assetPaths);
    if (result != PackResult::Ok) {
        std::cerr << "Error: Failed to list assets: "
                  << PackResultToString(result) << "\n";
        return 1;
    }
    std::cout << "Archive contains " << assetPaths.size() << " asset(s)\n";

    // Extract all.
    std::cout << "Extracting to: " << args.outputDir << "\n";
    result = packer.ExtractAll(args.outputDir);
    if (result != PackResult::Ok) {
        std::cerr << "Error: Failed to extract archive: "
                  << PackResultToString(result) << "\n";
        return 1;
    }

    std::cout << "Extraction complete: " << assetPaths.size()
              << " asset(s) written\n";
    return 0;
}

/** @brief Execute the 'list' subcommand. */
int CmdList(const CliArgs& args) {
    using namespace Horo::Archive;

    Packager packer;
    const PackResult result = packer.Open(args.input);
    if (result != PackResult::Ok) {
        std::cerr << "Error: Failed to open archive: "
                  << PackResultToString(result) << "\n";
        return 1;
    }

    std::vector<std::string> assetPaths;
    const PackResult listResult = packer.ListAssets(assetPaths);
    if (listResult != PackResult::Ok) {
        std::cerr << "Error: Failed to list assets: "
                  << PackResultToString(listResult) << "\n";
        return 1;
    }

    std::cout << assetPaths.size() << " asset(s):\n";
    for (const auto& path : assetPaths) {
        std::cout << "  " << path << "\n";
    }
    return 0;
}

/** @brief Execute the 'info' subcommand. */
int CmdInfo(const CliArgs& args) {
    using namespace Horo::Archive;

    Packager packer;
    const PackResult result = packer.Open(args.input);
    if (result != PackResult::Ok) {
        std::cerr << "Error: Failed to open archive: "
                  << PackResultToString(result) << "\n";
        return 1;
    }

    std::vector<std::string> assetPaths;
    packer.ListAssets(assetPaths);

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(args.input, ec);

    std::cout << "Archive: " << args.input << "\n";
    if (!ec) {
        std::cout << "  File size: " << fileSize << " bytes\n";
    }
    std::cout << "  Asset count: " << assetPaths.size() << "\n";
    std::cout << "  Magic: HORO\n";
    std::cout << "  Version: " << static_cast<int>(kHoroVersion) << "\n";

    return 0;
}

/** @brief Compute the SHA-256 checksum of a file using streaming I/O and
 *         optionally write a .sha256 sidecar.
 *
 *  @param args  Parsed CLI arguments; uses args.input and args.output.
 *  @return 0 on success, non-zero on failure. */
int CmdHash(const CliArgs& args) {
    using namespace Horo::Archive;

    Sha256Digest digest{};
    if (!ComputeFileSHA256(args.input, digest.data())) {
        std::cerr << "Error: Failed to compute SHA-256 for: "
                  << args.input << "\n";
        return 1;
    }

    const auto hex = DigestToHex(digest);
    const auto filename = std::filesystem::path(args.input).filename().string();

    if (!args.output.empty()) {
        // Write .sha256 file at the specified output path.
        std::ofstream out(args.output, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "Error: Cannot open output file: "
                      << args.output << "\n";
            return 1;
        }
        out << hex << "  " << filename << "\n";
        if (!out.good()) {
            std::cerr << "Error: Failed to write checksum file: "
                      << args.output << "\n";
            return 1;
        }
        std::cout << "Checksum written to: " << args.output << "\n";
    } else {
        // Print to stdout: "<hex>  <filename>"
        std::cout << hex << "  " << filename << "\n";
    }

    return 0;
}

} // anonymous namespace

// ── Entry point ──────────────────────────────────────────────────────────

/** @brief Main entry point for the horopak CLI tool. */
int main(int argc, char** argv) {
    const CliArgs args = ParseArgs(argc, argv);

    if (args.showVersion) {
        std::cout << Horo::Build::EngineVersion() << "\n";
        return 0;
    }

    if (args.showHelp) {
        PrintUsage(argv[0] ? argv[0] : "horopak");
        // Explicit --help (no error): exit 0. Parse error: exit 1.
        return args.parseError ? 1 : 0;
    }

    if (!ValidateArgs(args)) {
        PrintUsage(argv[0] ? argv[0] : "horopak");
        return 1;
    }

    switch (args.subcommand) {
    case Subcommand::Pack:
        return CmdPack(args);
    case Subcommand::Unpack:
        return CmdUnpack(args);
    case Subcommand::List:
        return CmdList(args);
    case Subcommand::Info:
        return CmdInfo(args);
    case Subcommand::Hash:
        return CmdHash(args);
    default:
        std::cerr << "Error: No subcommand specified\n";
        PrintUsage(argv[0] ? argv[0] : "horopak");
        return 1;
    }
}
