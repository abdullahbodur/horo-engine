#include "Horo/Runtime/Ui/UiDiagnostics.h"
#include "Horo/Runtime/Ui/UiErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Runtime::Ui {
    namespace {
        /** @brief Creates one valid persistent Runtime UI identity for a diagnostic fixture. */
        template <typename Identity> Identity DiagnosticIdentity(const std::uint8_t marker) {
            auto bytes = SerializedUiId{};
            bytes.back() = marker;
            const auto identity = Identity::Create(bytes);
            REQUIRE(identity.HasValue());
            return identity.Value();
        }

        /** @brief Requires one exact Runtime UI diagnostic-construction failure. */
        void RequireDiagnosticFailure(const Result<UiDiagnosticRecord> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        }

        TEST_CASE("Runtime UI diagnostic categories expose stable derived names", "[runtime_ui][diagnostics]") {
            const std::array cases{
                std::pair{UiDiagnosticCategory::Document, std::string_view{"runtime_ui.document"}},
                std::pair{UiDiagnosticCategory::Layout, std::string_view{"runtime_ui.layout"}},
                std::pair{UiDiagnosticCategory::Text, std::string_view{"runtime_ui.text"}},
                std::pair{UiDiagnosticCategory::Input, std::string_view{"runtime_ui.input"}},
                std::pair{UiDiagnosticCategory::Focus, std::string_view{"runtime_ui.focus"}},
                std::pair{UiDiagnosticCategory::Binding, std::string_view{"runtime_ui.binding"}},
                std::pair{UiDiagnosticCategory::Render, std::string_view{"runtime_ui.render"}},
                std::pair{UiDiagnosticCategory::Accessibility, std::string_view{"runtime_ui.accessibility"}},
                std::pair{UiDiagnosticCategory::Lifecycle, std::string_view{"runtime_ui.lifecycle"}},
            };
            for (const auto &[category, name] : cases)
                REQUIRE(UiDiagnosticCategoryName(category) == name);
            REQUIRE(UiDiagnosticCategoryName(static_cast<UiDiagnosticCategory>(255)).empty());
        }

        TEST_CASE("Runtime UI diagnostic records map every canonical error and reject invented sources", "[runtime_ui][diagnostics]") {
            REQUIRE(UiDiagnosticErrorDescriptors().size() == 17);
            for (const ErrorCodeDescriptor *descriptor : UiDiagnosticErrorDescriptors()) {
                const auto record = MakeUiDiagnosticRecord(UiDiagnosticCategory::Document, MakeError(*descriptor));
                REQUIRE(record.HasValue());
                REQUIRE(record.Value().code.Value() == descriptor->code.Value());
            }

            auto invented = MakeError(UiErrors::DiagnosticInvalid);
            invented.code = ErrorCode{"runtime_ui.future.invented"};
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Document, invented), UiErrors::DiagnosticUnsupported);
            invented.domain = ErrorDomainId{"horo.physics"};
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Document, invented), UiErrors::DiagnosticUnsupported);
        }

        TEST_CASE("Runtime UI diagnostic severity mapping is complete and preserves fatal evidence", "[runtime_ui][diagnostics]") {
            auto error = MakeError(UiErrors::DocumentInvalid);
            const std::array cases{
                std::pair{ErrorSeverity::Info, DiagnosticSeverity::Note},
                std::pair{ErrorSeverity::Warning, DiagnosticSeverity::Warning},
                std::pair{ErrorSeverity::Error, DiagnosticSeverity::Error},
                std::pair{ErrorSeverity::Critical, DiagnosticSeverity::Fatal},
            };
            for (const auto &[source, expected] : cases) {
                error.severity = source;
                const auto record = MakeUiDiagnosticRecord(UiDiagnosticCategory::Lifecycle, error);
                REQUIRE(record.HasValue());
                REQUIRE(record.Value().severity == expected);
            }
            error.severity = static_cast<ErrorSeverity>(255);
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Lifecycle, error), UiErrors::DiagnosticInvalid);
        }

        TEST_CASE("Runtime UI diagnostic records own complete ordered correlation evidence", "[runtime_ui][diagnostics]") {
            const auto document = DiagnosticIdentity<UiDocumentId>(1);
            const auto element = DiagnosticIdentity<UiElementId>(2);
            const auto canvas = DiagnosticIdentity<UiCanvasId>(3);
            std::vector<UiDiagnosticCorrelationEntry> correlation{
                {UiDiagnosticCorrelationKey::Document, document},
                {UiDiagnosticCorrelationKey::Element, element},
                {UiDiagnosticCorrelationKey::Canvas, canvas},
                {UiDiagnosticCorrelationKey::Player, std::uint64_t{0}},
                {UiDiagnosticCorrelationKey::Viewport, std::uint64_t{9}},
                {UiDiagnosticCorrelationKey::Operation, std::uint64_t{17}},
            };
            auto error = MakeError(UiErrors::CapacityExceeded, "Layout work exceeded its declared budget.");
            const auto record = MakeUiDiagnosticRecord(UiDiagnosticCategory::Layout, error, correlation);
            REQUIRE(record.HasValue());
            REQUIRE(record.Value().schemaVersion == 1);
            REQUIRE(record.Value().category == UiDiagnosticCategory::Layout);
            REQUIRE(record.Value().correlationCount == MaximumUiDiagnosticCorrelationEntries);
            REQUIRE(std::get<UiDocumentId>(record.Value().correlation[0].value) == document);
            REQUIRE(std::get<std::uint64_t>(record.Value().correlation[3].value) == 0);
            REQUIRE(std::get<std::uint64_t>(record.Value().correlation[5].value) == 17);

            error.message[0] = 'X';
            correlation.clear();
            REQUIRE(record.Value().message == "Layout work exceeded its declared budget.");
            REQUIRE(std::get<UiCanvasId>(record.Value().correlation[2].value) == canvas);
        }

        TEST_CASE("Runtime UI diagnostic construction rejects correlation ordering type and range errors", "[runtime_ui][diagnostics]") {
            const auto error = MakeError(UiErrors::DiagnosticInvalid);
            std::array<UiDiagnosticCorrelationEntry, MaximumUiDiagnosticCorrelationEntries + 1> oversized{};
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Input, error, oversized), UiErrors::DiagnosticInvalid);

            const std::array duplicate{
                UiDiagnosticCorrelationEntry{UiDiagnosticCorrelationKey::Player, std::uint64_t{0}},
                UiDiagnosticCorrelationEntry{UiDiagnosticCorrelationKey::Player, std::uint64_t{1}},
            };
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Input, error, duplicate), UiErrors::DiagnosticInvalid);
            const std::array unordered{
                UiDiagnosticCorrelationEntry{UiDiagnosticCorrelationKey::Operation, std::uint64_t{2}},
                UiDiagnosticCorrelationEntry{UiDiagnosticCorrelationKey::Viewport, std::uint64_t{1}},
            };
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Render, error, unordered), UiErrors::DiagnosticInvalid);
            const std::array wrongType{
                UiDiagnosticCorrelationEntry{UiDiagnosticCorrelationKey::Document, std::uint64_t{1}},
            };
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Document, error, wrongType), UiErrors::DiagnosticInvalid);
            const std::array unknownKey{
                UiDiagnosticCorrelationEntry{static_cast<UiDiagnosticCorrelationKey>(255), std::uint64_t{1}},
            };
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Document, error, unknownKey),
                                     UiErrors::DiagnosticInvalid);

            const std::array playerOverflow{
                UiDiagnosticCorrelationEntry{UiDiagnosticCorrelationKey::Player, std::uint64_t{256}},
            };
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Input, error, playerOverflow),
                                     UiErrors::DiagnosticInvalid);
            for (const UiDiagnosticCorrelationKey key : {UiDiagnosticCorrelationKey::Viewport, UiDiagnosticCorrelationKey::Operation}) {
                const std::array zero{UiDiagnosticCorrelationEntry{key, std::uint64_t{0}}};
                RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Lifecycle, error, zero), UiErrors::DiagnosticInvalid);
            }
        }

        TEST_CASE("Runtime UI diagnostic construction rejects invalid identities and message boundaries transactionally",
                  "[runtime_ui][diagnostics]") {
            auto error = MakeError(UiErrors::DocumentInvalid);
            const std::array invalidDocument{
                UiDiagnosticCorrelationEntry{UiDiagnosticCorrelationKey::Document, UiDocumentId{}},
            };
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Document, error, invalidDocument),
                                     UiErrors::DiagnosticInvalid);

            error.message.assign(MaximumUiDiagnosticMessageBytes, 'a');
            const auto boundary = MakeUiDiagnosticRecord(UiDiagnosticCategory::Text, error);
            REQUIRE(boundary.HasValue());
            error.message.push_back('b');
            const auto original = error.message;
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Text, error), UiErrors::DiagnosticInvalid);
            REQUIRE(error.message == original);

            error.message.clear();
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(UiDiagnosticCategory::Text, error), UiErrors::DiagnosticInvalid);
            RequireDiagnosticFailure(MakeUiDiagnosticRecord(static_cast<UiDiagnosticCategory>(255), MakeError(UiErrors::DocumentInvalid)),
                                     UiErrors::DiagnosticUnsupported);
        }

        TEST_CASE("Runtime UI diagnostic evidence survives source retirement without hidden owner state", "[runtime_ui][diagnostics]") {
            std::vector<UiDiagnosticRecord> retained;
            {
                auto source = MakeError(UiErrors::InstanceStateInvalid, "The source runtime is shutting down.");
                std::vector<UiDiagnosticCorrelationEntry> correlation{
                    {UiDiagnosticCorrelationKey::Document, DiagnosticIdentity<UiDocumentId>(7)},
                    {UiDiagnosticCorrelationKey::Operation, std::uint64_t{21}},
                };
                auto record = MakeUiDiagnosticRecord(UiDiagnosticCategory::Lifecycle, source, correlation);
                REQUIRE(record.HasValue());
                retained.push_back(std::move(record).Value());
            }
            REQUIRE(retained.front().message == "The source runtime is shutting down.");
            REQUIRE(retained.front().correlationCount == 2);
            REQUIRE(std::get<std::uint64_t>(retained.front().correlation[1].value) == 21);
        }
    }  // namespace
}  // namespace Horo::Runtime::Ui
