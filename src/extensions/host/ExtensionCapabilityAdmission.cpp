#include "Horo/Extensions/ExtensionCapabilityAdmission.h"

#include "Horo/Extensions/ExtensionErrors.h"

#include <algorithm>
#include <atomic>
#include <ranges>
#include <string_view>
#include <utility>

namespace Horo::Extensions {
    struct ExtensionCapabilityAdmissionState final {
        std::string extensionId;
        std::string moduleId;
        std::uint64_t activationGeneration{};
        std::uint64_t policyRevision{};
        std::atomic_bool active{true};
    };

    namespace {
        constexpr std::size_t MaximumIdentityBytes = 256;
        constexpr std::size_t MaximumAdmissionEntries = 256;

        [[nodiscard]] bool IsCanonicalSegment(const std::string_view segment) noexcept {
            if (segment.empty() || segment.front() < 'a' || segment.front() > 'z' || segment.back() == '-')
                return false;
            return std::ranges::all_of(segment, [](const char value) {
                return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-';
            });
        }

        [[nodiscard]] bool IsCanonicalId(const std::string_view value) noexcept {
            if (value.empty() || value.size() > MaximumIdentityBytes || value.front() == '.' || value.back() == '.')
                return false;
            std::size_t start = 0;
            while (start < value.size()) {
                const std::size_t separator = value.find('.', start);
                const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
                if (!IsCanonicalSegment(value.substr(start, end - start)))
                    return false;
                start = end + 1;
            }
            return true;
        }

        template <typename Identity> [[nodiscard]] bool Contains(const std::vector<Identity> &identities, const Identity &identity) {
            return std::ranges::find(identities, identity) != identities.end();
        }

        template <typename Identity> [[nodiscard]] bool IsValidUniqueSet(const std::vector<Identity> &identities) {
            if (identities.size() > MaximumAdmissionEntries)
                return false;
            std::vector<std::string_view> values;
            values.reserve(identities.size());
            for (const Identity &identity : identities) {
                if (!IsCanonicalId(identity.value))
                    return false;
                values.push_back(identity.value);
            }
            std::ranges::sort(values);
            return std::ranges::adjacent_find(values) == values.end();
        }

