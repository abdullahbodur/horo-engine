#include "Horo/Foundation/ValidationResult.h"

#include "foundation/FoundationErrors.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace Horo {
    namespace {
        [[nodiscard]] auto SortKey(const ValidationDiagnostic &diagnostic) noexcept {
            return std::tuple{std::string_view{diagnostic.domain.Value()},
                              std::string_view{diagnostic.code.Value()},
                              diagnostic.severity,
                              std::string_view{diagnostic.location.source},
                              diagnostic.location.line,
                              diagnostic.location.column,
                              std::string_view{diagnostic.message}};
        }

        [[nodiscard]] bool IsExactDuplicate(const ValidationDiagnostic &lhs, const ValidationDiagnostic &rhs) noexcept {
            return SortKey(lhs) == SortKey(rhs);
        }

        [[nodiscard]] bool HasValidSource(const SourceLocation &location) noexcept {
            return !location.source.empty() && (location.line != 0 || location.column == 0);
        }
    }  // namespace

    ValidationResult::ValidationResult(std::vector<ValidationDiagnostic> diagnostics, const std::size_t duplicateCount) noexcept
        : diagnostics_(std::move(diagnostics)), duplicateCount_(duplicateCount) {}

    /** @copydoc ValidationResult::Diagnostics */
    std::span<const ValidationDiagnostic> ValidationResult::Diagnostics() const noexcept {
        return diagnostics_;
    }

    /** @copydoc ValidationResult::Size */
    std::size_t ValidationResult::Size() const noexcept {
        return diagnostics_.size();
    }

    /** @copydoc ValidationResult::Empty */
    bool ValidationResult::Empty() const noexcept {
        return diagnostics_.empty();
    }

    /** @copydoc ValidationResult::Count */
    std::size_t ValidationResult::Count(const DiagnosticSeverity severity) const noexcept {
        return static_cast<std::size_t>(std::ranges::count(diagnostics_, severity, &ValidationDiagnostic::severity));
    }

    /** @copydoc ValidationResult::HasErrors */
    bool ValidationResult::HasErrors() const noexcept {
        return std::ranges::any_of(diagnostics_, [](const ValidationDiagnostic &diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error || diagnostic.severity == DiagnosticSeverity::Fatal;
        });
    }

    /** @copydoc ValidationResult::DuplicateCount */
    std::size_t ValidationResult::DuplicateCount() const noexcept {
        return duplicateCount_;
    }

    ValidationResultBuilder::ValidationResultBuilder(ErrorCodeRegistry registry, const ValidationResultLimits limits) noexcept
        : registry_(std::move(registry)), limits_(limits) {}

    /** @copydoc ValidationResultBuilder::Create */
    Result<ValidationResultBuilder> ValidationResultBuilder::Create(ErrorCodeRegistry registry, const ValidationResultLimits limits) {
        if (limits.maximumDiagnostics == 0 || limits.maximumDiagnostics > ValidationResultLimits::HardMaximumDiagnostics) {
            return Result<ValidationResultBuilder>::Failure(
                MakeError(ValidationErrors::InvalidLimits, "Validation diagnostic capacity must be within the documented hard limit."));
        }
        return Result<ValidationResultBuilder>::Success(ValidationResultBuilder{std::move(registry), limits});
    }

    /** @copydoc ValidationResultBuilder::Add */
    Result<void> ValidationResultBuilder::Add(const ErrorCodeDescriptor &descriptor, std::string message, SourceLocation location) {
        if (state_ != State::Active)
            return Result<void>::Failure(MakeError(ValidationErrors::PassClosed));
        if (diagnostics_.size() >= limits_.maximumDiagnostics)
            return Fail(MakeError(ValidationErrors::CapacityExceeded));

        const ErrorCodeDescriptor *registered = registry_.Resolve(descriptor.domain, descriptor.code);
        if (registered == nullptr)
            return Fail(MakeError(ValidationErrors::UnknownDiagnostic));
        if (!HasValidSource(location))
            return Fail(MakeError(ValidationErrors::InvalidSource));

        const std::optional<DiagnosticSeverity> severity = DiagnosticSeverityForError(registered->defaultSeverity);
        if (!severity.has_value())
            return Fail(MakeError(ValidationErrors::UnknownDiagnostic));
        if (message.empty())
            message.assign(registered->summary);

        diagnostics_.push_back({.domain = registered->domain,
                                .code = registered->code,
                                .severity = *severity,
                                .message = std::move(message),
                                .location = std::move(location)});
        ++submittedCount_;
        return Result<void>::Success();
    }

    /** @copydoc ValidationResultBuilder::Complete */
    Result<ValidationResult> ValidationResultBuilder::Complete() {
        if (state_ == State::Failed) {
            state_ = State::Completed;
            return Result<ValidationResult>::Failure(std::move(failure_));
        }
        if (state_ == State::Completed)
            return Result<ValidationResult>::Failure(MakeError(ValidationErrors::PassClosed));

        state_ = State::Completed;
        std::ranges::sort(diagnostics_, {}, &SortKey);
        const auto uniqueEnd = std::ranges::unique(diagnostics_, IsExactDuplicate).begin();
        const std::size_t duplicateCount = static_cast<std::size_t>(diagnostics_.end() - uniqueEnd);
        diagnostics_.erase(uniqueEnd, diagnostics_.end());
        return Result<ValidationResult>::Success(ValidationResult{std::move(diagnostics_), duplicateCount});
    }

    /** @copydoc ValidationResultBuilder::SubmittedCount */
    std::size_t ValidationResultBuilder::SubmittedCount() const noexcept {
        return submittedCount_;
    }

    Result<void> ValidationResultBuilder::Fail(Error error) {
        failure_ = std::move(error);
        state_ = State::Failed;
        return Result<void>::Failure(failure_);
    }
}  // namespace Horo
