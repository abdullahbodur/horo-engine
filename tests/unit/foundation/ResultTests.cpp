#include <catch2/catch_test_macros.hpp>

#include "Horo/Foundation/Diagnostics.h"
#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"

#include <string>

namespace
{
static_assert(noexcept(Horo::Result<void>::Success()),
              "A successful void Result must not allocate or throw on renderer hot paths.");

TEST_CASE("Result Preserves Successful Value", "[unit][foundation]")
{
    const Horo::Result<int> result = Horo::Result<int>::Success(42);
    REQUIRE((result.HasValue()));
    REQUIRE((!result.HasError()));
    REQUIRE((result.Value() == 42));
}

TEST_CASE("Result Preserves Typed Failure", "[unit][foundation]")
{
    const Horo::Error error{
        .code = Horo::ErrorCode{"foundation.test.failed"},
        .domain = Horo::ErrorDomainId{"horo.foundation.test"},
        .severity = Horo::ErrorSeverity::Error,
        .message = "Expected test failure",
    };
    const Horo::Result<int> result = Horo::Result<int>::Failure(error);
    REQUIRE((!result.HasValue()));
    REQUIRE((result.HasError()));
    REQUIRE((result.ErrorValue().code.Value() == "foundation.test.failed"));
    REQUIRE((result.ErrorValue().domain.Value() == "horo.foundation.test"));
}

TEST_CASE("Result Void Models Success And Failure", "[unit][foundation]")
{
    const Horo::Result<void> success = Horo::Result<void>::Success();
    REQUIRE((success.HasValue()));

    const Horo::Result<void> failure = Horo::Result<void>::Failure(Horo::Error{
        .code = Horo::ErrorCode{"foundation.test.void_failure"},
        .domain = Horo::ErrorDomainId{"horo.foundation.test"},
        .severity = Horo::ErrorSeverity::Warning,
        .message = "Expected void failure",
    });
    REQUIRE((failure.HasError()));
    REQUIRE((failure.ErrorValue().severity == Horo::ErrorSeverity::Warning));
}

TEST_CASE("Error Factory Uses Declared Descriptor Identity", "[unit][foundation]")
{
    const Horo::ErrorCodeDescriptor descriptor{
        .domain = Horo::ErrorDomainId{"horo.foundation.test"},
        .code = Horo::ErrorCode{"foundation.test.declared_failure"},
        .defaultSeverity = Horo::ErrorSeverity::Warning,
        .summary = "Declared failure",
        .remediationHint = "Use a valid test input.",
        .retryable = false,
        .userActionable = true,
    };

    const Horo::Error error = Horo::MakeError(descriptor, "Specific failure context");
    REQUIRE((error.domain.Value() == "horo.foundation.test"));
    REQUIRE((error.code.Value() == "foundation.test.declared_failure"));
    REQUIRE((error.severity == Horo::ErrorSeverity::Warning));
    REQUIRE((error.message == "Specific failure context"));

    const Horo::Error fallback = Horo::MakeError(descriptor);
    REQUIRE((fallback.message == "Declared failure"));
}

TEST_CASE("Diagnostic Carries Stable Identity And Location", "[unit][foundation]")
{
    const Horo::Diagnostic diagnostic{
        .code = Horo::DiagnosticCode{"foundation.test.invalid_value"},
        .severity = Horo::DiagnosticSeverity::Error,
        .message = "Value is invalid",
        .location = Horo::SourceLocation{.source = "settings.json", .line = 7, .column = 3},
    };
    REQUIRE((diagnostic.code.Value() == "foundation.test.invalid_value"));
    REQUIRE((diagnostic.location.source == "settings.json"));
    REQUIRE((diagnostic.location.line == 7));
}
} // namespace
