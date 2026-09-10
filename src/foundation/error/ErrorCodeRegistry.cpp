#include "Horo/Foundation/ErrorCodeRegistry.h"

#include "Horo/Foundation/ModuleDescriptor.h"
#include "foundation/FoundationErrors.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Horo {
    struct ErrorCodeRegistry::Storage {
        struct Domain {
            ErrorDomainId id;
            ModuleId owner;
        };

        struct Code {
            std::string summary;
            std::string remediationHint;
            ErrorCodeDescriptor descriptor;
            ModuleId owner;

            Code(const ErrorCodeDescriptor &source, ModuleId moduleOwner)
                : summary(source.summary), remediationHint(source.remediationHint), descriptor{.domain = source.domain,
                                                                                               .code = source.code,
                                                                                               .defaultSeverity = source.defaultSeverity,
                                                                                               .summary = summary,
                                                                                               .remediationHint = remediationHint,
                                                                                               .retryable = source.retryable,
                                                                                               .userActionable = source.userActionable,
                                                                                               .deprecatedBy = source.deprecatedBy},
                  owner(std::move(moduleOwner)) {}

            Code(const Code &) = delete;
            Code &operator=(const Code &) = delete;
        };

        std::vector<Domain> domains;
        std::vector<std::unique_ptr<Code>> codes;
        std::vector<const Code *> orderedCodes;
    };

    namespace {
        using TextPair = std::pair<std::string, std::string>;

        /** @brief Returns whether a stable identity is canonical and contains a namespace separator. */
        [[nodiscard]] bool IsCanonicalNamespacedId(const std::string_view value) noexcept {
            if (value.empty() || value.find('.') == std::string_view::npos)
                return false;
            bool previousWasSeparator = true;
            for (const unsigned char ch : value) {
                if (constexpr std::string_view kIdentityCharacters = "abcdefghijklmnopqrstuvwxyz0123456789.-_";
                    kIdentityCharacters.find(static_cast<char>(ch)) == std::string_view::npos)
                    return false;
                if (ch == '.' || ch == '-' || ch == '_') {
                    if (previousWasSeparator)
                        return false;
                    previousWasSeparator = true;
                    continue;
                }
                previousWasSeparator = false;
            }
            return !previousWasSeparator;
        }

        [[nodiscard]] std::string_view RootNamespace(const std::string_view value) noexcept {
            return value.substr(0, value.find('.'));
        }

        [[nodiscard]] bool IsSameOrNested(const std::string_view value, const std::string_view owner) noexcept {
            return value == owner || (value.size() > owner.size() && value.starts_with(owner) && value[owner.size()] == '.');
        }

        /** @brief Validates whether a module may claim the root and scope of an error domain. */
        [[nodiscard]] bool IsDomainAllowedForModule(const std::string_view domain, const std::string_view moduleId) noexcept {
            const std::string_view root = RootNamespace(domain);
            if (root != RootNamespace(moduleId) || (root != "horo" && root != "project" && root != "extension"))
                return false;
            if (root == "project" || root == "extension")
                return IsSameOrNested(domain, moduleId);
            return true;
        }

        [[nodiscard]] bool NamespacesOverlap(const std::string_view lhs, const std::string_view rhs) noexcept {
            return IsSameOrNested(lhs, rhs) || IsSameOrNested(rhs, lhs);
        }

        /** @brief Returns whether a descriptor carries a supported severity enumerator. */
        [[nodiscard]] bool IsKnownSeverity(const ErrorSeverity severity) noexcept {
            using enum ErrorSeverity;
            switch (severity) {
                case Info:
                case Warning:
                case Error:
                case Critical:
                    return true;
            }
            return false;
        }

        template <typename T> [[nodiscard]] Result<T> RegistryFailure(const ErrorCodeDescriptor &descriptor, std::string message) {
            return Result<T>::Failure(MakeError(descriptor, std::move(message)));
        }
    }  // namespace

    class ErrorCodeRegistryBuilder {
    public:
        [[nodiscard]] static Result<ErrorCodeRegistry> Build(const ErrorCodeRegistry *base,
                                                             const std::span<const ModuleDescriptor> descriptors) {
            auto storage = std::make_shared<ErrorCodeRegistry::Storage>();
            std::map<std::string, std::string, std::less<>> domainOwners;
            std::set<TextPair> registeredCodes;

            if (base != nullptr && base->storage_ != nullptr)
                CopyBase(*base->storage_, *storage, domainOwners, registeredCodes);

            for (const ModuleDescriptor &moduleDescriptor : descriptors) {
                const Result<void> added = AddModule(moduleDescriptor, *storage, domainOwners, registeredCodes);
                if (added.HasError())
                    return Result<ErrorCodeRegistry>::Failure(added.ErrorValue());
            }

            if (auto ownershipFailure = ValidateNamespaceOwnership(storage->domains); ownershipFailure.has_value())
                return Result<ErrorCodeRegistry>::Failure(std::move(*ownershipFailure));
            if (auto deprecationFailure = ValidateDeprecations(*storage, registeredCodes); deprecationFailure.has_value())
                return Result<ErrorCodeRegistry>::Failure(std::move(*deprecationFailure));

            IndexCodes(*storage);
            return Result<ErrorCodeRegistry>::Success(ErrorCodeRegistry{std::move(storage)});
        }

    private:
        [[nodiscard]] static Result<void> AddModule(const ModuleDescriptor &moduleDescriptor, ErrorCodeRegistry::Storage &storage,
                                                    std::map<std::string, std::string, std::less<>> &domainOwners,
                                                    std::set<TextPair> &registeredCodes) {
            for (const ModuleErrorDomainDescriptor &domain : moduleDescriptor.errorDomains) {
                const Result<void> added = AddDomain(moduleDescriptor, domain, storage, domainOwners, registeredCodes);
                if (added.HasError())
                    return added;
            }
            return Result<void>::Success();
        }

        [[nodiscard]] static Result<void> AddDomain(const ModuleDescriptor &moduleDescriptor, const ModuleErrorDomainDescriptor &domain,
                                                    ErrorCodeRegistry::Storage &storage,
                                                    std::map<std::string, std::string, std::less<>> &domainOwners,
                                                    std::set<TextPair> &registeredCodes) {
            const std::string &domainId = domain.id.Value();
            if (!IsCanonicalNamespacedId(domainId) || !IsDomainAllowedForModule(domainId, moduleDescriptor.id.value)) {
                return RegistryFailure<void>(ErrorCodeRegistryErrors::InvalidNamespace,
                                             std::format("Module '{}' cannot own non-canonical or escaped error domain '{}'.",
                                                         moduleDescriptor.id.value, domainId));
            }

            const auto [owner, inserted] = domainOwners.try_emplace(domainId, moduleDescriptor.id.value);
            if (!inserted) {
                const ErrorCodeDescriptor &failure = owner->second == moduleDescriptor.id.value
                                                         ? ErrorCodeRegistryErrors::InvalidNamespace
                                                         : ErrorCodeRegistryErrors::DomainOwnershipConflict;
                return RegistryFailure<void>(failure,
                                             std::format("Error domain '{}' is already claimed by module '{}'.", domainId, owner->second));
            }
            storage.domains.push_back({.id = domain.id, .owner = moduleDescriptor.id});

            for (const ErrorCodeDescriptor *descriptor : domain.descriptors) {
                const Result<void> added = AddCode(moduleDescriptor.id, domainId, descriptor, storage, registeredCodes);
                if (added.HasError())
                    return added;
            }
            return Result<void>::Success();
        }

        [[nodiscard]] static Result<void> AddCode(const ModuleId &owner, const std::string &domainId, const ErrorCodeDescriptor *descriptor,
                                                  ErrorCodeRegistry::Storage &storage, std::set<TextPair> &registeredCodes) {
            if (descriptor == nullptr) {
                return RegistryFailure<void>(ErrorCodeRegistryErrors::InvalidDescriptor,
                                             std::format("Error domain '{}' contains a null descriptor.", domainId));
            }
            if (descriptor->summary.empty() || !IsKnownSeverity(descriptor->defaultSeverity)) {
                return RegistryFailure<void>(ErrorCodeRegistryErrors::InvalidDescriptor,
                                             std::format("Error code '{}' under domain '{}' has incomplete or unsupported metadata.",
                                                         descriptor->code.Value(), domainId));
            }
            if (descriptor->domain.Value() != domainId || !IsCanonicalNamespacedId(descriptor->code.Value())) {
                return RegistryFailure<void>(ErrorCodeRegistryErrors::InvalidNamespace,
                                             std::format("Error code '{}' escapes or has an invalid namespace under domain '{}'.",
                                                         descriptor->code.Value(), domainId));
            }
            if (!registeredCodes.emplace(domainId, descriptor->code.Value()).second) {
                return RegistryFailure<void>(ErrorCodeRegistryErrors::DuplicateCode,
                                             std::format("Duplicate error identity ('{}', '{}').", domainId, descriptor->code.Value()));
            }
            storage.codes.push_back(std::make_unique<ErrorCodeRegistry::Storage::Code>(*descriptor, owner));
            return Result<void>::Success();
        }

        static void IndexCodes(ErrorCodeRegistry::Storage &storage) {
            storage.orderedCodes.reserve(storage.codes.size());
            for (const std::unique_ptr<ErrorCodeRegistry::Storage::Code> &code : storage.codes)
                storage.orderedCodes.push_back(code.get());
            std::ranges::sort(storage.orderedCodes, {}, [](const ErrorCodeRegistry::Storage::Code *entry) {
                return std::pair{entry->descriptor.domain.Value(), entry->descriptor.code.Value()};
            });
        }

        static void CopyBase(const ErrorCodeRegistry::Storage &base, ErrorCodeRegistry::Storage &target,
                             std::map<std::string, std::string, std::less<>> &domainOwners, std::set<TextPair> &registeredCodes) {
            target.domains = base.domains;
            for (const ErrorCodeRegistry::Storage::Domain &domain : target.domains)
                domainOwners.try_emplace(domain.id.Value(), domain.owner.value);
            target.codes.reserve(base.codes.size());
            for (const std::unique_ptr<ErrorCodeRegistry::Storage::Code> &code : base.codes) {
                registeredCodes.emplace(code->descriptor.domain.Value(), code->descriptor.code.Value());
                target.codes.push_back(std::make_unique<ErrorCodeRegistry::Storage::Code>(code->descriptor, code->owner));
            }
        }

        [[nodiscard]] static std::optional<Error> ValidateNamespaceOwnership(
            const std::vector<ErrorCodeRegistry::Storage::Domain> &domains) {
            for (std::size_t lhs = 0; lhs < domains.size(); ++lhs) {
                for (std::size_t rhs = lhs + 1; rhs < domains.size(); ++rhs) {
                    if (domains[lhs].owner != domains[rhs].owner && NamespacesOverlap(domains[lhs].id.Value(), domains[rhs].id.Value())) {
                        return MakeError(ErrorCodeRegistryErrors::DomainOwnershipConflict,
                                         std::format("Modules '{}' and '{}' claim overlapping error domains '{}' and '{}'.",
                                                     domains[lhs].owner.value, domains[rhs].owner.value, domains[lhs].id.Value(),
                                                     domains[rhs].id.Value()));
                    }
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] static std::optional<Error> ValidateDeprecations(const ErrorCodeRegistry::Storage &storage,
                                                                       const std::set<TextPair> &registeredCodes) {
            for (const std::unique_ptr<ErrorCodeRegistry::Storage::Code> &entry : storage.codes) {
                if (!entry->descriptor.deprecatedBy.has_value())
                    continue;
                const std::string &replacement = entry->descriptor.deprecatedBy->Value();
                const TextPair replacementKey{entry->descriptor.domain.Value(), replacement};
                if (!IsCanonicalNamespacedId(replacement) || replacement == entry->descriptor.code.Value() ||
                    !registeredCodes.contains(replacementKey)) {
                    return MakeError(ErrorCodeRegistryErrors::InvalidDeprecation,
                                     std::format("Deprecated error code '{}' has no distinct registered replacement in domain '{}'.",
                                                 entry->descriptor.code.Value(), entry->descriptor.domain.Value()));
                }
            }
            return std::nullopt;
        }
    };

    ErrorCodeRegistry::ErrorCodeRegistry(std::shared_ptr<const Storage> storage) noexcept : storage_(std::move(storage)) {}

    /** @copydoc ErrorCodeRegistry::Resolve(const ErrorDomainId &, const ErrorCode &) const */
    const ErrorCodeDescriptor *ErrorCodeRegistry::Resolve(const ErrorDomainId &domain, const ErrorCode &code) const noexcept {
        if (storage_ == nullptr)
            return nullptr;
        const auto key = std::pair<std::string_view, std::string_view>{domain.Value(), code.Value()};
        const auto found = std::ranges::lower_bound(storage_->orderedCodes, key, {}, [](const Storage::Code *entry) {
            return std::pair<std::string_view, std::string_view>{entry->descriptor.domain.Value(), entry->descriptor.code.Value()};
        });
        if (found == storage_->orderedCodes.end())
            return nullptr;
        const Storage::Code *entry = *found;
        if (entry->descriptor.domain.Value() != domain.Value() || entry->descriptor.code.Value() != code.Value())
            return nullptr;
        return &entry->descriptor;
    }

    /** @copydoc ErrorCodeRegistry::Resolve(const Error &) const */
    const ErrorCodeDescriptor *ErrorCodeRegistry::Resolve(const Error &error) const noexcept {
        return Resolve(error.domain, error.code);
    }

    /** @copydoc ErrorCodeRegistry::OwnerOf */
    const ModuleId *ErrorCodeRegistry::OwnerOf(const ErrorDomainId &domain, const ErrorCode &code) const noexcept {
        if (storage_ == nullptr)
            return nullptr;
        const ErrorCodeDescriptor *descriptor = Resolve(domain, code);
        if (descriptor == nullptr)
            return nullptr;
        const auto found = std::ranges::find_if(storage_->orderedCodes, [descriptor](const Storage::Code *entry) {
            return &entry->descriptor == descriptor;
        });
        return found == storage_->orderedCodes.end() ? nullptr : &(*found)->owner;
    }

    /** @copydoc ErrorCodeRegistry::Size */
    std::size_t ErrorCodeRegistry::Size() const noexcept {
        return storage_ == nullptr ? 0 : storage_->codes.size();
    }

    /** @copydoc ErrorCodeRegistry::DomainCount */
    std::size_t ErrorCodeRegistry::DomainCount() const noexcept {
        return storage_ == nullptr ? 0 : storage_->domains.size();
    }

    /** @copydoc ErrorCodeRegistry::Empty */
    bool ErrorCodeRegistry::Empty() const noexcept {
        return storage_ == nullptr || (storage_->codes.empty() && storage_->domains.empty());
    }

    /** @copydoc BuildErrorCodeRegistry */
    Result<ErrorCodeRegistry> BuildErrorCodeRegistry(const std::span<const ModuleDescriptor> descriptors) {
        return ErrorCodeRegistryBuilder::Build(nullptr, descriptors);
    }

    /** @copydoc ExtendErrorCodeRegistry */
    Result<ErrorCodeRegistry> ExtendErrorCodeRegistry(const ErrorCodeRegistry &base, const std::span<const ModuleDescriptor> descriptors) {
        return ErrorCodeRegistryBuilder::Build(&base, descriptors);
    }
}  // namespace Horo
