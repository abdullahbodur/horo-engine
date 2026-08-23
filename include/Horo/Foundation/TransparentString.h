#pragma once

/**
 * @file TransparentString.h
 * @brief Transparent hashing/equality helpers for heterogeneous string map lookups.
 */

#include <concepts>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Horo {

    /** @brief Hasher that accepts any string-like key without constructing a temporary std::string. */
    struct TransparentStringHash {
        using is_transparent = void; /**< Enables heterogeneous lookup. */

        [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    /** @brief Equality that compares any two string-like keys without conversions. */
    struct TransparentStringEqual {
        using is_transparent = void; /**< Enables heterogeneous lookup. */

        [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept {
            return left == right;
        }
    };

    /** @brief String-keyed map that supports heterogeneous lookup without temporary key allocations. */
    template <typename Value>
    using TransparentStringMap = std::unordered_map<std::string, Value, TransparentStringHash, TransparentStringEqual>;

    /** @brief String set that supports heterogeneous lookup without temporary key allocations. */
    using TransparentStringSet = std::unordered_set<std::string, TransparentStringHash, TransparentStringEqual>;
}  // namespace Horo
