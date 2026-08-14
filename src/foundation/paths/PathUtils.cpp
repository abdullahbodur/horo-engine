/**
 * @file PathUtils.cpp
 * @brief Implementation of OS-aware path utilities.
 */

#include "Horo/Foundation/PathUtils.h"

#include "../FoundationErrors.h"

#include <cctype>
#include <filesystem>
#include <system_error>

namespace Horo::Foundation::Paths {

    Result<void> EnsureDirectory(const std::filesystem::path &path) {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) {
            return Result<void>::Failure(
                MakeError(PathErrors::DirectoryCreateFailed, "Failed to create directory '" + path.string() + "': " + ec.message()));
        }
        return Result<void>::Success();
    }

    std::filesystem::path Resolve(const std::filesystem::path &projectRoot, const ProjectPath &relative) {
        std::filesystem::path result = projectRoot;
        result /= relative.String();
        return result;
    }

    Result<std::filesystem::path> Resolve(const std::filesystem::path &projectRoot, std::string_view relative) {
        // Parse through ProjectPath which validates no root escape
        auto parsed = ProjectPath::Parse(relative);
        if (parsed.HasError()) {
            return Result<std::filesystem::path>::Failure(
                MakeError(PathErrors::PathEscape, "Relative path '" + std::string(relative) + "' escapes the project root"));
        }
        return Result<std::filesystem::path>::Success(Resolve(projectRoot, parsed.Value()));
    }

    void NormalizeSeparators(std::string &path) {
#ifdef _WIN32
        constexpr char kForeignSep = '/';
        constexpr char kNativeSep = '\\';
#else
        constexpr char kForeignSep = '\\';
        constexpr char kNativeSep = '/';
#endif
        for (char &c : path) {
            if (c == kForeignSep)
                c = kNativeSep;
        }
    }

    bool HasPathPrefix(const std::filesystem::path &root, const std::filesystem::path &candidate) {
        if (root.empty() || candidate.empty())
            return false;

        std::error_code rootError;
        std::error_code candidateError;
        std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(root, rootError);
        std::filesystem::path normalizedCandidate = std::filesystem::weakly_canonical(candidate, candidateError);
        if (rootError)
            normalizedRoot = root.lexically_normal();
        if (candidateError)
            normalizedCandidate = candidate.lexically_normal();

        // Let the host filesystem resolve Windows short names, drive casing, and
        // junctions before falling back to lexical component comparison.
        std::error_code equivalentError;
        for (std::filesystem::path current = normalizedCandidate; !current.empty();) {
            if (std::filesystem::equivalent(normalizedRoot, current, equivalentError))
                return true;
            equivalentError.clear();
            const std::filesystem::path parent = current.parent_path();
            if (parent == current)
                break;
            current = parent;
        }

        auto rootPart = normalizedRoot.begin();
        auto candidatePart = normalizedCandidate.begin();
        while (rootPart != normalizedRoot.end() && candidatePart != normalizedCandidate.end()) {
#ifdef _WIN32
            const std::string r = rootPart->string();
            const std::string c = candidatePart->string();
            if (r.size() != c.size())
                return false;
            for (size_t i = 0; i < r.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(r[i])) != std::tolower(static_cast<unsigned char>(c[i])))
                    return false;
            }
#else
            if (*rootPart != *candidatePart)
                return false;
#endif
            ++rootPart;
            ++candidatePart;
        }
        return rootPart == normalizedRoot.end();
    }

}  // namespace Horo::Foundation::Paths
