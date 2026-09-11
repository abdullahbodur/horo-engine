#include "Horo/Foundation/ModuleDescriptor.h"
#include "Horo/Foundation/ValidationResult.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

namespace {
    using namespace Horo;

    [[nodiscard]] ErrorCodeDescriptor Descriptor(const std::string_view code, const ErrorSeverity severity,
                                                 const std::string_view summary) {
        return {.domain = ErrorDomainId{"horo.asset.validation"},
                .code = ErrorCode{std::string{code}},
                .defaultSeverity = severity,
                .summary = summary};
    }

    const ErrorCodeDescriptor kNote = Descriptor("asset.validation.note", ErrorSeverity::Info, "Asset validation note.");
    const ErrorCodeDescriptor kWarning = Descriptor("asset.validation.warning", ErrorSeverity::Warning, "Asset validation warning.");
    const ErrorCodeDescriptor kError = Descriptor("asset.validation.error", ErrorSeverity::Error, "Asset validation error.");
    const ErrorCodeDescriptor kFatal = Descriptor("asset.validation.fatal", ErrorSeverity::Critical, "Asset validation fatal error.");

    [[nodiscard]] ErrorCodeRegistry Registry() {
        ModuleDescriptor module{.id = ModuleId{"horo.assets"}, .version = {1, 0, 0}};
        module.errorDomains.push_back({.id = ErrorDomainId{"horo.asset.validation"}, .descriptors = {&kNote, &kWarning, &kError, &kFatal}});
        Result<ErrorCodeRegistry> built = BuildErrorCodeRegistry(std::span{&module, 1});
        REQUIRE(built.HasValue());
        return std::move(built).Value();
    }

    [[nodiscard]] ValidationResultBuilder Builder(const std::size_t capacity = ValidationResultLimits::DefaultMaximumDiagnostics) {
        Result<ValidationResultBuilder> created = ValidationResultBuilder::Create(Registry(), {.maximumDiagnostics = capacity});
        REQUIRE(created.HasValue());
        return std::move(created).Value();
    }

    [[nodiscard]] SourceLocation At(std::string source, const std::uint32_t line = 0, const std::uint32_t column = 0) {
        return {.source = std::move(source), .line = line, .column = column};
    }
}  // namespace

TEST_CASE("Validation pass reports every unique finding in deterministic order", "[unit][foundation][validation]") {
    ValidationResultBuilder builder = Builder();
    REQUIRE(builder.Add(kWarning, "z warning", At("b.asset", 9, 2)).HasValue());
    REQUIRE(builder.Add(kError, "error", At("a.asset", 3, 1)).HasValue());
    REQUIRE(builder.Add(kNote, "note", At("z.asset")).HasValue());
    REQUIRE(builder.Add(kFatal, "fatal", At("a.asset", 1, 1)).HasValue());
    REQUIRE(builder.SubmittedCount() == 4);

    const Result<ValidationResult> completed = builder.Complete();
    REQUIRE(completed.HasValue());
    const ValidationResult &result = completed.Value();
    REQUIRE(result.Size() == 4);
    REQUIRE_FALSE(result.Empty());
    REQUIRE(result.HasErrors());
    REQUIRE(result.Count(DiagnosticSeverity::Note) == 1);
    REQUIRE(result.Count(DiagnosticSeverity::Warning) == 1);
    REQUIRE(result.Count(DiagnosticSeverity::Error) == 1);
    REQUIRE(result.Count(DiagnosticSeverity::Fatal) == 1);
    REQUIRE(result.Diagnostics()[0].code.Value() == "asset.validation.error");
    REQUIRE(result.Diagnostics()[1].code.Value() == "asset.validation.fatal");
    REQUIRE(result.Diagnostics()[2].code.Value() == "asset.validation.note");
    REQUIRE(result.Diagnostics()[3].code.Value() == "asset.validation.warning");
    REQUIRE(result.Diagnostics()[0].location.source == "a.asset");
    REQUIRE(result.DuplicateCount() == 0);
}

TEST_CASE("Validation pass removes only exact duplicates", "[unit][foundation][validation][deduplication]") {
    ValidationResultBuilder builder = Builder();
    REQUIRE(builder.Add(kWarning, "Repeated", At("scene.horo", 4, 2)).HasValue());
    REQUIRE(builder.Add(kWarning, "Repeated", At("scene.horo", 4, 2)).HasValue());
    REQUIRE(builder.Add(kWarning, "Different detail", At("scene.horo", 4, 2)).HasValue());
    REQUIRE(builder.Add(kWarning, "Repeated", At("scene.horo", 5, 2)).HasValue());

    const Result<ValidationResult> completed = builder.Complete();
    REQUIRE(completed.HasValue());
    REQUIRE(completed.Value().Size() == 3);
    REQUIRE(completed.Value().DuplicateCount() == 1);
    REQUIRE_FALSE(completed.Value().HasErrors());
}

