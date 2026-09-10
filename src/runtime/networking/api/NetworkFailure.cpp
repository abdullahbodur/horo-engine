#include "Horo/Network/NetworkFailure.h"

#include "Horo/Network/NetworkErrors.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace Horo::Network {
    namespace {
        struct Classification final {
            NetworkFailureLayer layer;
            NetworkFailureDisposition disposition;
            const ErrorCodeDescriptor *descriptor;
        };

        /** @brief Resolves the single canonical classification for a terminal kind. */
        std::optional<Classification> Classify(const NetworkFailureKind kind) noexcept {
            using enum NetworkFailureLayer;
            static const std::array classifications{
                Classification{Transport, NetworkFailureDisposition::Retryable, &NetworkErrors::NameResolutionFailed},
                Classification{Transport, NetworkFailureDisposition::Retryable, &NetworkErrors::TransportCapabilityUnavailable},
                Classification{Transport, NetworkFailureDisposition::Retryable, &NetworkErrors::PacketQueueFull},
                Classification{Protocol, NetworkFailureDisposition::RemoteRejection, &NetworkErrors::MessageEnvelopeInvalid},
                Classification{Protocol, NetworkFailureDisposition::Incompatible, &NetworkErrors::ProtocolVersionIncompatible},
                Classification{Session, NetworkFailureDisposition::LocalPolicy, &NetworkErrors::SessionPolicyRejected},
                Classification{Session, NetworkFailureDisposition::RemoteRejection, &NetworkErrors::SessionRemoteRejected},
                Classification{Session, NetworkFailureDisposition::LocalPolicy, &NetworkErrors::SessionCancelled},
                Classification{Session, NetworkFailureDisposition::Retryable, &NetworkErrors::SessionTimedOut},
                Classification{Session, NetworkFailureDisposition::LocalPolicy, &NetworkErrors::SessionShuttingDown},
                Classification{Replication, NetworkFailureDisposition::Incompatible, &NetworkErrors::ReplicationDescriptorIncompatible},
                Classification{GameplayDispatch, NetworkFailureDisposition::RemoteRejection, &NetworkErrors::GameplayDispatchRejected},
                Classification{Count, NetworkFailureDisposition::Fatal, &NetworkErrors::FatalFailure},
            };
            const auto index = static_cast<std::size_t>(kind);
            return index < classifications.size() ? std::optional{classifications[index]} : std::nullopt;
        }

        using ContextValidator = bool (*)(const NetworkFailureContextValue &);

        /** @brief Validates a transport-handle diagnostic context value. */
        bool ValidConnectionContext(const NetworkFailureContextValue &value) noexcept {
            const auto *handle = std::get_if<TransportHandleDiagnostic>(&value);
            return handle != nullptr && handle->slot != std::numeric_limits<std::uint32_t>::max() && handle->generation != 0;
        }

        /** @brief Validates a protocol identity context value. */
        bool ValidProtocolContext(const NetworkFailureContextValue &value) noexcept {
            const auto *identity = std::get_if<ProtocolId>(&value);
            return identity != nullptr && identity->IsValid();
        }

        /** @brief Validates a close-reason identity context value. */
        bool ValidCloseReasonContext(const NetworkFailureContextValue &value) noexcept {
            const auto *identity = std::get_if<CloseReasonId>(&value);
            return identity != nullptr && identity->IsValid();
        }

        /** @brief Validates a replicated-object identity context value. */
        bool ValidNetworkObjectContext(const NetworkFailureContextValue &value) noexcept {
            const auto *identity = std::get_if<NetworkObjectId>(&value);
            return identity != nullptr && identity->IsValid();
        }

        /** @brief Validates a non-zero scalar context value. */
        bool ValidNonZeroScalarContext(const NetworkFailureContextValue &value) noexcept {
            const auto *scalar = std::get_if<std::uint64_t>(&value);
            return scalar != nullptr && *scalar != 0;
        }

        /** @brief Validates a message-type identity context value. */
        bool ValidMessageTypeContext(const NetworkFailureContextValue &value) noexcept {
            const auto *identity = std::get_if<MessageTypeId>(&value);
            return identity != nullptr && identity->IsValid();
        }

        /** @brief Validates a scalar context value that permits zero. */
        bool ValidScalarContext(const NetworkFailureContextValue &value) noexcept {
            return std::holds_alternative<std::uint64_t>(value);
        }

        /** @brief Validates one context value against its closed key vocabulary. */
        bool ContextValueIsValid(const NetworkFailureContextEntry &entry) noexcept {
            static const std::array<ContextValidator, static_cast<std::size_t>(NetworkFailureContextKey::Count)> validators{
                ValidConnectionContext,    ValidProtocolContext,      ValidCloseReasonContext, ValidNetworkObjectContext,
                ValidNonZeroScalarContext, ValidNonZeroScalarContext, ValidMessageTypeContext, ValidScalarContext,
                ValidScalarContext,        ValidNonZeroScalarContext,
            };
            const auto index = static_cast<std::size_t>(entry.key);
            return index < validators.size() && validators[index](entry.value);
        }

        struct Utf8Lead final {
            std::size_t continuationCount;
            std::uint32_t codepoint;
        };

        /** @brief Decodes the shape and initial scalar bits of a non-ASCII UTF-8 lead byte. */
        std::optional<Utf8Lead> DecodeUtf8Lead(const std::byte lead) noexcept {
            const auto value = std::to_integer<std::uint8_t>(lead);
            if (value >= 0xc2U && value <= 0xdfU)
                return Utf8Lead{1, std::to_integer<std::uint8_t>(lead & std::byte{0x1f})};
            if (value >= 0xe0U && value <= 0xefU)
                return Utf8Lead{2, std::to_integer<std::uint8_t>(lead & std::byte{0x0f})};
            if (value >= 0xf0U && value <= 0xf4U)
                return Utf8Lead{3, std::to_integer<std::uint8_t>(lead & std::byte{0x07})};
            return std::nullopt;
        }

        /** @brief Reads one string byte without assigning byte protocol meaning to character data. */
        std::byte ByteAt(const std::string_view text, const std::size_t index) noexcept {
            return static_cast<std::byte>(static_cast<unsigned char>(text[index]));
        }

        /** @brief Rejects overlong, surrogate, and out-of-range decoded Unicode scalars. */
        bool IsCanonicalUtf8Scalar(const std::uint32_t codepoint, const std::size_t continuationCount) noexcept {
            if (continuationCount == 2)
                return codepoint >= 0x800U && !(codepoint >= 0xd800U && codepoint <= 0xdfffU);
            if (continuationCount == 3)
                return codepoint >= 0x10000U && codepoint <= 0x10ffffU;
            return true;
        }

        /** @brief Decodes one bounded non-ASCII UTF-8 scalar and returns its byte width. */
        std::optional<std::size_t> DecodeUtf8Scalar(const std::string_view text, const std::size_t index) noexcept {
            const auto decodedLead = DecodeUtf8Lead(ByteAt(text, index));
            if (!decodedLead.has_value() || index + decodedLead->continuationCount >= text.size())
                return std::nullopt;
            std::uint32_t codepoint = decodedLead->codepoint;
            for (std::size_t offset = 1; offset <= decodedLead->continuationCount; ++offset) {
                const auto continuation = ByteAt(text, index + offset);
                if ((continuation & std::byte{0xc0}) != std::byte{0x80})
                    return std::nullopt;
                codepoint = (codepoint << 6U) | std::to_integer<std::uint8_t>(continuation & std::byte{0x3f});
            }
            if (!IsCanonicalUtf8Scalar(codepoint, decodedLead->continuationCount))
                return std::nullopt;
            return decodedLead->continuationCount + 1;
        }

        /** @brief Checks a bounded prefix for printable canonical UTF-8 without retaining it. */
        bool IsPrintableUtf8(const std::string_view text) noexcept {
            std::size_t index = 0;
            while (index < text.size()) {
                if (const auto lead = std::to_integer<std::uint8_t>(ByteAt(text, index)); lead < 0x80U) {
                    if (lead < 0x20U || lead == 0x7fU)
                        return false;
                    ++index;
                    continue;
                }
                const auto scalarBytes = DecodeUtf8Scalar(text, index);
                if (!scalarBytes.has_value())
                    return false;
                index += *scalarBytes;
            }
            return true;
        }

        /** @brief Validates ordered typed context and close-reason protocol provenance. */
        bool ContextIsValid(const std::span<const NetworkFailureContextEntry> context) noexcept {
            if (context.size() > MaximumNetworkFailureContextEntries)
                return false;
            std::optional<NetworkFailureContextKey> previous;
            bool hasProtocol = false;
            for (const auto &entry : context) {
                if ((previous.has_value() && entry.key <= *previous) || !ContextValueIsValid(entry))
                    return false;
                hasProtocol = hasProtocol || entry.key == NetworkFailureContextKey::Protocol;
                if (entry.key == NetworkFailureContextKey::CloseReason && !hasProtocol)
                    return false;
                previous = entry.key;
            }
            return true;
        }

        /** @brief Prevents safe diagnostic context from naming a different connection generation. */
        bool ContextMatchesConnection(const NetworkTerminalRecord &terminal, const ConnectionHandle connection) noexcept {
            const auto expected = connection.Diagnostic();
            for (const auto &entry : terminal.Context()) {
                if (entry.key == NetworkFailureContextKey::Connection)
                    return std::get<TransportHandleDiagnostic>(entry.value) == expected;
            }
            return true;
        }
    }  // namespace

    /** @copydoc NetworkTerminalRecord::Descriptor */
    const ErrorCodeDescriptor &NetworkTerminalRecord::Descriptor() const noexcept {
        const auto classification = Classify(kind_);
        return classification.has_value() ? *classification->descriptor : NetworkErrors::TerminalRecordInvalid;
    }

    /** @copydoc NetworkTerminalRecord::ToError */
    Error NetworkTerminalRecord::ToError() const {
        return MakeError(Descriptor());
    }

    /** @copydoc MakeNetworkTerminalRecord */
    Result<NetworkTerminalRecord> MakeNetworkTerminalRecord(const NetworkFailureLayer layer, const NetworkFailureKind kind,
                                                            const std::span<const NetworkFailureContextEntry> context) {
        const auto classification = Classify(kind);
        const bool layerKnown = static_cast<std::uint8_t>(layer) < static_cast<std::uint8_t>(NetworkFailureLayer::Count);
        if (const bool layerIsValid =
                layerKnown && (kind == NetworkFailureKind::FatalInternal || (classification.has_value() && classification->layer == layer));
            !classification.has_value() || !layerIsValid || !ContextIsValid(context))
            return Result<NetworkTerminalRecord>::Failure(MakeError(NetworkErrors::TerminalRecordInvalid));

        NetworkTerminalRecord record;
        record.layer_ = layer;
        record.kind_ = kind;
        record.disposition_ = classification->disposition;
        record.contextCount_ = context.size();
        std::ranges::copy(context, record.context_.begin());
        return Result<NetworkTerminalRecord>::Success(std::move(record));
    }

    /** @copydoc NormalizePrivateBackendFailure */
    Result<NetworkTerminalRecord> NormalizePrivateBackendFailure(const NetworkFailureLayer layer, const NetworkFailureKind kind,
                                                                 const std::string_view privateDetail,
                                                                 const std::span<const NetworkFailureContextEntry> context,
                                                                 const bool instrumentationEnabled) {
        auto result = MakeNetworkTerminalRecord(layer, kind, context);
        if (result.HasError() || !instrumentationEnabled)
            return result;

        NetworkTerminalRecord record = std::move(result).Value();
        record.backendEvidence_.observed = !privateDetail.empty();
        record.backendEvidence_.truncated = privateDetail.size() > MaximumPrivateBackendDetailBytes;
        const std::size_t inspectedSize = std::min(privateDetail.size(), MaximumPrivateBackendDetailBytes);
        record.backendEvidence_.observedBytes =
            static_cast<std::uint16_t>(std::min(privateDetail.size(), MaximumPrivateBackendDetailBytes + 1));
        record.backendEvidence_.malformed = !IsPrintableUtf8(privateDetail.substr(0, inspectedSize));
        return Result<NetworkTerminalRecord>::Success(std::move(record));
    }

    /** @copydoc NetworkTerminalOwner::Create */
    Result<NetworkTerminalOwner> NetworkTerminalOwner::Create(const ConnectionHandle connection) {
        if (!connection.IsValid())
            return Result<NetworkTerminalOwner>::Failure(MakeError(NetworkErrors::TerminalRecordInvalid));
        return Result<NetworkTerminalOwner>::Success(NetworkTerminalOwner{connection});
    }

    /** @copydoc NetworkTerminalOwner::Resolve */
    Result<void> NetworkTerminalOwner::Resolve(const ConnectionHandle observed, const NetworkTerminalRecord &terminal) {
        if (observed != connection_ || !ContextMatchesConnection(terminal, observed))
            return Result<void>::Failure(MakeError(NetworkErrors::TerminalGenerationStale));
        if (!accepting_ || terminal_.has_value())
            return Result<void>::Failure(MakeError(NetworkErrors::TerminalAlreadyResolved));
        terminal_ = terminal;
        accepting_ = false;
        return Result<void>::Success();
    }

    /** @copydoc NetworkTerminalOwner::Replace */
    Result<void> NetworkTerminalOwner::Replace(const ConnectionHandle retired, const ConnectionHandle replacement) {
        if (retired != connection_ || accepting_ || !terminal_.has_value())
            return Result<void>::Failure(MakeError(NetworkErrors::TerminalGenerationStale));
        auto expected = connection_.NextGeneration();
        if (expected.HasError())
            return Result<void>::Failure(expected.ErrorValue());
        if (replacement != expected.Value())
            return Result<void>::Failure(MakeError(NetworkErrors::TerminalGenerationStale));
        connection_ = replacement;
        terminal_.reset();
        accepting_ = true;
        return Result<void>::Success();
    }

    /** @copydoc NetworkTerminalOwner::Terminal */
    const NetworkTerminalRecord *NetworkTerminalOwner::Terminal() const noexcept {
        return terminal_.has_value() ? &*terminal_ : nullptr;
    }
}  // namespace Horo::Network
