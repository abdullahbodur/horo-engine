#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Horo::Tests {
    inline std::uint64_t ReadDescriptorRevision(const std::filesystem::path &path) {
        std::ifstream stream{path};
        std::uint64_t revision{};
        if (!(stream >> revision) || revision == 0)
            throw std::runtime_error{"test gameplay descriptor revision is missing or invalid"};
        return revision;
    }
}  // namespace Horo::Tests
