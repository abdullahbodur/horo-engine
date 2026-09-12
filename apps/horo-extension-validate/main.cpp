#include "Horo/Extensions/ExtensionManifest.h"

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
        for (std::size_t index = 1; index < arguments.size(); ++index) {
            const std::string_view argument{arguments[index]};
            if (argument == "--json") {
                options.json = true;
            } else if (argument == "--schema-version" && index + 1 < arguments.size()) {
                if (std::string_view{arguments[++index]} != "1")
                    return std::nullopt;
            } else if (argument.starts_with('-') || !options.manifest.empty()) {
                return std::nullopt;
            } else {
                options.manifest = argument;
            }
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
                    escaped += "\\\"";
                    break;
                case '\\':
                    escaped += "\\\\";
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
                    if (const auto byte = static_cast<unsigned char>(character); byte < 0x20U) {
                        escaped += "\\u00";
                        escaped += HexDigits[byte >> 4U];
                        escaped += HexDigits[byte & 0x0FU];
                    } else {
                        escaped += character;
                    }
                    break;
            }
        }
        return escaped;
    }

    [[nodiscard]] ManifestReadResult ReadManifest(const std::filesystem::path &path) {
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(path, error);
        if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status)))
            return {.failure = ManifestReadFailure::Missing};
        if (error)
            return {.failure = ManifestReadFailure::Unreadable};
        if (!std::filesystem::is_regular_file(status))
            return {.failure = ManifestReadFailure::NotRegularFile};

        const std::uintmax_t size = std::filesystem::file_size(path, error);
        constexpr std::uintmax_t MaximumBytes = 64U * 1024U;
        if (error)
            return {.failure = ManifestReadFailure::Unreadable};
        if (size > MaximumBytes)
            return {.failure = ManifestReadFailure::TooLarge};
        std::ifstream input{path, std::ios::binary};
        if (!input)
            return {.failure = ManifestReadFailure::Unreadable};
        std::string content(static_cast<std::size_t>(size), '\0');
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
        if (input.gcount() != static_cast<std::streamsize>(content.size()) || input.peek() != std::ifstream::traits_type::eof())
            return {.failure = ManifestReadFailure::ChangedDuringRead};
        return {.content = std::move(content)};
    }

    [[nodiscard]] std::string_view ReadFailureCode(const ManifestReadFailure failure) {
        switch (failure) {
            case ManifestReadFailure::Missing:
                return "extension.manifest.input_missing";
            case ManifestReadFailure::NotRegularFile:
                return "extension.manifest.input_not_file";
            case ManifestReadFailure::TooLarge:
                return "extension.manifest.input_too_large";
            case ManifestReadFailure::Unreadable:
                return "extension.manifest.input_unreadable";
            case ManifestReadFailure::ChangedDuringRead:
                return "extension.manifest.input_changed";
            case ManifestReadFailure::None:
                break;
        }
        return "extension.manifest.input_invalid";
    }

    [[nodiscard]] std::string_view ReadFailureMessage(const ManifestReadFailure failure) {
        switch (failure) {
            case ManifestReadFailure::Missing:
                return "Manifest input does not exist.";
            case ManifestReadFailure::NotRegularFile:
                return "Manifest input is not a regular file.";
            case ManifestReadFailure::TooLarge:
                return "Manifest input exceeds the 65536-byte limit.";
            case ManifestReadFailure::Unreadable:
                return "Manifest input cannot be read.";
            case ManifestReadFailure::ChangedDuringRead:
                return "Manifest input changed while it was being read.";
            case ManifestReadFailure::None:
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
        std::cout << "{\"valid\":false,\"schemaVersion\":1,\"path\":\"$\",\"code\":\"" << code << "\",\"message\":\"" << message
                  << "\",\"line\":0,\"column\":0}\n";
    }

    void PrintValid(const Horo::Extensions::ExtensionManifest &manifest, const bool json) {
        if (!json) {
            std::cout << "valid: " << manifest.id << ' ' << manifest.version << '\n';
            return;
        }
        std::cout << "{\"valid\":true,\"schemaVersion\":" << manifest.schemaVersion << ",\"id\":\"" << EscapeJson(manifest.id)
                  << "\",\"version\":\"" << EscapeJson(manifest.version) << "\",\"moduleCount\":" << manifest.modules.size() << "}\n";
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
        std::cout << "{\"valid\":false,\"schemaVersion\":1,\"path\":\"" << EscapeJson(path) << "\",\"code\":\"" << EscapeJson(code)
                  << "\",\"message\":\"" << EscapeJson(error.message) << "\",\"line\":" << line << ",\"column\":" << column << "}\n";
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
