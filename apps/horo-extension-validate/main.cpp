#include "Horo/Extensions/ExtensionManifest.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
    struct Options {
        std::filesystem::path manifest;
        bool json{};
    };

    enum class ManifestReadFailure : std::uint8_t {
        None,
        Missing,
        NotRegularFile,
        TooLarge,
        Unreadable,
        ChangedDuringRead,
    };

    struct ManifestReadResult {
        std::string content;
        ManifestReadFailure failure{ManifestReadFailure::None};

        [[nodiscard]] bool HasValue() const noexcept {
            return failure == ManifestReadFailure::None;
        }
    };

    void PrintUsage() {
        std::cerr << "Usage: horo-extension-validate [--json] [--schema-version 1] <extension.json>\n";
    }

    [[nodiscard]] std::optional<Options> ParseOptions(const std::span<char *> arguments) {
        Options options;
        std::size_t index = 1;
        while (index < arguments.size()) {
            const std::string_view argument{arguments[index]};
            if (argument == "--json") {
                options.json = true;
            } else if (argument == "--schema-version") {
                ++index;
                if (index == arguments.size() || std::string_view{arguments[index]} != "1")
                    return std::nullopt;
            } else if (argument.starts_with('-') || !options.manifest.empty()) {
                return std::nullopt;
            } else {
                options.manifest = argument;
            }
            ++index;
        }
        return options.manifest.empty() ? std::nullopt : std::optional{std::move(options)};
    }

    [[nodiscard]] std::string EscapeJson(const std::string_view value) {
        constexpr char HexDigits[] = "0123456789abcdef";
        std::string escaped;
        escaped.reserve(value.size());
        for (const char character : value) {
            switch (character) {
                case '"':
                    escaped += R"(\")";
                    break;
                case '\\':
                    escaped += R"(\\)";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    if (const auto byte = static_cast<std::byte>(static_cast<unsigned char>(character)); byte < std::byte{0x20}) {
                        escaped += "\\u00";
                        escaped += HexDigits[std::to_integer<std::size_t>(byte >> 4U)];
                        escaped += HexDigits[std::to_integer<std::size_t>(byte & std::byte{0x0F})];
                    } else {
                        escaped += character;
                    }
                    break;
            }
        }
        return escaped;
    }

    [[nodiscard]] ManifestReadResult ReadManifest(const std::filesystem::path &path) {
        using enum ManifestReadFailure;
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(path, error);
        if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status)))
            return {.failure = Missing};
        if (error)
            return {.failure = Unreadable};
        if (!std::filesystem::is_regular_file(status))
            return {.failure = NotRegularFile};

        const std::uintmax_t size = std::filesystem::file_size(path, error);
        constexpr std::uintmax_t MaximumBytes = 64U * 1024U;
        if (error)
            return {.failure = Unreadable};
        if (size > MaximumBytes)
            return {.failure = TooLarge};
        std::ifstream input{path, std::ios::binary};
        if (!input)
            return {.failure = Unreadable};
        std::string content(static_cast<std::size_t>(size), '\0');
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
        if (input.gcount() != static_cast<std::streamsize>(content.size()) || input.peek() != std::ifstream::traits_type::eof())
            return {.failure = ChangedDuringRead};
        return {.content = std::move(content)};
    }

    [[nodiscard]] std::string_view ReadFailureCode(const ManifestReadFailure failure) {
        using enum ManifestReadFailure;
        switch (failure) {
            case Missing:
                return "extension.manifest.input_missing";
            case NotRegularFile:
                return "extension.manifest.input_not_file";
            case TooLarge:
                return "extension.manifest.input_too_large";
            case Unreadable:
                return "extension.manifest.input_unreadable";
            case ChangedDuringRead:
                return "extension.manifest.input_changed";
            case None:
                break;
        }
        return "extension.manifest.input_invalid";
    }

    [[nodiscard]] std::string_view ReadFailureMessage(const ManifestReadFailure failure) {
        using enum ManifestReadFailure;
        switch (failure) {
            case Missing:
                return "Manifest input does not exist.";
            case NotRegularFile:
                return "Manifest input is not a regular file.";
            case TooLarge:
                return "Manifest input exceeds the 65536-byte limit.";
            case Unreadable:
                return "Manifest input cannot be read.";
            case ChangedDuringRead:
                return "Manifest input changed while it was being read.";
            case None:
                break;
        }
        return "Manifest input is invalid.";
    }

    void PrintReadFailure(const ManifestReadFailure failure, const std::filesystem::path &path, const bool json) {
        const std::string_view code = ReadFailureCode(failure);
        const std::string_view message = ReadFailureMessage(failure);
        if (!json) {
            std::cerr << code << ": " << message << ' ' << path.string() << '\n';
            return;
        }
        std::cout << R"({"valid":false,"schemaVersion":1,"path":"$","code":")" << code << R"(","message":")" << message
                  << R"(","line":0,"column":0})" << '\n';
    }

    void PrintValid(const Horo::Extensions::ExtensionManifest &manifest, const bool json) {
        if (!json) {
            std::cout << "valid: " << manifest.id << ' ' << manifest.version << '\n';
            return;
        }
        std::cout << R"({"valid":true,"schemaVersion":)" << manifest.schemaVersion << R"(,"id":")" << EscapeJson(manifest.id)
                  << R"(","version":")" << EscapeJson(manifest.version) << R"(","moduleCount":)" << manifest.modules.size() << "}\n";
    }

    void PrintInvalid(const Horo::Error &error, const bool json) {
        const Horo::Diagnostic *diagnostic = error.diagnostics.empty() ? nullptr : &error.diagnostics.front();
        const std::string_view code = diagnostic == nullptr ? std::string_view{"extension.manifest.invalid"} : diagnostic->code.Value();
        const std::string_view path = diagnostic == nullptr || diagnostic->path.empty() ? std::string_view{"$"} : diagnostic->path;
        const std::uint32_t line = diagnostic == nullptr ? 0 : diagnostic->location.line;
        const std::uint32_t column = diagnostic == nullptr ? 0 : diagnostic->location.column;
        if (!json) {
            std::cerr << code << ": " << error.message;
            if (line != 0)
                std::cerr << " (" << line << ':' << column << ')';
            std::cerr << '\n';
            return;
        }
        std::cout << R"({"valid":false,"schemaVersion":1,"path":")" << EscapeJson(path) << R"(","code":")" << EscapeJson(code)
                  << R"(","message":")" << EscapeJson(error.message) << R"(","line":)" << line << R"(,"column":)" << column << "}\n";
    }
}  // namespace

int main(const int argc, char **argv) {
    const auto options = ParseOptions(std::span{argv, static_cast<std::size_t>(argc)});
    if (!options) {
        PrintUsage();
        return 2;
    }
    const auto content = ReadManifest(options->manifest);
    if (!content.HasValue()) {
        PrintReadFailure(content.failure, options->manifest, options->json);
        return 2;
    }
    const auto result = Horo::Extensions::ParseExtensionManifest(content.content);
    if (result.HasError()) {
        PrintInvalid(result.ErrorValue(), options->json);
        return 1;
    }
    PrintValid(result.Value(), options->json);
    return 0;
}
