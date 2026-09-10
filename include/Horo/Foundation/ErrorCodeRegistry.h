#pragma once

/**
 * @file ErrorCodeRegistry.h
 * @brief Immutable host-validated registry of module-owned textual error identities.
 */

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <memory>
#include <span>

namespace Horo {
    struct ModuleDescriptor;
    struct ModuleId;
    class ErrorCodeRegistryBuilder;

    /**
     * @brief Immutable lookup snapshot for validated module-owned error descriptors.
     *
     * Registry storage owns copies of textual descriptor data. Returned descriptor and
     * owner pointers remain valid for the lifetime of this snapshot. Copying a snapshot
     * shares immutable storage and performs no descriptor cloning.
     */
    class ErrorCodeRegistry {
    public:
        ErrorCodeRegistry() noexcept = default;

        /**
         * @brief Resolves an exact stable domain/code pair.
         * @param domain Stable textual error domain.
         * @param code Stable textual error code.
         * @return Registered descriptor, or nullptr when the pair is not registered.
         */
        [[nodiscard]] const ErrorCodeDescriptor *Resolve(const ErrorDomainId &domain, const ErrorCode &code) const noexcept;

        /**
         * @brief Resolves the exact descriptor governing an error value.
         * @param error Typed error whose stable identity is inspected.
         * @return Registered descriptor, or nullptr when the error is not externally declared.
         */
        [[nodiscard]] const ErrorCodeDescriptor *Resolve(const Error &error) const noexcept;

        /**
         * @brief Returns the module that owns an exact registered error identity.
         * @param domain Stable textual error domain.
         * @param code Stable textual error code.
         * @return Registry-owned module identity, or nullptr when the pair is not registered.
         */
        [[nodiscard]] const ModuleId *OwnerOf(const ErrorDomainId &domain, const ErrorCode &code) const noexcept;

        /** @brief Returns the number of registered textual domain/code pairs. */
        [[nodiscard]] std::size_t Size() const noexcept;

        /** @brief Returns the number of explicitly owned textual error domains. */
        [[nodiscard]] std::size_t DomainCount() const noexcept;

        /** @brief Returns whether this snapshot contains no registered codes or domains. */
        [[nodiscard]] bool Empty() const noexcept;

    private:
        struct Storage;

        explicit ErrorCodeRegistry(std::shared_ptr<const Storage> storage) noexcept;

        std::shared_ptr<const Storage> storage_;

        friend class ErrorCodeRegistryBuilder;
        friend Result<ErrorCodeRegistry> BuildErrorCodeRegistry(std::span<const ModuleDescriptor> descriptors);
        friend Result<ErrorCodeRegistry> ExtendErrorCodeRegistry(const ErrorCodeRegistry &base,
                                                                 std::span<const ModuleDescriptor> descriptors);
    };

    /**
     * @brief Validates a complete module contribution set and builds an immutable registry.
     * @param descriptors Modules whose explicitly claimed domains and descriptors are registered.
     * @return Immutable registry, or a typed failure for malformed namespaces, ownership
     *         collisions, duplicate pairs, or invalid deprecation replacements.
     * @note Descriptor pointers must remain valid only for this call; all textual data is copied.
     */
    [[nodiscard]] Result<ErrorCodeRegistry> BuildErrorCodeRegistry(std::span<const ModuleDescriptor> descriptors);

    /**
     * @brief Builds a new immutable snapshot by adding module contributions to an existing registry.
     * @param base Previously published registry snapshot.
     * @param descriptors Newly selected modules whose contributions are validated against @p base.
     * @return Extended immutable registry, or a typed failure leaving @p base unchanged.
     * @note Descriptor pointers must remain valid only for this call; all textual data is copied.
     */
    [[nodiscard]] Result<ErrorCodeRegistry> ExtendErrorCodeRegistry(const ErrorCodeRegistry &base,
                                                                    std::span<const ModuleDescriptor> descriptors);
}  // namespace Horo
