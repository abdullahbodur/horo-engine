#include "Horo/Physics/PhysicsDiagnostics.h"

#include "Horo/Physics/PhysicsErrors.h"

#include <optional>
#include <utility>

namespace Horo::Physics {
    namespace {
        /** @brief Maps only canonical Physics error descriptors to stable diagnostic identities. */
        std::optional<DiagnosticCode> CanonicalDiagnosticCode(const Error &error) {
            if (error.domain.Value() != "horo.physics")
                return std::nullopt;

            const ErrorCodeDescriptor *const descriptors[]{
                &PhysicsErrors::WorldInvalid,
                &PhysicsErrors::HandleMalformed,
                &PhysicsErrors::HandleWorldMismatch,
                &PhysicsErrors::HandleStale,
                &PhysicsErrors::GenerationExhausted,
                &PhysicsErrors::CapabilityUnavailable,
                &PhysicsErrors::OperationUnsupported,
                &PhysicsErrors::InvalidState,
                &PhysicsErrors::ThreadAffinityViolation,
                &PhysicsErrors::SolverDeadlineExceeded,
                &PhysicsErrors::DescriptorInvalid,
                &PhysicsErrors::ProfileUnsupported,
                &PhysicsErrors::CapacityExceeded,
                &PhysicsErrors::CapabilityStale,
                &PhysicsErrors::QuerySnapshotStale,
                &PhysicsErrors::InitializationFailed,
            };
            for (const ErrorCodeDescriptor *descriptor : descriptors) {
                if (error.code.Value() == descriptor->code.Value())
                    return DiagnosticCode{std::string{descriptor->code.Value()}};
            }
            return std::nullopt;
        }

        /** @brief Converts the complete Foundation severity vocabulary without fallback. */
        std::optional<DiagnosticSeverity> DiagnosticSeverityFor(const ErrorSeverity severity) noexcept {
            switch (severity) {
                case ErrorSeverity::Info:
                    return DiagnosticSeverity::Note;
                case ErrorSeverity::Warning:
                    return DiagnosticSeverity::Warning;
                case ErrorSeverity::Error:
                    return DiagnosticSeverity::Error;
                case ErrorSeverity::Critical:
                    return DiagnosticSeverity::Fatal;
            }
            return std::nullopt;
        }

        /** @brief Returns whether a scalar key requires non-zero generation/order evidence. */
        bool RequiresNonZeroScalar(const PhysicsDiagnosticContextKey key) noexcept {
            return key == PhysicsDiagnosticContextKey::SceneGeneration || key == PhysicsDiagnosticContextKey::SimulationTick ||
                   key == PhysicsDiagnosticContextKey::QuerySnapshotGeneration || key == PhysicsDiagnosticContextKey::OperationSequence;
        }

        /** @brief Validates one key/value pair and captures its world identity when applicable. */
        bool ValidateContextEntry(const PhysicsDiagnosticContextEntry &entry, std::optional<PhysicsWorldId> &contextWorld) noexcept {
            const auto acceptWorld = [&contextWorld](const PhysicsWorldId world) {
                if (!world.IsValid() || (contextWorld.has_value() && *contextWorld != world))
                    return false;
                contextWorld = world;
                return true;
            };

            switch (entry.key) {
                case PhysicsDiagnosticContextKey::World:
                    if (const auto *world = std::get_if<PhysicsWorldId>(&entry.value))
                        return acceptWorld(*world);
                    return false;
                case PhysicsDiagnosticContextKey::Body:
                    if (const auto *handle = std::get_if<BodyHandle>(&entry.value))
                        return handle->IsValid() && acceptWorld(handle->world);
                    return false;
                case PhysicsDiagnosticContextKey::Shape:
                    if (const auto *handle = std::get_if<ShapeHandle>(&entry.value))
                        return handle->IsValid() && acceptWorld(handle->world);
                    return false;
                case PhysicsDiagnosticContextKey::Constraint:
                    if (const auto *handle = std::get_if<ConstraintHandle>(&entry.value))
                        return handle->IsValid() && acceptWorld(handle->world);
                    return false;
                case PhysicsDiagnosticContextKey::Asset:
                    if (const auto *asset = std::get_if<Assets::AssetId>(&entry.value))
                        return asset->IsValid();
                    return false;
                case PhysicsDiagnosticContextKey::SceneGeneration:
                case PhysicsDiagnosticContextKey::SimulationTick:
                case PhysicsDiagnosticContextKey::QuerySnapshotGeneration:
                case PhysicsDiagnosticContextKey::OperationSequence:
                case PhysicsDiagnosticContextKey::RequestedCount:
                case PhysicsDiagnosticContextKey::Capacity:
                    if (const auto *value = std::get_if<std::uint64_t>(&entry.value))
                        return !RequiresNonZeroScalar(entry.key) || *value != 0;
                    return false;
            }
            return false;
        }
    }  // namespace

    /** @copydoc PhysicsDiagnosticCategoryName */
    std::string_view PhysicsDiagnosticCategoryName(const PhysicsDiagnosticCategory category) noexcept {
        switch (category) {
            case PhysicsDiagnosticCategory::Configuration:
                return "physics.configuration";
            case PhysicsDiagnosticCategory::Cook:
                return "physics.cook";
            case PhysicsDiagnosticCategory::Runtime:
                return "physics.runtime";
            case PhysicsDiagnosticCategory::Query:
                return "physics.query";
            case PhysicsDiagnosticCategory::Event:
                return "physics.event";
            case PhysicsDiagnosticCategory::Lifecycle:
                return "physics.lifecycle";
        }
        return {};
    }

    /** @copydoc MakePhysicsDiagnosticRecord */
    Result<PhysicsDiagnosticRecord> MakePhysicsDiagnosticRecord(const PhysicsDiagnosticCategory category, const Error &error,
                                                                const std::span<const PhysicsDiagnosticContextEntry> context) {
        if (PhysicsDiagnosticCategoryName(category).empty())
            return Result<PhysicsDiagnosticRecord>::Failure(
                MakeError(PhysicsErrors::OperationUnsupported, "Unknown Physics diagnostic category."));
        const auto code = CanonicalDiagnosticCode(error);
        const auto severity = DiagnosticSeverityFor(error.severity);
        if (!code.has_value() || !severity.has_value() || error.message.empty() ||
            error.message.size() > MaximumPhysicsDiagnosticMessageBytes || context.size() > MaximumPhysicsDiagnosticContextEntries)
            return Result<PhysicsDiagnosticRecord>::Failure(
                MakeError(PhysicsErrors::DescriptorInvalid, "Physics diagnostic evidence is malformed or exceeds its bounded record."));

        std::optional<PhysicsDiagnosticContextKey> previousKey;
        std::optional<PhysicsWorldId> contextWorld;
        for (const PhysicsDiagnosticContextEntry &entry : context) {
            if ((previousKey.has_value() && entry.key <= *previousKey) || !ValidateContextEntry(entry, contextWorld))
                return Result<PhysicsDiagnosticRecord>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Physics diagnostic context is unordered, duplicate or malformed."));
            previousKey = entry.key;
        }

        PhysicsDiagnosticRecord record;
        record.category = category;
        record.code = *code;
        record.severity = *severity;
        record.message = error.message;
        record.contextCount = static_cast<std::uint8_t>(context.size());
        for (std::size_t index = 0; index < context.size(); ++index)
            record.context[index] = context[index];
        return Result<PhysicsDiagnosticRecord>::Success(std::move(record));
    }
}  // namespace Horo::Physics
