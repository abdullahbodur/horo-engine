/**
 * @file PathUtils.cpp
 * @brief Implementation of OS-aware path utilities.
 */

#include "Horo/Foundation/PathUtils.h"

#include "../FoundationErrors.h"

#include <filesystem>
#include <system_error>

namespace Horo::Foundation::Paths
{

Result<void> EnsureDirectory(const std::filesystem::path &path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec)
    {
        return Result<void>::Failure(
            MakeError(PathErrors::DirectoryCreateFailed,
                      "Failed to create directory '" + path.string() + "': " + ec.message()));
    }
    return Result<void>::Success();
}

std::filesystem::path
Resolve(const std::filesystem::path &projectRoot, const ProjectPath &relative)
{
    std::filesystem::path result = projectRoot;
    result /= relative.String();
    return result;
}

Result<std::filesystem::path>
Resolve(const std::filesystem::path &projectRoot, std::string_view relative)
{
    // Parse through ProjectPath which validates no root escape
    auto parsed = ProjectPath::Parse(relative);
    if (parsed.HasError())
    {
        return Result<std::filesystem::path>::Failure(
            MakeError(PathErrors::PathEscape,
                      "Relative path '" + std::string(relative) + "' escapes the project root"));
    }
    return Result<std::filesystem::path>::Success(Resolve(projectRoot, parsed.Value()));
}

void NormalizeSeparators(std::string &path)
{
#ifdef _WIN32
    constexpr char kForeignSep = '/';
    constexpr char kNativeSep = '\\';
#else
    constexpr char kForeignSep = '\\';
    constexpr char kNativeSep = '/';
#endif
    for (char &c : path)
    {
        if (c == kForeignSep)
            c = kNativeSep;
    }
}

} // namespace Horo::Foundation::Paths
