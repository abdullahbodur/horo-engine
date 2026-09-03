#include "ExtensionManifestParsing.h"
#include "Horo/Extensions/ExtensionErrors.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace Horo::Extensions::ManifestParsing {
    namespace {
        constexpr std::string_view ManifestSource = "extension.json";

        [[nodiscard]] bool HasValidLimits(const ExtensionManifestLimits &limits) noexcept {
            const std::array values = {
                limits.maximumDocumentBytes, limits.maximumNestingDepth,  limits.maximumObjectMembers,
                limits.maximumArrayElements, limits.maximumStringBytes,   limits.maximumIdentifierBytes,
                limits.maximumModules,       limits.maximumContributions, limits.maximumPlatforms,
            };
            return std::ranges::all_of(values, [](const std::size_t value) {
                return value > 0;
            });
        }

        [[nodiscard]] bool IsOpeningContainer(const char character) noexcept {
            return character == '{' || character == '[';
        }

        [[nodiscard]] bool IsClosingContainer(const char character) noexcept {
            return character == '}' || character == ']';
        }

        [[nodiscard]] bool ConsumeQuotedCharacter(const char character, bool &inString, bool &escaped) noexcept {
            if (!inString) {
                if (character == '"') {
                    inString = true;
                    return true;
                }
                return false;
            }
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            return true;
        }

        [[nodiscard]] std::optional<Error> CheckRawNesting(const std::string_view content, const ExtensionManifestLimits &limits) {
            std::size_t depth = 0;
            bool inString = false;
            bool escaped = false;
            for (const char character : content) {
                if (ConsumeQuotedCharacter(character, inString, escaped))
                    continue;
                if (IsOpeningContainer(character)) {
                    ++depth;
                    if (depth > limits.maximumNestingDepth) {
                        return ManifestError("$", "extension.manifest.nesting_limit", "JSON nesting exceeds the configured limit.");
                    }
                }
                if (IsClosingContainer(character) && depth > 0)
                    --depth;
            }
            return std::nullopt;
        }

        enum class ContainerKind {
            Object,
            Array,
        };

        struct ParserFrame {
            ContainerKind kind{};
            std::string path;
            std::set<std::string, std::less<>> keys;
            std::optional<std::string> pendingKey;
            std::size_t nextIndex = 0;
        };

        struct ParserFailure {
            Error error;
        };

        class BoundedJsonParser final {
        public:
            explicit BoundedJsonParser(const ExtensionManifestLimits &limits) : limits_(limits) {}

            [[nodiscard]] Json Parse(const std::string_view content) {
                const Json::parser_callback_t callback = [this](const int, const Json::parse_event_t event, const Json &parsed) {
                    OnEvent(event, parsed);
                    return true;
                };
                return Json::parse(content.begin(), content.end(), callback, true, false);
            }

        private:
            [[nodiscard]] std::string NextPath() const {
                if (frames_.empty())
                    return "$";
                const ParserFrame &parent = frames_.back();
                if (parent.kind == ContainerKind::Array)
                    return ElementPath(parent.path, parent.nextIndex);
                return parent.pendingKey.has_value() ? ChildPath(parent.path, *parent.pendingKey) : parent.path;
            }

            void ConsumeParentValue() {
                if (frames_.empty())
                    return;
                ParserFrame &parent = frames_.back();
                if (parent.kind == ContainerKind::Object) {
                    parent.pendingKey.reset();
                    return;
                }
                if (++arrayElements_ > limits_.maximumArrayElements) {
                    Fail(ElementPath(parent.path, parent.nextIndex), "extension.manifest.array_limit",
                         "Total array elements exceed the configured limit.");
                }
                ++parent.nextIndex;
            }

            void BeginContainer(const ContainerKind kind) {
                const std::string path = NextPath();
                if (!frames_.empty() && frames_.back().kind == ContainerKind::Array)
                    ConsumeParentValue();
                frames_.push_back(ParserFrame{.kind = kind, .path = path});
            }

            void EndContainer() {
                if (frames_.empty())
                    return;
                frames_.pop_back();
                if (!frames_.empty() && frames_.back().kind == ContainerKind::Object)
                    frames_.back().pendingKey.reset();
            }

            void OnKey(const Json &parsed) {
                if (frames_.empty() || frames_.back().kind != ContainerKind::Object)
                    return;
                ParserFrame &object = frames_.back();
                const std::string &key = parsed.get_ref<const std::string &>();
                const std::string path = ChildPath(object.path, key);
                if (key.size() > limits_.maximumIdentifierBytes)
                    Fail(path, "extension.manifest.identifier_limit", "Field name exceeds the configured identifier limit.");
                if (++objectMembers_ > limits_.maximumObjectMembers)
                    Fail(path, "extension.manifest.object_limit", "Total object members exceed the configured limit.");
                if (!object.keys.insert(key).second)
                    Fail(path, "extension.manifest.duplicate_field", "Duplicate field is not allowed.");
                object.pendingKey = key;
            }

            void OnValue(const Json &parsed) {
                const std::string path = NextPath();
                if (parsed.is_string() && parsed.get_ref<const std::string &>().size() > limits_.maximumStringBytes)
                    Fail(path, "extension.manifest.string_limit", "String exceeds the configured limit.");
                ConsumeParentValue();
            }

            void OnEvent(const Json::parse_event_t event, const Json &parsed) {
                using enum Json::parse_event_t;
                switch (event) {
                    case object_start:
                        BeginContainer(ContainerKind::Object);
                        break;
                    case array_start:
                        BeginContainer(ContainerKind::Array);
                        break;
                    case object_end:
                    case array_end:
                        EndContainer();
                        break;
                    case key:
                        OnKey(parsed);
                        break;
                    case value:
                        OnValue(parsed);
                        break;
                }
            }

            [[noreturn]] static void Fail(const std::string_view path, const std::string_view code, const std::string_view reason) {
                throw ParserFailure{ManifestError(path, code, reason)};
            }

            const ExtensionManifestLimits &limits_;
            std::vector<ParserFrame> frames_;
            std::size_t objectMembers_ = 0;
            std::size_t arrayElements_ = 0;
        };

        [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> LocationAt(const std::string_view content, const std::size_t byte) noexcept {
            std::uint32_t line = 1;
            std::uint32_t column = 1;
            const std::size_t end = std::min(byte > 0 ? byte - 1 : 0, content.size());
            for (std::size_t index = 0; index < end; ++index) {
                if (content[index] == '\n') {
                    ++line;
                    column = 1;
                } else {
                    ++column;
                }
            }
            return {line, column};
        }

        [[nodiscard]] Result<Json> ParseJson(const std::string_view content, const ExtensionManifestLimits &limits) {
            try {
                return Result<Json>::Success(BoundedJsonParser{limits}.Parse(content));
            } catch (ParserFailure &failure) {
                return Result<Json>::Failure(std::move(failure.error));
            } catch (const Json::parse_error &exception) {
                const auto [line, column] = LocationAt(content, exception.byte);
                return Result<Json>::Failure(ManifestError("$", "extension.manifest.invalid_json", "Malformed JSON syntax.", line, column));
            }
        }
    }  // namespace

    std::string ChildPath(const std::string_view parent, const std::string_view key) {
        return std::format("{}.{}", parent, key);
    }

    std::string ElementPath(const std::string_view parent, const std::size_t index) {
        return std::format("{}[{}]", parent, index);
    }

    Error ManifestError(const std::string_view path, const std::string_view diagnosticCode, const std::string_view reason,
                        const std::uint32_t line, const std::uint32_t column) {
        const std::string message = std::format("{}: {}", path, reason);
        Error error = MakeError(ExtensionErrors::InvalidManifest, message);
        error.diagnostics.push_back(Diagnostic{
            .code = DiagnosticCode{std::string{diagnosticCode}},
            .severity = DiagnosticSeverity::Error,
            .message = message,
            .location = SourceLocation{.source = std::string{ManifestSource}, .line = line, .column = column},
        });
        return error;
    }

    Result<Json> ParseBoundedJson(const std::string_view content, const ExtensionManifestLimits &limits) {
        if (!HasValidLimits(limits))
            return Result<Json>::Failure(
                ManifestError("$", "extension.manifest.invalid_limits", "Every parser limit must be greater than zero."));
        if (content.size() > limits.maximumDocumentBytes)
            return Result<Json>::Failure(
                ManifestError("$", "extension.manifest.document_limit", "Document exceeds the configured byte limit."));
        if (const std::optional<Error> nestingError = CheckRawNesting(content, limits); nestingError.has_value())
            return Result<Json>::Failure(*nestingError);
        return ParseJson(content, limits);
    }
}  // namespace Horo::Extensions::ManifestParsing
