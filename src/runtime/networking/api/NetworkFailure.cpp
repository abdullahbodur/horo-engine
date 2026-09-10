#include "Horo/Network/NetworkFailure.h"

#include "Horo/Network/NetworkErrors.h"

#include <algorithm>
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
            static const std::array classifications{
                Classification{NetworkFailureLayer::Transport, NetworkFailureDisposition::Retryable, &NetworkErrors::NameResolutionFailed},
                Classification{NetworkFailureLayer::Transport, NetworkFailureDisposition::Retryable,
                               &NetworkErrors::TransportCapabilityUnavailable},
                Classification{NetworkFailureLayer::Transport, NetworkFailureDisposition::Retryable, &NetworkErrors::PacketQueueFull},
                Classification{NetworkFailureLayer::Protocol, NetworkFailureDisposition::RemoteRejection,
                               &NetworkErrors::MessageEnvelopeInvalid},
                Classification{NetworkFailureLayer::Protocol, NetworkFailureDisposition::Incompatible,
                               &NetworkErrors::ProtocolVersionIncompatible},
                Classification{NetworkFailureLayer::Session, NetworkFailureDisposition::LocalPolicy, &NetworkErrors::SessionPolicyRejected},
                Classification{NetworkFailureLayer::Session, NetworkFailureDisposition::RemoteRejection,
                               &NetworkErrors::SessionRemoteRejected},
                Classification{NetworkFailureLayer::Session, NetworkFailureDisposition::LocalPolicy, &NetworkErrors::SessionCancelled},
                Classification{NetworkFailureLayer::Session, NetworkFailureDisposition::Retryable, &NetworkErrors::SessionTimedOut},
                Classification{NetworkFailureLayer::Session, NetworkFailureDisposition::LocalPolicy, &NetworkErrors::SessionShuttingDown},
                Classification{NetworkFailureLayer::Replication, NetworkFailureDisposition::Incompatible,
                               &NetworkErrors::ReplicationDescriptorIncompatible},
                Classification{NetworkFailureLayer::GameplayDispatch, NetworkFailureDisposition::RemoteRejection,
                               &NetworkErrors::GameplayDispatchRejected},
                Classification{NetworkFailureLayer::Count, NetworkFailureDisposition::Fatal, &NetworkErrors::FatalFailure},
            };
            const auto index = static_cast<std::size_t>(kind);
            return index < classifications.size() ? std::optional{classifications[index]} : std::nullopt;
        }

        /** @brief Validates one context value against its closed key vocabulary. */
        bool ContextValueIsValid(const NetworkFailureContextEntry &entry) noexcept {
            using Validator = bool (*)(const NetworkFailureContextValue &);
            static const std::array<Validator, static_cast<std::size_t>(NetworkFailureContextKey::Count)> validators{
                [](const auto &value) {
                const auto *handle = std::get_if<TransportHandleDiagnostic>(&value);
                return handle != nullptr && handle->slot != std::numeric_limits<std::uint32_t>::max() && handle->generation != 0;
            },
                [](const auto &value) {
                const auto *identity = std::get_if<ProtocolId>(&value);
                return identity != nullptr && identity->IsValid();
            },
                [](const auto &value) {
                const auto *identity = std::get_if<CloseReasonId>(&value);
                return identity != nullptr && identity->IsValid();
            },
                [](const auto &value) {
                const auto *identity = std::get_if<NetworkObjectId>(&value);
                return identity != nullptr && identity->IsValid();
            },
                [](const auto &value) {
                const auto *scalar = std::get_if<std::uint64_t>(&value);
                return scalar != nullptr && *scalar != 0;
            },
                [](const auto &value) {
                const auto *scalar = std::get_if<std::uint64_t>(&value);
                return scalar != nullptr && *scalar != 0;
            },
                [](const auto &value) {
                const auto *identity = std::get_if<MessageTypeId>(&value);
                return identity != nullptr && identity->IsValid();
            },
                [](const auto &value) {
                return std::holds_alternative<std::uint64_t>(value);
            },
                [](const auto &value) {
                return std::holds_alternative<std::uint64_t>(value);
            },
                [](const auto &value) {
                const auto *scalar = std::get_if<std::uint64_t>(&value);
                return scalar != nullptr && *scalar != 0;
            },
            };
            const auto index = static_cast<std::size_t>(entry.key);
            return index < validators.size() && validators[index](entry.value);
        }

        /** @brief Checks a bounded prefix for printable canonical UTF-8 without retaining it. */
        bool IsPrintableUtf8(const std::string_view text) noexcept {
            std::size_t index = 0;
            while (index < text.size()) {
                const auto lead = static_cast<unsigned char>(text[index]);
                if (lead < 0x80U) {
                    if (lead < 0x20U || lead == 0x7fU)
                        return false;
                    ++index;
                    continue;
                }
                std::size_t continuationCount = 0;
                std::uint32_t codepoint = 0;
                if (lead >= 0xc2U && lead <= 0xdfU) {
                    continuationCount = 1;
                    codepoint = lead & 0x1fU;
                } else if (lead >= 0xe0U && lead <= 0xefU) {
                    continuationCount = 2;
                    codepoint = lead & 0x0fU;
                } else if (lead >= 0xf0U && lead <= 0xf4U) {
                    continuationCount = 3;
                    codepoint = lead & 0x07U;
                } else {
                    return false;
                }
                if (index + continuationCount >= text.size())
                    return false;
                for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
                    const auto continuation = static_cast<unsigned char>(text[index + offset]);
                    if ((continuation & 0xc0U) != 0x80U)
                        return false;
                    codepoint = (codepoint << 6U) | (continuation & 0x3fU);
                }
                if ((continuationCount == 2 && (codepoint < 0x800U || (codepoint >= 0xd800U && codepoint <= 0xdfffU))) ||
                    (continuationCount == 3 && (codepoint < 0x10000U || codepoint > 0x10ffffU)))
                    return false;
                index += continuationCount + 1;
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
        const bool layerIsValid =
            layerKnown && (kind == NetworkFailureKind::FatalInternal || (classification.has_value() && classification->layer == layer));
        if (!classification.has_value() || !layerIsValid || !ContextIsValid(context))
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
