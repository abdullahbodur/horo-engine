#include "Horo/Extensions/ExtensionManifest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {
    struct Options {
        std::filesystem::path manifest;
        bool json{};
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

    [[nodiscard]] std::optional<std::string> ReadManifest(const std::filesystem::path &path) {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        constexpr std::uintmax_t MaximumBytes = 64U * 1024U;
        if (error || size > MaximumBytes)
            return std::nullopt;
        std::ifstream input{path, std::ios::binary};
        if (!input)
            return std::nullopt;
        std::string content(static_cast<std::size_t>(size), '\0');
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
        if (input.gcount() != static_cast<std::streamsize>(content.size()) || input.peek() != std::ifstream::traits_type::eof())
            return std::nullopt;
        return content;
    }

    [[nodiscard]] std::string_view DiagnosticPath(const Horo::Error &error) {
        const std::size_t separator = error.message.find(':');
        return separator == std::string::npos ? std::string_view{"$"} : std::string_view{error.message}.substr(0, separator);
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
        const std::uint32_t line = diagnostic == nullptr ? 0 : diagnostic->location.line;
        const std::uint32_t column = diagnostic == nullptr ? 0 : diagnostic->location.column;
        if (!json) {
            std::cerr << code << ": " << error.message;
            if (line != 0)
                std::cerr << " (" << line << ':' << column << ')';
            std::cerr << '\n';
            return;
        }
        std::cout << "{\"valid\":false,\"schemaVersion\":1,\"path\":\"" << EscapeJson(DiagnosticPath(error)) << "\",\"code\":\""
                  << EscapeJson(code) << "\",\"message\":\"" << EscapeJson(error.message) << "\",\"line\":" << line
                  << ",\"column\":" << column << "}\n";
    }
}  // namespace

int main(const int argc, char **argv) {
    const auto options = ParseOptions(std::span{argv, static_cast<std::size_t>(argc)});
    if (!options) {
        PrintUsage();
        return 2;
    }
    const auto content = ReadManifest(options->manifest);
    if (!content) {
        std::cerr << "Unable to read a manifest within the 65536-byte limit: " << options->manifest.string() << '\n';
        return 2;
    }
    const auto result = Horo::Extensions::ParseExtensionManifest(*content);
    if (result.HasError()) {
        PrintInvalid(result.ErrorValue(), options->json);
        return 1;
    }
    PrintValid(result.Value(), options->json);
    return 0;
}
