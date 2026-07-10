#include "Horo/Foundation/Diagnostics.h"
#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"

#include <cassert>
#include <string>

namespace
{

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
    DiagnosticCarriesStableIdentityAndLocation();
    return 0;
}
