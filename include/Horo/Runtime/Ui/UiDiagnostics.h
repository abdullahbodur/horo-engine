#pragma once

/**
 * @file UiDiagnostics.h
 * @brief Backend-neutral bounded Runtime UI diagnostic evidence.
 */

#include "Horo/Foundation/Diagnostics.h"
#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Ui/UiIdentity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace Horo::Runtime::Ui {
    /** @brief Maximum owned correlation entries retained by one Runtime UI diagnostic record. */
    inline constexpr std::size_t MaximumUiDiagnosticCorrelationEntries = 6;
    /** @brief Maximum UTF-8 byte count admitted for one Runtime UI diagnostic message. */
    inline constexpr std::size_t MaximumUiDiagnosticMessageBytes = 512;

    /** @brief Stable Runtime UI failure area used for presentation and filtering. */
    enum class UiDiagnosticCategory : std::uint8_t {
        Document,
        Layout,
        Text,
        Input,
        Focus,
        Binding,
        Render,
        Accessibility,
        Lifecycle,
    };

    /** @brief Closed typed vocabulary for safe Runtime UI diagnostic correlation. */
    enum class UiDiagnosticCorrelationKey : std::uint8_t {
        Document,
        Element,
        Canvas,
        Player,
        Viewport,
        Operation,
    };

    /**
     * @brief Owned Runtime UI identity or dependency-neutral scalar projection allowed in diagnostic correlation.
     *
     * Player values project the canonical Input player slot into uint64_t and must fit uint8_t. Viewport values
     * project the non-zero host-supplied view identity. Operation values use the non-zero Foundation OperationId
     * representation. These scalar projections are evidence only and do not define replacement identity domains.
     */
    using UiDiagnosticCorrelationValue = std::variant<UiDocumentId, UiElementId, UiCanvasId, std::uint64_t>;

    /** @brief One typed correlation field; records require fields in strictly increasing key order. */
    struct UiDiagnosticCorrelationEntry final {
        UiDiagnosticCorrelationKey key{UiDiagnosticCorrelationKey::Document};
        UiDiagnosticCorrelationValue value;
    };

    /**
     * @brief Owned inert Runtime UI failure evidence for result, observability and support projections.
     *
     * This value owns its message and bounded correlation. It carries no renderer or platform handles, resource
     * leases, store references, logging authority, registration authority or runtime mutation capability.
     */
    struct UiDiagnosticRecord final {
        std::uint32_t schemaVersion{1};
        UiDiagnosticCategory category{UiDiagnosticCategory::Document};
        DiagnosticCode code;
        DiagnosticSeverity severity{DiagnosticSeverity::Error};
        std::string message;
        std::array<UiDiagnosticCorrelationEntry, MaximumUiDiagnosticCorrelationEntries> correlation;
        std::uint8_t correlationCount{};
    };

    /**
     * @brief Returns the stable dotted presentation name derived from a known category.
     * @param category Closed category value to render.
     * @return Stable process-lifetime name, or an empty view for an unknown enum representation.
     */
    [[nodiscard]] std::string_view UiDiagnosticCategoryName(UiDiagnosticCategory category) noexcept;

    /**
     * @brief Returns the single canonical descriptor table admitted as Runtime UI diagnostic sources.
     * @return Borrowed immutable process-lifetime descriptor pointers in declaration order.
     */
    [[nodiscard]] std::span<const ErrorCodeDescriptor *const> UiDiagnosticErrorDescriptors() noexcept;

    /**
     * @brief Creates bounded owned diagnostic evidence from one canonical Runtime UI error.
     * @param category Closed Runtime UI failure area; its stable name is derived, never parsed as input.
     * @param error Canonical `horo.runtime_ui` operation error whose code is mapped explicitly.
     * @param correlation Borrowed fields in strictly increasing key order; copied on success only.
     * @return Owned record, UiErrors::DiagnosticUnsupported for an unknown category or source error, or
     * UiErrors::DiagnosticInvalid for malformed, oversized, unordered, duplicate or type-incompatible input.
     * @pre Control, validation or adapter boundary; record/error construction may allocate.
     * @post Inputs are unchanged. Failure publishes no partial record, log, event or runtime state.
     */
    [[nodiscard]] Result<UiDiagnosticRecord> MakeUiDiagnosticRecord(UiDiagnosticCategory category, const Error &error,
                                                                    std::span<const UiDiagnosticCorrelationEntry> correlation = {});
}  // namespace Horo::Runtime::Ui
