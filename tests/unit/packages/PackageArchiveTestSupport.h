#pragma once

#include "Horo/Foundation/Sha256.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <memory>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

namespace Horo::Tests::Packages {
    inline nlohmann::json FileInventoryEntry(const std::string_view path, const std::string_view content) {
        return {{"path", path},
                {"size", content.size()},
                {"sha256", FormatSha256(ComputeSha256(std::as_bytes(std::span{content})))},
                {"executable", false},
                {"contributionRoot", nullptr}};
    }

    inline std::vector<std::byte> FinalizeArchive(mz_zip_archive &zip) {
        void *buffer = nullptr;
        std::size_t size = 0;
        REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size));
        const std::unique_ptr<void, decltype(&std::free)> owner{buffer, &std::free};
        const auto *bytes = static_cast<const std::byte *>(buffer);
        std::vector<std::byte> result(bytes, bytes + size);
        REQUIRE(mz_zip_writer_end(&zip));
        return result;
    }
}  // namespace Horo::Tests::Packages