        [[nodiscard]] Result<void> ValidatePolicy(const ExtensionAdmissionPolicy &policy) {
            if (policy.revision == 0 || !IsValidUniqueSet(policy.knownPermissions) || !IsValidUniqueSet(policy.approvedPermissions) ||
                !IsValidUniqueSet(policy.availableCapabilities)) {
                return Result<void>::Failure(MakeError(ExtensionErrors::CapabilityAdmissionInvalid,
                                                       "Capability admission policy is malformed or exceeds its bounds."));
            }
            if (std::ranges::any_of(policy.approvedPermissions, [&policy](const ExtensionPermissionId &permission) {
                return !Contains(policy.knownPermissions, permission);
            })) {
                return Result<void>::Failure(MakeError(ExtensionErrors::CapabilityAdmissionInvalid,
                                                       "Approved permissions must belong to the sealed permission catalog."));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateRequest(const ExtensionAdmissionRequest &request) {
            if (!IsCanonicalId(request.extensionId) || !IsCanonicalId(request.moduleId) || request.activationGeneration == 0 ||
                request.capabilities.size() > MaximumAdmissionEntries) {
                return Result<void>::Failure(MakeError(ExtensionErrors::CapabilityAdmissionInvalid,
                                                       "Capability admission request has invalid owner, generation, or bounds."));
            }
            std::vector<ExtensionCapabilityId> capabilities;
            capabilities.reserve(request.capabilities.size());
            for (const ExtensionCapabilityRequest &entry : request.capabilities) {
                if (!IsCanonicalId(entry.capability.value) || !IsValidUniqueSet(entry.requiredPermissions)) {
                    return Result<void>::Failure(MakeError(ExtensionErrors::CapabilityAdmissionInvalid,
                                                           "Capability admission request contains invalid or duplicate identities."));
                }
                capabilities.push_back(entry.capability);
            }
            if (!IsValidUniqueSet(capabilities)) {
                return Result<void>::Failure(MakeError(ExtensionErrors::CapabilityAdmissionInvalid,
                                                       "A capability may be requested only once per module activation."));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> EvaluateEntry(const ExtensionCapabilityRequest &entry, const ExtensionAdmissionPolicy &policy) {
            if (!Contains(policy.availableCapabilities, entry.capability)) {
                return Result<void>::Failure(MakeError(ExtensionErrors::CapabilityUnavailable,
                                                       "Required host capability is unavailable: " + entry.capability.value));
            }
            for (const ExtensionPermissionId &permission : entry.requiredPermissions) {
                if (!Contains(policy.knownPermissions, permission)) {
                    return Result<void>::Failure(
                        MakeError(ExtensionErrors::PermissionDenied,
                                  "Required permission is unknown to the sealed host catalog: " + permission.value));
                }
                if (!Contains(policy.approvedPermissions, permission)) {
                    return Result<void>::Failure(MakeError(ExtensionErrors::PermissionDenied,
                                                           "Required permission was not approved by host policy: " + permission.value));
                }
            }
            return Result<void>::Success();
        }
    }  // namespace

    ExtensionCapabilityUseLease::ExtensionCapabilityUseLease(std::shared_ptr<const ExtensionCapabilityAdmissionState> state,
                                                             ExtensionCapabilityId capability)
        : state_(std::move(state)), capability_(std::move(capability)) {}

    /** @copydoc ExtensionCapabilityUseLease::Capability */
    const ExtensionCapabilityId &ExtensionCapabilityUseLease::Capability() const noexcept {
        return capability_;
    }

    ExtensionCapabilityHandle::ExtensionCapabilityHandle(std::shared_ptr<const ExtensionCapabilityAdmissionState> state,
                                                         ExtensionCapabilityId capability)
        : state_(std::move(state)), capability_(std::move(capability)) {}

    /** @copydoc ExtensionCapabilityHandle::Capability */
    const ExtensionCapabilityId &ExtensionCapabilityHandle::Capability() const noexcept {
        return capability_;
    }

    /** @copydoc ExtensionCapabilityHandle::ExtensionId */
    const std::string &ExtensionCapabilityHandle::ExtensionId() const noexcept {
        return state_->extensionId;
    }

    /** @copydoc ExtensionCapabilityHandle::ModuleId */
    const std::string &ExtensionCapabilityHandle::ModuleId() const noexcept {
        return state_->moduleId;
    }

    /** @copydoc ExtensionCapabilityHandle::ActivationGeneration */
    std::uint64_t ExtensionCapabilityHandle::ActivationGeneration() const noexcept {
        return state_->activationGeneration;
    }

    /** @copydoc ExtensionCapabilityHandle::AcquireUse */
    Result<ExtensionCapabilityUseLease> ExtensionCapabilityHandle::AcquireUse(const std::string_view extensionId,
                                                                              const std::string_view moduleId,
                                                                              const std::uint64_t activationGeneration) const {
        if (state_ == nullptr)
            return Result<ExtensionCapabilityUseLease>::Failure(MakeError(ExtensionErrors::CapabilityRevoked));
        if (!state_->active.load(std::memory_order_acquire))
            return Result<ExtensionCapabilityUseLease>::Failure(MakeError(ExtensionErrors::CapabilityRevoked));
        if (extensionId != state_->extensionId || moduleId != state_->moduleId || activationGeneration != state_->activationGeneration) {
            return Result<ExtensionCapabilityUseLease>::Failure(
                MakeError(ExtensionErrors::PermissionDenied, "Capability handle does not belong to the calling activation."));
        }
        return Result<ExtensionCapabilityUseLease>::Success(ExtensionCapabilityUseLease{state_, capability_});
    }

    /** @copydoc ExtensionCapabilityHandle::IsUsable */
    bool ExtensionCapabilityHandle::IsUsable() const noexcept {
        if (state_ == nullptr)
            return false;
        return state_->active.load(std::memory_order_acquire);
    }

    ExtensionCapabilityAdmission::ExtensionCapabilityAdmission(std::shared_ptr<ExtensionCapabilityAdmissionState> state,
                                                               std::vector<ExtensionCapabilityId> capabilities) noexcept
        : state_(std::move(state)), capabilities_(std::move(capabilities)) {}

    ExtensionCapabilityAdmission::~ExtensionCapabilityAdmission() {
        Revoke();
    }

    ExtensionCapabilityAdmission::ExtensionCapabilityAdmission(ExtensionCapabilityAdmission &&other) noexcept
        : state_(std::move(other.state_)), capabilities_(std::move(other.capabilities_)) {}

    ExtensionCapabilityAdmission &ExtensionCapabilityAdmission::operator=(ExtensionCapabilityAdmission &&other) noexcept {
        if (this == &other)
            return *this;
        Revoke();
        state_ = std::move(other.state_);
        capabilities_ = std::move(other.capabilities_);
        return *this;
    }

    /** @copydoc ExtensionCapabilityAdmission::Evaluate */
    Result<ExtensionCapabilityAdmission> ExtensionCapabilityAdmission::Evaluate(const ExtensionAdmissionRequest &request,
                                                                                const ExtensionAdmissionPolicy &policy) {
        if (auto valid = ValidatePolicy(policy); valid.HasError())
            return Result<ExtensionCapabilityAdmission>::Failure(valid.ErrorValue());
        if (auto valid = ValidateRequest(request); valid.HasError())
            return Result<ExtensionCapabilityAdmission>::Failure(valid.ErrorValue());

        std::vector<ExtensionCapabilityId> capabilities;
        capabilities.reserve(request.capabilities.size());
        for (const ExtensionCapabilityRequest &entry : request.capabilities) {
            if (auto admitted = EvaluateEntry(entry, policy); admitted.HasError())
                return Result<ExtensionCapabilityAdmission>::Failure(admitted.ErrorValue());
            capabilities.push_back(entry.capability);
        }
        std::ranges::sort(capabilities, {}, &ExtensionCapabilityId::value);
        auto state = std::make_shared<ExtensionCapabilityAdmissionState>();
        state->extensionId = request.extensionId;
        state->moduleId = request.moduleId;
        state->activationGeneration = request.activationGeneration;
        state->policyRevision = policy.revision;
        return Result<ExtensionCapabilityAdmission>::Success(ExtensionCapabilityAdmission{std::move(state), std::move(capabilities)});
    }

    /** @copydoc ExtensionCapabilityAdmission::Grant */
    Result<ExtensionCapabilityHandle> ExtensionCapabilityAdmission::Grant(const ExtensionCapabilityId &capability) const {
        if (state_ == nullptr)
            return Result<ExtensionCapabilityHandle>::Failure(MakeError(ExtensionErrors::CapabilityRevoked));
        if (!state_->active.load(std::memory_order_acquire))
            return Result<ExtensionCapabilityHandle>::Failure(MakeError(ExtensionErrors::CapabilityRevoked));
        if (!Contains(capabilities_, capability)) {
            return Result<ExtensionCapabilityHandle>::Failure(
                MakeError(ExtensionErrors::CapabilityUnavailable, "Capability was not declared and admitted: " + capability.value));
        }
        return Result<ExtensionCapabilityHandle>::Success(ExtensionCapabilityHandle{state_, capability});
    }

    /** @copydoc ExtensionCapabilityAdmission::Revoke */
    void ExtensionCapabilityAdmission::Revoke() noexcept {
        if (state_ != nullptr)
            state_->active.store(false, std::memory_order_release);
    }

    /** @copydoc ExtensionCapabilityAdmission::PolicyRevision */
    std::uint64_t ExtensionCapabilityAdmission::PolicyRevision() const noexcept {
        return state_ == nullptr ? 0 : state_->policyRevision;
    }

    /** @copydoc ExtensionCapabilityAdmission::Capabilities */
    std::span<const ExtensionCapabilityId> ExtensionCapabilityAdmission::Capabilities() const noexcept {
        return capabilities_;
    }
}  // namespace Horo::Extensions
