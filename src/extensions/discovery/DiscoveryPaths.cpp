#include "DiscoveryPaths.h"

#include <algorithm>
#include <string>
#include <system_error>

namespace Horo::Extensions::Discovery {
    namespace {
        const ErrorCodeDescriptor InvalidDiscoveryPath{
            .domain = ErrorDomainId{"horo.extensions"},
            .code = ErrorCode{"invalid_discovery_path"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "The extension discovery path is invalid or outside its approved root.",
            .remediationHint = "Use an existing approved root and declared paths contained within it.",
            .retryable = false,
            .userActionable = true,
        };

        /** @brief Preserve the failing path and filesystem reason in a typed discovery error. */
        Result<std::filesystem::path> PathFailure(const std::filesystem::path &path, const std::string &reason) {
            return Result<std::filesystem::path>::Failure(MakeError(InvalidDiscoveryPath, path.generic_string() + ": " + reason));
        }

        /** @brief Reject ambiguous root names and parent traversal before filesystem resolution. */
        bool IsDeclaredRelativePath(const std::filesystem::path &path) {
            return !path.empty() && !path.has_root_path() && std::ranges::find(path, "..") == path.end();
        }

        /** @brief Compare complete components, never textual prefixes such as root and root-other. */
        bool IsStrictDescendant(const std::filesystem::path &root, const std::filesystem::path &candidate) {
            const auto [rootPosition, candidatePosition] = std::ranges::mismatch(root, candidate);
            return rootPosition == root.end() && candidatePosition != candidate.end();
        }
    }  // namespace

    /** @copydoc CanonicalRoot */
    Result<std::filesystem::path> CanonicalRoot(const std::filesystem::path &root) {
        if (!root.is_absolute())
            return PathFailure(root, "The approved root must be absolute.");
        std::error_code error;
        const auto canonical = std::filesystem::canonical(root, error);
        if (error)
            return PathFailure(root, error.message());
        if (!std::filesystem::is_directory(canonical, error))
            return PathFailure(root, error ? error.message() : "The approved root must be an accessible directory.");
        return Result<std::filesystem::path>::Success(canonical);
    }

    /** @copydoc ResolveContainedPath */
    Result<std::filesystem::path> ResolveContainedPath(const std::filesystem::path &canonicalRoot,
                                                       const std::filesystem::path &relativePath) {
        if (!IsDeclaredRelativePath(relativePath))
            return PathFailure(relativePath, "Expected a relative path without parent traversal.");
        std::error_code error;
        const auto canonical = std::filesystem::canonical(canonicalRoot / relativePath, error);
        if (error)
            return PathFailure(relativePath, error.message());
        if (!IsStrictDescendant(canonicalRoot, canonical))
            return PathFailure(relativePath, "Resolved content escapes or aliases the approved root.");
        return Result<std::filesystem::path>::Success(canonical);
    }
}  // namespace Horo::Extensions::Discovery
