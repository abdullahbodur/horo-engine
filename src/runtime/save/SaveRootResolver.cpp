#include "Horo/Runtime/Save/SaveRootResolver.h"

#include "Horo/Foundation/Platform.h"
#include "Horo/Runtime/Save/SaveErrors.h"

#include <array>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Horo::Runtime {
    namespace {
        struct PlatformRootPlan {
            std::filesystem::path stateRoot;
            std::array<std::string_view, 2> fixedComponents;
        };

        [[nodiscard]] std::string_view PlatformName(const SaveRootPlatform platform) noexcept {
            switch (platform) {
                case SaveRootPlatform::Windows:
                    return "Windows";
                case SaveRootPlatform::MacOS:
                    return "macOS";
                case SaveRootPlatform::Linux:
                    return "Linux";
                case SaveRootPlatform::Test:
                    return "Test";
            }
            return "Unknown";
        }

        [[nodiscard]] Error SafeFailure(const ErrorCodeDescriptor &descriptor, const SaveRootPlatform platform,
                                        const std::string_view operation, const std::error_code error = {}) {
            std::string message{"Save-root "};
            message.append(operation);
            message.append(" failed for ");
            message.append(PlatformName(platform));
            if (error)
                message.append(" with filesystem code ").append(std::to_string(error.value()));
            message.push_back('.');
            return MakeError(descriptor, std::move(message));
        }

        [[nodiscard]] std::optional<std::filesystem::path> EnvironmentPath(const ProcessService &processes, const std::string_view name) {
            auto value = processes.EnvironmentValue(name);
            if (!value || value->empty())
                return std::nullopt;
            return std::filesystem::path{*value};
        }

        [[nodiscard]] Result<PlatformRootPlan> MissingEnvironment(const SaveRootPlatform platform) {
            return Result<PlatformRootPlan>::Failure(SafeFailure(SaveErrors::SaveRootConfigurationInvalid, platform, "environment lookup"));
        }

        [[nodiscard]] Result<PlatformRootPlan> WindowsPlan(const ProcessService &processes) {
            auto state = EnvironmentPath(processes, "LOCALAPPDATA");
            if (!state)
                return MissingEnvironment(SaveRootPlatform::Windows);
            return Result<PlatformRootPlan>::Success({.stateRoot = std::move(*state), .fixedComponents = {"Horo", "Products"}});
        }

        [[nodiscard]] Result<PlatformRootPlan> MacOSPlan(const ProcessService &processes) {
            auto home = EnvironmentPath(processes, "HOME");
            if (!home)
                return MissingEnvironment(SaveRootPlatform::MacOS);
            return Result<PlatformRootPlan>::Success(
                {.stateRoot = std::move(*home) / "Library" / "Application Support", .fixedComponents = {"Horo", "Products"}});
        }

        [[nodiscard]] Result<PlatformRootPlan> LinuxPlan(const ProcessService &processes) {
            auto state = EnvironmentPath(processes, "XDG_STATE_HOME");
            if (!state) {
                auto home = EnvironmentPath(processes, "HOME");
                if (!home)
                    return MissingEnvironment(SaveRootPlatform::Linux);
                state = std::move(*home) / ".local" / "state";
            }
            return Result<PlatformRootPlan>::Success({.stateRoot = std::move(*state), .fixedComponents = {"horo", "products"}});
        }

        [[nodiscard]] Result<PlatformRootPlan> PlatformPlan(const SaveRootResolutionRequest &request, const ProcessService &processes) {
            Result<PlatformRootPlan> plan = Result<PlatformRootPlan>::Failure(MakeError(SaveErrors::SaveRootPlatformUnsupported));
            switch (request.platform) {
                case SaveRootPlatform::Windows:
                    plan = WindowsPlan(processes);
                    break;
                case SaveRootPlatform::MacOS:
                    plan = MacOSPlan(processes);
                    break;
                case SaveRootPlatform::Linux:
                    plan = LinuxPlan(processes);
                    break;
                case SaveRootPlatform::Test:
                    if (!request.testStateRoot || request.testStateRoot->empty())
                        return Result<PlatformRootPlan>::Failure(
                            SafeFailure(SaveErrors::SaveRootConfigurationInvalid, request.platform, "sandbox lookup"));
                    plan =
                        Result<PlatformRootPlan>::Success({.stateRoot = *request.testStateRoot, .fixedComponents = {"horo", "products"}});
                    break;
                default:
                    break;
            }

            if (plan.HasError())
                return plan;
            if (!plan.Value().stateRoot.is_absolute())
                return Result<PlatformRootPlan>::Failure(
                    SafeFailure(SaveErrors::SaveRootConfigurationInvalid, request.platform, "absolute-path validation"));
            return plan;
        }

        [[nodiscard]] Result<std::filesystem::path> CanonicalApprovedRoot(const PlatformRootPlan &plan, const SaveRootPlatform platform) {
            std::error_code error;
            std::filesystem::create_directories(plan.stateRoot, error);
            if (error)
                return Result<std::filesystem::path>::Failure(
                    SafeFailure(SaveErrors::SaveRootUnavailable, platform, "state-directory creation", error));

            const auto status = std::filesystem::status(plan.stateRoot, error);
            if (error || status.type() != std::filesystem::file_type::directory)
                return Result<std::filesystem::path>::Failure(
                    SafeFailure(SaveErrors::SaveRootUnavailable, platform, "state-directory inspection", error));

            auto canonical = std::filesystem::canonical(plan.stateRoot, error);
            if (error || !canonical.is_absolute())
                return Result<std::filesystem::path>::Failure(
                    SafeFailure(SaveErrors::SaveRootUnavailable, platform, "state-directory canonicalization", error));
            return Result<std::filesystem::path>::Success(std::move(canonical));
        }

        [[nodiscard]] Result<std::filesystem::path> CreateContainedDirectory(const std::filesystem::path &canonicalParent,
                                                                             const std::string_view component,
                                                                             const SaveRootPlatform platform) {
            const std::filesystem::path candidate = canonicalParent / component;
            std::error_code error;
            auto entryStatus = std::filesystem::symlink_status(candidate, error);
            if (error && error != std::errc::no_such_file_or_directory)
                return Result<std::filesystem::path>::Failure(
                    SafeFailure(SaveErrors::SaveRootUnavailable, platform, "entry inspection", error));
            error.clear();

            if (entryStatus.type() == std::filesystem::file_type::not_found) {
                std::filesystem::create_directory(candidate, error);
                if (error) {
                    return Result<std::filesystem::path>::Failure(
                        SafeFailure(SaveErrors::SaveRootUnavailable, platform, "entry creation", error));
                }
                entryStatus = std::filesystem::symlink_status(candidate, error);
            }
            if (error)
                return Result<std::filesystem::path>::Failure(
                    SafeFailure(SaveErrors::SaveRootUnavailable, platform, "entry verification", error));
            if (entryStatus.type() != std::filesystem::file_type::directory)
                return Result<std::filesystem::path>::Failure(
                    SafeFailure(SaveErrors::SaveRootContainmentViolation, platform, "no-follow validation"));

            auto canonical = std::filesystem::canonical(candidate, error);
            if (error)
                return Result<std::filesystem::path>::Failure(
                    SafeFailure(SaveErrors::SaveRootUnavailable, platform, "entry canonicalization", error));
            if (canonical.parent_path() != canonicalParent)
                return Result<std::filesystem::path>::Failure(
                    SafeFailure(SaveErrors::SaveRootContainmentViolation, platform, "parent containment"));
            return Result<std::filesystem::path>::Success(std::move(canonical));
        }
    }  // namespace

    /** @copydoc ProductSaveRoot::ProductSaveRoot */
    ProductSaveRoot::ProductSaveRoot(ProductStorageId product, const SaveRootPlatform platform, std::filesystem::path canonicalPath)
        : product_(std::move(product)), platform_(platform), canonicalPath_(std::move(canonicalPath)) {}

    /** @copydoc ProductSaveRoot::Product */
    const ProductStorageId &ProductSaveRoot::Product() const noexcept {
        return product_;
    }

    /** @copydoc ProductSaveRoot::Platform */
    SaveRootPlatform ProductSaveRoot::Platform() const noexcept {
        return platform_;
    }

    /** @copydoc ProductSaveRoot::CanonicalPath */
    const std::filesystem::path &ProductSaveRoot::CanonicalPath() const noexcept {
        return canonicalPath_;
    }

    /** @copydoc ProductSaveRoot::IsValid */
    bool ProductSaveRoot::IsValid() const noexcept {
        return product_.IsValid() && canonicalPath_.is_absolute() && !canonicalPath_.empty();
    }

    /** @copydoc ResolveProductSaveRoot */
    Result<ProductSaveRoot> ResolveProductSaveRoot(const SaveRootResolutionRequest &request, const ProcessService &processes) {
        if (!request.product.IsValid())
            return Result<ProductSaveRoot>::Failure(MakeError(SaveErrors::IdentityInvalid));

        auto plan = PlatformPlan(request, processes);
        if (plan.HasError())
            return Result<ProductSaveRoot>::Failure(plan.ErrorValue());
        auto current = CanonicalApprovedRoot(plan.Value(), request.platform);
        if (current.HasError())
            return Result<ProductSaveRoot>::Failure(current.ErrorValue());

        for (const std::string_view component : plan.Value().fixedComponents) {
            current = CreateContainedDirectory(current.Value(), component, request.platform);
            if (current.HasError())
                return Result<ProductSaveRoot>::Failure(current.ErrorValue());
        }
        current = CreateContainedDirectory(current.Value(), request.product.ToString(), request.platform);
        if (current.HasError())
            return Result<ProductSaveRoot>::Failure(current.ErrorValue());

        return Result<ProductSaveRoot>::Success(ProductSaveRoot{request.product, request.platform, std::move(current.Value())});
    }
}  // namespace Horo::Runtime
