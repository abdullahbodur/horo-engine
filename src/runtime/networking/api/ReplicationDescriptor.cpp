#include "Horo/Network/ReplicationDescriptor.h"

#include "Horo/Network/NetworkErrors.h"

#include <algorithm>
#include <new>
#include <string_view>
#include <vector>

namespace Horo::Network {
    namespace {
        /** @brief Returns whether one owner byte is a permitted separator. */
        [[nodiscard]] constexpr bool IsOwnerSeparator(const unsigned char character) noexcept {
            return character == '.' || character == '-' || character == '_';
        }

        /** @brief Returns whether one owner byte is lowercase ASCII or a decimal digit. */
        [[nodiscard]] constexpr bool IsLowercaseAlphanumeric(const unsigned char character) noexcept {
            return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        }

        /** @brief Checks the Foundation module identity grammar without creating ambient registration. */
        [[nodiscard]] bool IsCanonicalOwner(const std::string_view owner) noexcept {
            if (owner.empty())
                return false;
            bool previousSeparator = true;
            for (const unsigned char character : owner) {
                if (IsOwnerSeparator(character)) {
                    if (previousSeparator)
                        return false;
                    previousSeparator = true;
                    continue;
                }
                if (!IsLowercaseAlphanumeric(character))
                    return false;
                previousSeparator = false;
            }
            return !previousSeparator;
        }

        /** @brief Checks that all construction limits are explicit and finite. */
        [[nodiscard]] bool AreValid(const ReplicationDescriptorLimits &limits) noexcept {
            return limits.maximumSchemas > 0 && limits.maximumFieldsPerSchema > 0 && limits.maximumOwnerIdentityBytes > 0 &&
                   limits.maximumDefaultBytesPerField > 0 && limits.maximumTotalDefaultBytes > 0;
        }

        /** @brief Validates one field's stable identity and introduction version. */
        [[nodiscard]] Result<void> ValidateFieldIdentity(const ReplicationFieldDescriptor &field,
                                                         const ReplicationSchemaDescriptor &schema) {
            if (!field.id.IsValid() || !field.valueType.IsValid() || !field.codec.IsValid())
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            if (!field.introducedVersion.IsValid() || field.introducedVersion > schema.version)
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            return Result<void>::Success();
        }

        /** @brief Validates one field's policy enumerators and finite wire bounds. */
        [[nodiscard]] Result<void> ValidateFieldPolicy(const ReplicationFieldDescriptor &field) {
            if (field.condition >= ReplicationCondition::Count || field.requirement >= ReplicationFieldRequirement::Count ||
                field.writePolicy >= ReplicationWritePolicy::Count)
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            if (field.limits.maximumEncodedBytes == 0 || field.limits.maximumElementCount == 0)
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            return Result<void>::Success();
        }

        /** @brief Validates one field's canonical default and compatible-minor requirement. */
        [[nodiscard]] Result<void> ValidateFieldDefault(const ReplicationFieldDescriptor &field, const ReplicationSchemaDescriptor &schema,
                                                        const ReplicationDescriptorLimits &limits) {
            using enum ReplicationFieldRequirement;
            if (field.canonicalDefault.has_value()) {
                if (field.requirement != Optional || field.canonicalDefault->canonicalBytes.size() > limits.maximumDefaultBytesPerField ||
                    field.canonicalDefault->canonicalBytes.size() > field.limits.maximumEncodedBytes)
                    return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            } else if (field.requirement == Optional) {
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            }

            if (field.requirement == Required && field.introducedVersion > schema.compatibility.minimum)
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorIncompatible));
            return Result<void>::Success();
        }

        /** @brief Validates schema-wide identity, version, ownership, and non-empty state. */
        [[nodiscard]] Result<void> ValidateSchemaShape(const ReplicationSchemaDescriptor &descriptor) {
            if (!descriptor.id.IsValid() || !descriptor.version.IsValid() || !descriptor.compatibility.minimum.IsValid() ||
                !descriptor.compatibility.maximum.IsValid())
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            if (descriptor.compatibility.minimum.major != descriptor.version.major ||
                descriptor.compatibility.maximum != descriptor.version ||
                descriptor.compatibility.minimum > descriptor.compatibility.maximum)
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            if (!IsCanonicalOwner(descriptor.owner.value) || descriptor.fields.empty())
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            return Result<void>::Success();
        }

        /** @brief Validates schema-wide construction capacities without overflowing subtraction. */
        [[nodiscard]] Result<void> ValidateSchemaCapacity(const ReplicationSchemaDescriptor &descriptor,
                                                          const ReplicationDescriptorLimits &limits) {
            if (descriptor.owner.value.size() > limits.maximumOwnerIdentityBytes ||
                descriptor.fields.size() > limits.maximumFieldsPerSchema ||
                descriptor.tombstonedFields.size() > limits.maximumFieldsPerSchema ||
                descriptor.fields.size() > limits.maximumFieldsPerSchema - descriptor.tombstonedFields.size())
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationCapacityExceeded));
            return Result<void>::Success();
        }

        /** @brief Validates active and retired field identities independent of declaration order. */
        [[nodiscard]] Result<void> ValidateFieldIdentities(const ReplicationSchemaDescriptor &descriptor) {
            std::vector<FieldId> activeFields;
            activeFields.reserve(descriptor.fields.size());
            for (const ReplicationFieldDescriptor &field : descriptor.fields)
                activeFields.push_back(field.id);
            std::ranges::sort(activeFields);
            if (std::ranges::adjacent_find(activeFields) != activeFields.end())
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorConflict));

            std::vector<FieldId> tombstones = descriptor.tombstonedFields;
            if (std::ranges::any_of(tombstones, [](const FieldId id) {
                return !id.IsValid();
            }))
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorConflict));
            std::ranges::sort(tombstones);
            if (std::ranges::adjacent_find(tombstones) != tombstones.end())
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorConflict));
            if (std::ranges::any_of(tombstones, [&activeFields](const FieldId id) {
                return std::ranges::binary_search(activeFields, id);
            }))
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorConflict));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc ValidateReplicationSchemaDescriptor */
    Result<void> ValidateReplicationSchemaDescriptor(const ReplicationSchemaDescriptor &descriptor,
                                                     const ReplicationDescriptorLimits &limits) {
        try {
            if (!AreValid(limits))
                return Result<void>::Failure(MakeError(NetworkErrors::ReplicationDescriptorInvalid));
            if (const Result<void> shape = ValidateSchemaShape(descriptor); shape.HasError())
                return shape;
            if (const Result<void> capacity = ValidateSchemaCapacity(descriptor, limits); capacity.HasError())
                return capacity;
            for (const ReplicationFieldDescriptor &field : descriptor.fields) {
                if (const Result<void> identity = ValidateFieldIdentity(field, descriptor); identity.HasError())
                    return identity;
                if (const Result<void> policy = ValidateFieldPolicy(field); policy.HasError())
                    return policy;
                if (const Result<void> validDefault = ValidateFieldDefault(field, descriptor, limits); validDefault.HasError())
                    return validDefault;
            }
            return ValidateFieldIdentities(descriptor);
        } catch (const std::bad_alloc &) {
            return Result<void>::Failure(MakeError(NetworkErrors::ReplicationCapacityExceeded));
        }
    }
}  // namespace Horo::Network
