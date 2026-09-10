#pragma once

#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>

namespace Horo::Cli::Detail {
    [[nodiscard]] inline std::string InputText(const std::span<const std::byte> bytes) {
        std::string text(bytes.size(), '\0');
        std::ranges::transform(bytes, text.begin(), [](const std::byte value) {
            return static_cast<char>(std::to_integer<unsigned char>(value));
        });
        return text;
    }

    [[nodiscard]] inline bool ValidJsonDocument(const std::span<const std::byte> bytes) {
        const std::string text = InputText(bytes);
        return !nlohmann::json::parse(text, nullptr, false).is_discarded();
    }

    [[nodiscard]] inline bool ValidJsonLines(const std::span<const std::byte> bytes, const std::size_t maximumLines) {
        const std::string text = InputText(bytes);
        const std::string_view input{text};
        std::size_t begin = 0;
        std::size_t count = 0;
        while (begin < input.size()) {
            const std::size_t end = input.find('\n', begin);
            std::string_view line = input.substr(begin, end == std::string_view::npos ? input.size() - begin : end - begin);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (line.empty())
                return false;
            ++count;
            if (count > maximumLines || nlohmann::json::parse(line, nullptr, false).is_discarded())
                return false;
            if (end == std::string_view::npos)
                break;
            begin = end + 1;
        }
        return count > 0;
    }
}  // namespace Horo::Cli::Detail
