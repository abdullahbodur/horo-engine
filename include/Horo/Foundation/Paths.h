#pragma once

#include "Horo/Foundation/Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace Horo
{
    /** @brief Normalized portable path relative to an open project root. */
    class ProjectPath
    {
    public:
        /** @brief Parses a portable relative path and rejects any root escape. */
        [[nodiscard]] static Result<ProjectPath> Parse(const std::string_view input)
        {
            std::vector<std::string> segments;
            std::string segment;
            const auto consume = [&segments](std::string value) -> bool {
                if (value.empty() || value == ".") return true;
                if (value == "..") { if (segments.empty()) return false; segments.pop_back(); return true; }
                segments.push_back(std::move(value));
                return true;
            };
            for (const char character : input)
            {
                if (character == '/' || character == '\\') { if (!consume(std::move(segment))) return FailureForEscape(); segment.clear(); }
                else segment += character;
            }
            if (!consume(std::move(segment))) return FailureForEscape();
            std::string normalized;
            for (const auto &part : segments) { if (!normalized.empty()) normalized += '/'; normalized += part; }
            return Result<ProjectPath>::Success(ProjectPath(std::move(normalized)));
        }

        [[nodiscard]] const std::string &String() const noexcept { return m_value; }
    private:
        explicit ProjectPath(std::string value) : m_value(std::move(value)) {}
        [[nodiscard]] static Result<ProjectPath> FailureForEscape()
        {
            return Result<ProjectPath>::Failure(Error{ErrorCode{"foundation.path.root_escape"}, ErrorDomainId{"horo.foundation"}, ErrorSeverity::Error, "Project path escapes its allowed root."});
        }
        std::string m_value;
    };

    /** @brief Stable project-root-relative paths shared across engine modules. */
    namespace ProjectLayout
    {
        /** @brief Derived asset registry index rebuilt from authoritative asset data. */
        inline constexpr std::string_view AssetIndexPath = ".horo/asset_index.json";
    }
} // namespace Horo
