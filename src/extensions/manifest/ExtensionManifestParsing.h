#pragma once

#include "Horo/Extensions/ExtensionManifest.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Horo::Extensions::ManifestParsing {
    using Json = nlohmann::json;

    [[nodiscard]] std::string ChildPath(std::string_view parent, std::string_view key);
    [[nodiscard]] std::string ElementPath(std::string_view parent, std::size_t index);
    [[nodiscard]] Error ManifestError(std::string_view path, std::string_view diagnosticCode, std::string_view reason,
                                      std::uint32_t line = 0, std::uint32_t column = 0);
    [[nodiscard]] Result<Json> ParseBoundedJson(std::string_view content, const ExtensionManifestLimits &limits);
}  // namespace Horo::Extensions::ManifestParsing
