#include "Horo/Release/DistributionModel.h"

#include "Horo/Release/ReleaseErrors.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace Horo::Release {
    namespace {
        using Capability = DistributionCapability;
        using Layout = DistributionInstallLayout;

        struct FormatDescriptor final {
            DistributionPackageFormat format;
            std::array<bool, 3> platforms;
            DistributionPackageCapabilities capabilities;
        };

        constexpr std::array
            FormatDescriptors{FormatDescriptor{DistributionPackageFormat::WindowsMsi,
                                               {true, false, false},
                                               {Layout::SystemManaged, Capability::Supported, Capability::Supported, Capability::Required,
                                                Capability::Supported, Capability::Required, Capability::Unsupported}},
                              FormatDescriptor{DistributionPackageFormat::WindowsExeInstaller,
                                               {true, false, false},
                                               {Layout::SystemManaged, Capability::Supported, Capability::Supported, Capability::Required,
                                                Capability::Supported, Capability::Required, Capability::Supported}},
                              FormatDescriptor{DistributionPackageFormat::ZipArchive,
                                               {true, true, false},
                                               {Layout::Portable, Capability::Unsupported, Capability::Unsupported, Capability::Supported,
                                                Capability::Supported, Capability::Supported, Capability::Supported}},
                              FormatDescriptor{DistributionPackageFormat::MacDmg,
                                               {false, true, false},
                                               {Layout::ApplicationBundle, Capability::Supported, Capability::Supported,
                                                Capability::Supported, Capability::Supported, Capability::Required, Capability::Supported}},
                              FormatDescriptor{DistributionPackageFormat::MacPkg,
                                               {false, true, false},
                                               {Layout::SystemManaged, Capability::Supported, Capability::Supported, Capability::Required,
                                                Capability::Supported, Capability::Required, Capability::Unsupported}},
                              FormatDescriptor{DistributionPackageFormat::MacAppBundle,
                                               {false, true, false},
                                               {Layout::ApplicationBundle, Capability::Supported, Capability::Supported,
                                                Capability::Supported, Capability::Supported, Capability::Required, Capability::Supported}},
                              FormatDescriptor{DistributionPackageFormat::LinuxAppImage,
                                               {false, false, true},
                                               {Layout::Portable, Capability::Supported, Capability::Supported, Capability::Supported,
                                                Capability::Supported, Capability::Supported, Capability::Supported}},
                              FormatDescriptor{DistributionPackageFormat::TarGzip,
                                               {false, false, true},
                                               {Layout::Portable, Capability::Unsupported, Capability::Unsupported, Capability::Supported,
                                                Capability::Supported, Capability::Supported, Capability::Supported}},
                              FormatDescriptor{DistributionPackageFormat::LinuxDeb,
                                               {false, false, true},
                                               {Layout::SystemManaged, Capability::Supported, Capability::Supported, Capability::Required,
                                                Capability::Supported, Capability::Required, Capability::Unsupported}},
                              FormatDescriptor{DistributionPackageFormat::LinuxRpm,
                                               {false, false, true},
                                               {Layout::SystemManaged, Capability::Supported, Capability::Supported, Capability::Required,
                                                Capability::Supported, Capability::Required, Capability::Unsupported}},
                              FormatDescriptor{DistributionPackageFormat::StorePackage,
                                               {true, true, true},
                                               {Layout::StoreManaged, Capability::Supported, Capability::Required, Capability::Required,
                                                Capability::Required, Capability::Required, Capability::Supported}}};

        [[nodiscard]] bool IsIdentityCharacter(const char value) noexcept {
            return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '.' || value == '_' || value == '-';
        }

        [[nodiscard]] bool IsValidIdentity(const std::string_view value) noexcept {
            return !value.empty() && value.size() <= MaximumDistributionIdentityBytes && value.front() >= 'a' && value.front() <= 'z' &&
                   std::ranges::all_of(value, IsIdentityCharacter);
        }

        [[nodiscard]] bool IsKnownProduct(const DistributionProductKind kind) noexcept {
            return kind >= DistributionProductKind::Editor && kind <= DistributionProductKind::GameDedicatedServer;
        }

        [[nodiscard]] bool IsKnownPlatform(const DistributionPlatform platform) noexcept {
            return platform >= DistributionPlatform::Windows && platform <= DistributionPlatform::Linux;
        }

        [[nodiscard]] bool IsKnownArchitecture(const DistributionArchitecture architecture) noexcept {
            return architecture >= DistributionArchitecture::X64 && architecture <= DistributionArchitecture::Arm64;
        }

        [[nodiscard]] bool IsKnownArtifactClass(const DistributionArtifactClass artifactClass) noexcept {
            return artifactClass >= DistributionArtifactClass::InstallableProduct &&
                   artifactClass <= DistributionArtifactClass::Diagnostics;
        }

        [[nodiscard]] const FormatDescriptor *FindDescriptor(const DistributionPackageFormat format) noexcept {
            const auto descriptor = std::ranges::find(FormatDescriptors, format, &FormatDescriptor::format);
            return descriptor == FormatDescriptors.end() ? nullptr : &*descriptor;
        }

        [[nodiscard]] bool ProductVersionMatches(const DistributionArtifactIdentity &artifact) noexcept {
            const bool gameProduct = artifact.product.kind == DistributionProductKind::GameRuntime ||
                                     artifact.product.kind == DistributionProductKind::GameDedicatedServer;
            return gameProduct == std::holds_alternative<GameProductVersion>(artifact.version);
        }

        [[nodiscard]] bool ValidProductIdentity(const DistributionProductIdentity &product) noexcept {
            if (!IsKnownProduct(product.kind))
                return false;
            const bool renderer = product.kind == DistributionProductKind::RendererComponent;
            return renderer ? IsValidIdentity(product.componentId) : product.componentId.empty();
        }

        [[nodiscard]] bool SupportsArtifactClass(const DistributionArtifactIdentity &artifact,
                                                 const DistributionPackageFormat format) noexcept {
            if (artifact.artifactClass == DistributionArtifactClass::InstallableProduct)
                return artifact.installation.has_value() && IsValidIdentity(artifact.installation->value);
            if (artifact.installation.has_value())
                return false;
            return format == DistributionPackageFormat::ZipArchive || format == DistributionPackageFormat::TarGzip;
        }

        [[nodiscard]] bool SupportsProduct(const DistributionProductIdentity &product, const DistributionPackageFormat format) noexcept {
            if (product.kind == DistributionProductKind::RendererComponent)
                return format == DistributionPackageFormat::ZipArchive || format == DistributionPackageFormat::TarGzip;
            if (product.kind == DistributionProductKind::GameDedicatedServer)
                return format != DistributionPackageFormat::MacAppBundle && format != DistributionPackageFormat::LinuxAppImage &&
                       format != DistributionPackageFormat::StorePackage;
            if (product.kind == DistributionProductKind::PublicSdk)
                return format != DistributionPackageFormat::WindowsExeInstaller && format != DistributionPackageFormat::MacDmg &&
                       format != DistributionPackageFormat::MacAppBundle && format != DistributionPackageFormat::LinuxAppImage &&
                       format != DistributionPackageFormat::StorePackage;
            return true;
        }
    }  // namespace

    /** @copydoc DescribeDistributionPackageFormat */
    Result<DistributionPackageCapabilities> DescribeDistributionPackageFormat(const DistributionPackageFormat format,
                                                                              const DistributionPlatform platform) {
        const FormatDescriptor *descriptor = FindDescriptor(format);
        if (!descriptor || !IsKnownPlatform(platform) || !descriptor->platforms[static_cast<std::size_t>(platform)])
            return Result<DistributionPackageCapabilities>::Failure(MakeError(ReleaseErrors::DistributionCombinationUnsupported));
        return Result<DistributionPackageCapabilities>::Success(descriptor->capabilities);
    }

    /** @copydoc ValidateDistributionPackageSelection */
    Result<DistributionPackageSelection> ValidateDistributionPackageSelection(const DistributionArtifactIdentity &artifact,
                                                                              const DistributionPackageFormat format) {
        if (!ValidProductIdentity(artifact.product) || !IsKnownArchitecture(artifact.architecture) ||
            !IsKnownArtifactClass(artifact.artifactClass) || !IsValidIdentity(artifact.build.value) ||
            !IsValidIdentity(artifact.package.value) || !ProductVersionMatches(artifact))
            return Result<DistributionPackageSelection>::Failure(MakeError(ReleaseErrors::DistributionIdentityInvalid));
        auto capabilities = DescribeDistributionPackageFormat(format, artifact.platform);
        if (capabilities.HasError() || !SupportsArtifactClass(artifact, format) || !SupportsProduct(artifact.product, format))
            return Result<DistributionPackageSelection>::Failure(MakeError(ReleaseErrors::DistributionCombinationUnsupported));
        return Result<DistributionPackageSelection>::Success({artifact, format, std::move(capabilities).Value()});
    }
}  // namespace Horo::Release
