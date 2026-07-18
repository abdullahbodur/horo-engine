#include "Horo/Foundation/Diagnostics.h"
#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"

#include <cassert>
#include <string>

namespace
{
static_assert(noexcept(Horo::Result<void>::Success()),
              "A successful void Result must not allocate or throw on renderer hot paths.");

void ResultPreservesSuccessfulValue()
{
    const Horo::Result<int> result = Horo::Result<int>::Success(42);
    assert(result.HasValue());
    assert(!result.HasError());
    assert(result.Value() == 42);
}

void ResultPreservesTypedFailure()
{
    const Horo::Error error{
        .code = Horo::ErrorCode{"foundation.test.failed"},
        .domain = Horo::ErrorDomainId{"horo.foundation.test"},
        .severity = Horo::ErrorSeverity::Error,
        .message = "Expected test failure",
    };
    const Horo::Result<int> result = Horo::Result<int>::Failure(error);
    assert(!result.HasValue());
    assert(result.HasError());
    assert(result.ErrorValue().code.Value() == "foundation.test.failed");
    assert(result.ErrorValue().domain.Value() == "horo.foundation.test");
}

void ResultVoidModelsSuccessAndFailure()
{
    const Horo::Result<void> success = Horo::Result<void>::Success();
    assert(success.HasValue());

    const Horo::Result<void> failure = Horo::Result<void>::Failure(Horo::Error{
        .code = Horo::ErrorCode{"foundation.test.void_failure"},
        .domain = Horo::ErrorDomainId{"horo.foundation.test"},
        .severity = Horo::ErrorSeverity::Warning,
        .message = "Expected void failure",
    });
    assert(failure.HasError());
    assert(failure.ErrorValue().severity == Horo::ErrorSeverity::Warning);
}

void ErrorFactoryUsesDeclaredDescriptorIdentity()
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
    assert(error.domain.Value() == "horo.foundation.test");
    assert(error.code.Value() == "foundation.test.declared_failure");
    assert(error.severity == Horo::ErrorSeverity::Warning);
    assert(error.message == "Specific failure context");

    const Horo::Error fallback = Horo::MakeError(descriptor);
    assert(fallback.message == "Declared failure");
}


void DiagnosticCarriesStableIdentityAndLocation()
{
    const Horo::Diagnostic diagnostic{
        .code = Horo::DiagnosticCode{"foundation.test.invalid_value"},
        .severity = Horo::DiagnosticSeverity::Error,
        .message = "Value is invalid",
        .location = Horo::SourceLocation{.source = "settings.json", .line = 7, .column = 3},
    };
    assert(diagnostic.code.Value() == "foundation.test.invalid_value");
    assert(diagnostic.location.source == "settings.json");
    assert(diagnostic.location.line == 7);
}

} // namespace

int main()
{
    ResultPreservesSuccessfulValue();
    ResultPreservesTypedFailure();
    ResultVoidModelsSuccessAndFailure();
    ErrorFactoryUsesDeclaredDescriptorIdentity();
    DiagnosticCarriesStableIdentityAndLocation();
    return 0;
}
