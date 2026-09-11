#pragma once

/**
 * @file ValidationResult.h
 * @brief Registry-validated multi-diagnostic results for full-pass validation.
 */

#include "Horo/Foundation/ErrorCodeRegistry.h"
#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace Horo {
    /** @brief Resource limits applied while one validation pass collects findings. */
    struct ValidationResultLimits {
        static constexpr std::size_t DefaultMaximumDiagnostics = 4096;
        static constexpr std::size_t HardMaximumDiagnostics = 65536;

        std::size_t maximumDiagnostics{DefaultMaximumDiagnostics}; /**< Maximum submitted findings, including duplicates. */
    };

    /** @brief One registry-resolved validation finding with owned source context. */
    struct ValidationDiagnostic {
        ErrorDomainId domain;          /**< Stable registered error domain. */
        ErrorCode code;                /**< Stable registered error code. */
        DiagnosticSeverity severity{}; /**< Severity derived from the registered descriptor. */
        std::string message;           /**< Finding-specific developer-facing detail. */
        SourceLocation location;       /**< Owned user-visible source context. */
    };

    /** @brief Immutable, deterministically ordered findings produced by one completed validation pass. */
    class ValidationResult final {
    public:
        /**
         * @brief Returns the completed pass findings.
         * @return Borrowed contiguous view valid for the lifetime of this result.
         */
        [[nodiscard]] std::span<const ValidationDiagnostic> Diagnostics() const noexcept;

        /** @brief Returns the number of unique findings in this result. */
        [[nodiscard]] std::size_t Size() const noexcept;

        /** @brief Returns whether the completed pass has no findings. */
        [[nodiscard]] bool Empty() const noexcept;

        /**
         * @brief Returns the number of findings at one severity.
         * @param severity Severity to count.
         * @return Number of matching findings.
         */
        [[nodiscard]] std::size_t Count(DiagnosticSeverity severity) const noexcept;

        /** @brief Returns whether at least one error or fatal finding is present. */
        [[nodiscard]] bool HasErrors() const noexcept;

        /** @brief Returns how many exact duplicate submissions were removed. */
        [[nodiscard]] std::size_t DuplicateCount() const noexcept;

    private:
        explicit ValidationResult(std::vector<ValidationDiagnostic> diagnostics, std::size_t duplicateCount) noexcept;

        std::vector<ValidationDiagnostic> diagnostics_;
        std::size_t duplicateCount_{};

        friend class ValidationResultBuilder;
    };

    /**
     * @brief Bounded full-pass collector that resolves every finding through one immutable registry snapshot.
     *
     * The builder owns a copy of the immutable registry snapshot. Validation producers may therefore
     * outlive the host's current snapshot publication, but must keep the builder on one owning thread.
     * A failed admission poisons the pass; Complete then returns the same typed operation failure.
     */
    class ValidationResultBuilder final {
    public:
        /**
         * @brief Creates an active validation pass.
         * @param registry Immutable registry snapshot used for all finding identities.
         * @param limits Explicit bounded-work policy for the pass.
         * @return Active builder, or a typed failure when limits are invalid.
         */
        [[nodiscard]] static Result<ValidationResultBuilder> Create(ErrorCodeRegistry registry, ValidationResultLimits limits = {});

        ValidationResultBuilder(const ValidationResultBuilder &) = delete;
        ValidationResultBuilder &operator=(const ValidationResultBuilder &) = delete;
        ValidationResultBuilder(ValidationResultBuilder &&) noexcept = default;
        ValidationResultBuilder &operator=(ValidationResultBuilder &&) noexcept = default;

        /**
         * @brief Adds one validation finding without ending the full pass.
         * @param descriptor Module-owned descriptor whose exact identity must exist in the captured registry.
         * @param message Finding-specific detail; the registered summary is used when empty.
         * @param location Required source context. A column requires a non-zero line.
         * @return Success when admitted, or a typed failure for unknown identity, malformed source, capacity, or closed state.
         * @note Exact duplicates are accepted here and removed deterministically by Complete.
         */
        [[nodiscard]] Result<void> Add(const ErrorCodeDescriptor &descriptor, std::string message, SourceLocation location);

        /**
         * @brief Completes the pass and canonicalizes all admitted findings.
         * @return Immutable validation result, or the pass failure when admission previously failed or the pass was already completed.
         * @post This builder is closed after the call, regardless of outcome.
         */
        [[nodiscard]] Result<ValidationResult> Complete();

        /** @brief Returns the number of findings submitted before completion or failure. */
        [[nodiscard]] std::size_t SubmittedCount() const noexcept;

    private:
        enum class State : unsigned char {
            Active,
            Failed,
            Completed,
        };

        ValidationResultBuilder(ErrorCodeRegistry registry, ValidationResultLimits limits) noexcept;
        [[nodiscard]] Result<void> Fail(Error error);

        ErrorCodeRegistry registry_;
        ValidationResultLimits limits_;
        std::vector<ValidationDiagnostic> diagnostics_;
        Error failure_;
        std::size_t submittedCount_{};
        State state_{State::Active};
    };
}  // namespace Horo
