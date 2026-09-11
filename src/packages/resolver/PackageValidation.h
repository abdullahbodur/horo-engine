#pragma once

#include "Horo/Packages/PackageDependencyResolver.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace Horo::Packages::Detail {
    [[nodiscard]] inline bool IsCanonicalPackageToken(const std::string_view text, const std::size_t maximum) noexcept {
        if (text.empty() || text.size() > maximum || text.front() == '.' || text.back() == '.' || text.find("..") != std::string_view::npos)
            return false;
        return std::ranges::all_of(text, [](const unsigned char value) {
            return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_';
        });
    }

    [[nodiscard]] inline bool IsValidPackagePlatform(const PackagePlatform &platform) noexcept {
        return IsCanonicalPackageToken(platform.operatingSystem, 64U) && IsCanonicalPackageToken(platform.architecture, 64U) &&
               IsCanonicalPackageToken(platform.sdkAbi, 128U);
    }
}  // namespace Horo::Packages::Detail