TEST_CASE("Validation pass uses registry-owned descriptor data", "[unit][foundation][validation][registry]") {
    ValidationResultBuilder builder = Builder();
    ErrorCodeDescriptor borrowed{.domain = ErrorDomainId{"horo.asset.validation"},
                                 .code = ErrorCode{"asset.validation.warning"},
                                 .defaultSeverity = ErrorSeverity::Critical,
                                 .summary = "Untrusted caller summary."};

    REQUIRE(builder.Add(borrowed, {}, At("mesh.fbx")).HasValue());
    const Result<ValidationResult> completed = builder.Complete();
    REQUIRE(completed.HasValue());
    REQUIRE(completed.Value().Diagnostics().front().severity == DiagnosticSeverity::Warning);
    REQUIRE(completed.Value().Diagnostics().front().message == "Asset validation warning.");
    REQUIRE(completed.Value().Diagnostics().front().domain.Value() == "horo.asset.validation");
}

TEST_CASE("Validation pass supports an empty successful full pass", "[unit][foundation][validation][boundary]") {
    ValidationResultBuilder builder = Builder();
    const Result<ValidationResult> completed = builder.Complete();
    REQUIRE(completed.HasValue());
    REQUIRE(completed.Value().Empty());
    REQUIRE(completed.Value().Diagnostics().empty());
    REQUIRE_FALSE(completed.Value().HasErrors());
}

TEST_CASE("Validation pass rejects unknown identities and preserves the first failure", "[unit][foundation][validation][failure]") {
    ValidationResultBuilder builder = Builder();
    const ErrorCodeDescriptor unknown{.domain = ErrorDomainId{"horo.asset.validation"},
                                      .code = ErrorCode{"asset.validation.unknown"},
                                      .defaultSeverity = ErrorSeverity::Error,
                                      .summary = "Unknown."};

    const Result<void> admission = builder.Add(unknown, "unknown", At("asset.bin"));
    REQUIRE(admission.HasError());
    REQUIRE(admission.ErrorValue().code.Value() == "foundation.validation.unknown_diagnostic");
    REQUIRE(builder.Add(kError, "later", At("asset.bin")).ErrorValue().code.Value() == "foundation.validation.pass_closed");

    const Result<ValidationResult> completed = builder.Complete();
    REQUIRE(completed.HasError());
    REQUIRE(completed.ErrorValue().code.Value() == "foundation.validation.unknown_diagnostic");
}

TEST_CASE("Validation pass validates source context", "[unit][foundation][validation][source]") {
    SECTION("source is required") {
        ValidationResultBuilder builder = Builder();
        REQUIRE(builder.Add(kError, "missing source", {}).ErrorValue().code.Value() == "foundation.validation.invalid_source");
    }
    SECTION("column requires a line") {
        ValidationResultBuilder builder = Builder();
        REQUIRE(builder.Add(kError, "orphan column", At("asset.bin", 0, 2)).ErrorValue().code.Value() ==
                "foundation.validation.invalid_source");
    }
    SECTION("whole-source and line-only locations are valid") {
        ValidationResultBuilder builder = Builder();
        REQUIRE(builder.Add(kNote, "whole source", At("asset.bin")).HasValue());
        REQUIRE(builder.Add(kNote, "line", At("asset.bin", 7)).HasValue());
        REQUIRE(builder.Complete().HasValue());
    }
}

TEST_CASE("Validation pass enforces bounded collection", "[unit][foundation][validation][capacity]") {
    REQUIRE(ValidationResultBuilder::Create(Registry(), {.maximumDiagnostics = 0}).ErrorValue().code.Value() ==
            "foundation.validation.invalid_limits");
    REQUIRE(ValidationResultBuilder::Create(Registry(), {.maximumDiagnostics = ValidationResultLimits::HardMaximumDiagnostics + 1})
                .ErrorValue()
                .code.Value() == "foundation.validation.invalid_limits");

    ValidationResultBuilder builder = Builder(2);
    REQUIRE(builder.Add(kNote, "one", At("a.asset")).HasValue());
    REQUIRE(builder.Add(kWarning, "two", At("b.asset")).HasValue());
    const Result<void> overflow = builder.Add(kError, "three", At("c.asset"));
    REQUIRE(overflow.HasError());
    REQUIRE(overflow.ErrorValue().code.Value() == "foundation.validation.capacity_exceeded");
    REQUIRE(builder.SubmittedCount() == 2);
    REQUIRE(builder.Complete().ErrorValue().code.Value() == "foundation.validation.capacity_exceeded");
}

TEST_CASE("Completed validation passes are terminal", "[unit][foundation][validation][lifecycle]") {
    ValidationResultBuilder builder = Builder();
    REQUIRE(builder.Add(kNote, "done", At("asset.bin")).HasValue());
    REQUIRE(builder.Complete().HasValue());
    REQUIRE(builder.SubmittedCount() == 1);
    REQUIRE(builder.Add(kWarning, "late", At("asset.bin")).ErrorValue().code.Value() == "foundation.validation.pass_closed");
    REQUIRE(builder.Complete().ErrorValue().code.Value() == "foundation.validation.pass_closed");
}

TEST_CASE("Single-failure Result semantics remain independent", "[unit][foundation][validation][result]") {
    const Result<int> failed = Result<int>::Failure(MakeError(kError, "operation failed"));
    REQUIRE(failed.HasError());
    REQUIRE(failed.ErrorValue().code.Value() == "asset.validation.error");
    REQUIRE(failed.ErrorValue().diagnostics.empty());
}
