#pragma once
#include <functional>
#include <string>
#include <string_view>

namespace Horo {
    /// Transparent hasher enabling heterogeneous lookup in unordered containers.
    /// Use as: std::unordered_map<std::string, V, StringHash, std::equal_to<>>
    /// This allows find/count/erase with std::string_view or const char* keys
    /// without constructing a temporary std::string (fixes SonarCloud cpp:S6045).
    struct StringHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };
} // namespace Horo
