#include "Horo/Physics/CharacterWorldSettings.h"

#include "Horo/Physics/CharacterErrors.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>

namespace Horo::Character {
    namespace {
        [[nodiscard]] Result<void> Invalid(const char *message) {
            return Result<void>::Failure(MakeError(CharacterErrors::DescriptorInvalid, message));
        }

        [[nodiscard]] Result<void> Exceeded(const char *message) {
            return Result<void>::Failure(MakeError(CharacterErrors::CapacityExceeded, message));
        }

        [[nodiscard]] bool IsZero(const CharacterWorldHistoryBudgets &history) noexcept {
            return history.maximumCheckpoints == 0 && history.maximumBytes == 0 && history.maximumResimulationTicks == 0;
        }

        [[nodiscard]] Result<void> ValidateCapacities(const CharacterWorldCapacities &values) {
            if (values.maximumControllers == 0 || values.maximumQueuedCommands == 0 || values.maximumRetainedContacts == 0 ||
                values.maximumQueuedEvents == 0 || values.maximumQueuedQueries == 0 || values.maximumStagedImpulses == 0 ||
                values.maximumDiagnosticRecords == 0 || values.maximumDebugPrimitives == 0) {
                return Invalid("Character world retained capacities must all be non-zero.");
            }
            if (values.maximumControllers > CharacterWorldSettingLimits::MaximumControllers ||
                values.maximumQueuedCommands > CharacterWorldSettingLimits::MaximumQueuedCommands ||
                values.maximumRetainedContacts > CharacterWorldSettingLimits::MaximumRetainedContacts ||
                values.maximumQueuedEvents > CharacterWorldSettingLimits::MaximumQueuedEvents ||
                values.maximumQueuedQueries > CharacterWorldSettingLimits::MaximumQueuedQueries ||
                values.maximumStagedImpulses > CharacterWorldSettingLimits::MaximumStagedImpulses ||
                values.maximumDiagnosticRecords > CharacterWorldSettingLimits::MaximumDiagnosticRecords ||
                values.maximumDebugPrimitives > CharacterWorldSettingLimits::MaximumDebugPrimitives) {
                return Exceeded("Character world retained capacity exceeds a schema-1 hard ceiling.");
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateWork(const CharacterWorldWorkBudgets &work, const CharacterWorldCapacities &capacities) {
            if (work.maximumCommandsPerTick == 0 || work.maximumQueriesPerTick == 0 || work.maximumContactsPerMovement == 0 ||
                work.maximumMovementIterations == 0 || work.maximumRecoveryIterations == 0 || work.scratchBytes == 0 ||
                !std::isfinite(work.maximumDisplacementMetersPerTick) || work.maximumDisplacementMetersPerTick <= 0.0F) {
                return Invalid("Character world work budgets must be finite and non-zero.");
            }
            if (work.maximumCommandsPerTick > capacities.maximumQueuedCommands ||
                work.maximumQueriesPerTick > capacities.maximumQueuedQueries ||
                work.maximumContactsPerMovement > MaximumCharacterContacts ||
                work.maximumMovementIterations > CharacterWorldSettingLimits::MaximumMovementIterations ||
                work.maximumRecoveryIterations > CharacterWorldSettingLimits::MaximumRecoveryIterations ||
                work.scratchBytes > CharacterWorldSettingLimits::MaximumScratchBytes ||
                work.maximumDisplacementMetersPerTick > CharacterWorldSettingLimits::MaximumDisplacementMetersPerTick) {
                return Exceeded("Character fixed-tick work exceeds retained storage or a schema-1 hard ceiling.");
            }

            const auto retainedContactRequirement =
                static_cast<std::uint64_t>(capacities.maximumControllers) * work.maximumContactsPerMovement;
            if (retainedContactRequirement > capacities.maximumRetainedContacts) {
                return Invalid("Retained contact capacity must cover every controller's admitted movement result.");
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateHistory(const CharacterWorldHistoryBudgets &history) {
            if (IsZero(history)) {
                return Result<void>::Success();
            }
            if (!history.Enabled()) {
                return Invalid("Character history must be fully disabled or provide every checkpoint and resimulation bound.");
            }
            if (history.maximumCheckpoints > CharacterWorldSettingLimits::MaximumHistoryCheckpoints ||
                history.maximumBytes > CharacterWorldSettingLimits::MaximumHistoryBytes ||
                history.maximumResimulationTicks > CharacterWorldSettingLimits::MaximumResimulationTicks) {
                return Exceeded("Character history or resimulation capacity exceeds a schema-1 hard ceiling.");
            }
            if (history.maximumResimulationTicks > history.maximumCheckpoints || history.maximumBytes < history.maximumCheckpoints) {
                return Invalid("Character history must retain at least one byte per checkpoint and cover the resimulation horizon.");
            }
            return Result<void>::Success();
        }

        [[nodiscard]] CharacterWorldSettingsIdentity SettingsIdentity(const CharacterWorldSettingsDescriptor &values) noexcept {
            constexpr std::uint64_t SchemaVersion = 1;
            const auto displacement = std::bit_cast<std::uint32_t>(values.work.maximumDisplacementMetersPerTick);
            const std::array<std::uint64_t, 19> words{
                SchemaVersion,
                values.capacities.maximumControllers,
                values.capacities.maximumQueuedCommands,
                values.capacities.maximumRetainedContacts,
                values.capacities.maximumQueuedEvents,
                values.capacities.maximumQueuedQueries,
                values.capacities.maximumStagedImpulses,
                values.capacities.maximumDiagnosticRecords,
                values.capacities.maximumDebugPrimitives,
                values.work.maximumCommandsPerTick,
                values.work.maximumQueriesPerTick,
                values.work.maximumContactsPerMovement,
                values.work.maximumMovementIterations,
                values.work.maximumRecoveryIterations,
                values.work.scratchBytes,
                displacement,
                values.history.maximumCheckpoints,
                values.history.maximumBytes,
                values.history.maximumResimulationTicks,
            };
            std::array<std::byte, words.size() * sizeof(std::uint64_t)> bytes{};
            std::size_t cursor{};
            for (const auto word : words) {
                for (std::uint32_t shift{}; shift < 64; shift += 8) {
                    bytes[cursor++] = static_cast<std::byte>((word >> shift) & 0xffU);
                }
            }
            return {ComputeSha256(bytes)};
        }
    }  // namespace

    /** @copydoc CharacterWorldSettings::Capture */
    Result<CharacterWorldSettings> CharacterWorldSettings::Capture(const CharacterWorldSettingsDescriptor &descriptor) {
        if (const auto capacities = ValidateCapacities(descriptor.capacities); capacities.HasError()) {
            return Result<CharacterWorldSettings>::Failure(capacities.ErrorValue());
        }
        if (const auto work = ValidateWork(descriptor.work, descriptor.capacities); work.HasError()) {
            return Result<CharacterWorldSettings>::Failure(work.ErrorValue());
        }
        if (const auto history = ValidateHistory(descriptor.history); history.HasError()) {
            return Result<CharacterWorldSettings>::Failure(history.ErrorValue());
        }
        return Result<CharacterWorldSettings>::Success(CharacterWorldSettings{descriptor, SettingsIdentity(descriptor)});
    }

    /** @copydoc CharacterWorldSettings::CharacterWorldSettings */
    CharacterWorldSettings::CharacterWorldSettings(CharacterWorldSettingsDescriptor values, CharacterWorldSettingsIdentity identity)
        : values_(values), identity_(identity) {}

    /** @copydoc CharacterWorldSettings::Values */
    const CharacterWorldSettingsDescriptor &CharacterWorldSettings::Values() const noexcept {
        return values_;
    }

    /** @copydoc CharacterWorldSettings::Identity */
    const CharacterWorldSettingsIdentity &CharacterWorldSettings::Identity() const noexcept {
        return identity_;
    }
}  // namespace Horo::Character
