#include "Horo/Release/ReleaseProfile.h"

#include "Horo/Release/ReleaseErrors.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <type_traits>
#include <utility>

namespace Horo::Release {
    namespace {
        using Json = nlohmann::json;
        using OrderedJson = nlohmann::ordered_json;

        constexpr std::size_t MaximumProfileJsonDepth = 10;
        constexpr std::size_t MaximumProfileJsonKeyBytes = 64;

        [[nodiscard]] bool ValidBoundedIdentity(const std::string_view value, const std::size_t maximum) noexcept {
            return value.size() <= maximum && IsValidDistributionIdentity(value);
        }

        [[nodiscard]] Error ProfileError(const ErrorCodeDescriptor &descriptor, std::string detail = {}) {
            return MakeError(descriptor, std::move(detail));
        }

        [[nodiscard]] bool ValidLimits(const ReleaseProfileLimits &limits) noexcept {
            constexpr ReleaseProfileLimits maximum;
            return limits.documentBytes > 0U && limits.documentBytes <= maximum.documentBytes && limits.presets > 0U &&
                   limits.presets <= maximum.presets && limits.inheritanceDepth > 0U &&
                   limits.inheritanceDepth <= maximum.inheritanceDepth && limits.destinationsPerPreset > 0U &&
                   limits.destinationsPerPreset <= maximum.destinationsPerPreset && limits.capabilitiesPerPreset > 0U &&
                   limits.capabilitiesPerPreset <= maximum.capabilitiesPerPreset && limits.availableCapabilities > 0U &&
                   limits.availableCapabilities <= maximum.availableCapabilities && limits.identityBytes > 0U &&
                   limits.identityBytes <= MaximumDistributionIdentityBytes;
        }

        [[nodiscard]] bool HasExactKeys(const Json &value, const std::initializer_list<std::string_view> required,
                                        const std::initializer_list<std::string_view> optional = {}) {
            if (!value.is_object())
                return false;
            for (const auto &[key, ignored] : value.items()) {
                static_cast<void>(ignored);
                const bool known = std::ranges::find(required, key) != required.end() || std::ranges::find(optional, key) != optional.end();
                if (!known)
                    return false;
            }
            return std::ranges::all_of(required, [&](const std::string_view key) {
                return value.contains(key);
            });
        }

        struct DecodeGuard final {
            // These are fixed schema-parser safety bounds, not product identity policy.
            std::array<std::set<std::string, std::less<>>, MaximumProfileJsonDepth + 2U> keys;
            bool valid{true};

            bool operator()(const int depth, const Json::parse_event_t event, const Json &value) {
                if (depth < 0 || static_cast<std::size_t>(depth) >= keys.size() - 1U) {
                    valid = false;
                    return false;
                }
                const auto index = static_cast<std::size_t>(depth);
                if (event == Json::parse_event_t::object_start)
                    keys[index + 1U].clear();
                else if (event == Json::parse_event_t::key)
                    valid &= value.is_string() && value.get_ref<const std::string &>().size() <= MaximumProfileJsonKeyBytes &&
                             keys[index].insert(value.get_ref<const std::string &>()).second;
                else if (event == Json::parse_event_t::value && value.is_string())
                    valid &= value.get_ref<const std::string &>().size() <= MaximumDistributionIdentityBytes;
                return valid;
            }
        };

        template <typename Id>
        [[nodiscard]] Result<std::vector<Id>> DecodeIdentities(const Json &value, const std::size_t maximumCount,
                                                               const ReleaseProfileLimits &limits) {
            if (!value.is_array() || value.size() > maximumCount)
                return Result<std::vector<Id>>::Failure(ProfileError(ReleaseErrors::ProfileLimitExceeded));
            std::vector<Id> result;
            result.reserve(value.size());
            for (const Json &encoded : value) {
                if (!encoded.is_string() || !ValidBoundedIdentity(encoded.get_ref<const std::string &>(), limits.identityBytes))
                    return Result<std::vector<Id>>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
                result.push_back({encoded.get<std::string>()});
            }
            std::ranges::sort(result, {}, &Id::value);
            if (std::ranges::adjacent_find(result, {}, &Id::value) != result.end())
                return Result<std::vector<Id>>::Failure(
                    ProfileError(ReleaseErrors::ProfileInvalid, "Profile identity list contains duplicates."));
            return Result<std::vector<Id>>::Success(std::move(result));
        }

        [[nodiscard]] std::optional<DistributionProductKind> ProductFromString(const std::string_view value) noexcept {
            using enum DistributionProductKind;
            if (value == "engine-editor")
                return Editor;
            if (value == "engine-cli")
                return EngineCli;
            if (value == "package-tool-cli")
                return PackageToolCli;
            if (value == "sdk")
                return PublicSdk;
            if (value == "game-runtime")
                return GameRuntime;
            if (value == "game-dedicated-server")
                return GameDedicatedServer;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view ProductToString(const DistributionProductKind value) noexcept {
            using enum DistributionProductKind;
            switch (value) {
                case Editor:
                    return "engine-editor";
                case EngineCli:
                    return "engine-cli";
                case PackageToolCli:
                    return "package-tool-cli";
                case PublicSdk:
                    return "sdk";
                case GameRuntime:
                    return "game-runtime";
                case GameDedicatedServer:
                    return "game-dedicated-server";
                case RendererComponent:
                    return "renderer-component";
            }
            return {};
        }

        [[nodiscard]] std::optional<DistributionArtifactClass> ArtifactClassFromString(const std::string_view value) noexcept {
            using enum DistributionArtifactClass;
            if (value == "installable")
                return InstallableProduct;
            if (value == "symbols")
                return Symbols;
            if (value == "developer-diagnostics")
                return Diagnostics;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view ArtifactClassToString(const DistributionArtifactClass value) noexcept {
            using enum DistributionArtifactClass;
            switch (value) {
                case InstallableProduct:
                    return "installable";
                case Symbols:
                    return "symbols";
                case Diagnostics:
                    return "developer-diagnostics";
            }
            return {};
        }

        [[nodiscard]] std::optional<DistributionPlatform> PlatformFromString(const std::string_view value) noexcept {
            using enum DistributionPlatform;
            if (value == "windows")
                return Windows;
            if (value == "macos")
                return MacOS;
            if (value == "linux")
                return Linux;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view PlatformToString(const DistributionPlatform value) noexcept {
            using enum DistributionPlatform;
            switch (value) {
                case Windows:
                    return "windows";
                case MacOS:
                    return "macos";
                case Linux:
                    return "linux";
            }
            return {};
        }

        struct FormatName final {
            DistributionPackageFormat value;
            std::string_view name;
        };

        constexpr std::array FormatNames{FormatName{DistributionPackageFormat::WindowsMsi, "windows-msi"},
                                         FormatName{DistributionPackageFormat::WindowsExeInstaller, "windows-exe"},
                                         FormatName{DistributionPackageFormat::ZipArchive, "zip"},
                                         FormatName{DistributionPackageFormat::MacDmg, "mac-dmg"},
                                         FormatName{DistributionPackageFormat::MacPkg, "mac-pkg"},
                                         FormatName{DistributionPackageFormat::MacAppBundle, "mac-app"},
                                         FormatName{DistributionPackageFormat::LinuxAppImage, "linux-appimage"},
                                         FormatName{DistributionPackageFormat::TarGzip, "tar-gzip"},
                                         FormatName{DistributionPackageFormat::LinuxDeb, "linux-deb"},
                                         FormatName{DistributionPackageFormat::LinuxRpm, "linux-rpm"},
                                         FormatName{DistributionPackageFormat::StorePackage, "store"}};

        [[nodiscard]] std::optional<DistributionPackageFormat> FormatFromString(const std::string_view value) noexcept {
            const auto match = std::ranges::find(FormatNames, value, &FormatName::name);
            return match == FormatNames.end() ? std::nullopt : std::optional{match->value};
        }

        [[nodiscard]] std::string_view FormatToString(const DistributionPackageFormat value) noexcept {
            const auto match = std::ranges::find(FormatNames, value, &FormatName::value);
            return match == FormatNames.end() ? std::string_view{} : match->name;
        }

        [[nodiscard]] std::optional<ReleaseSymbolPolicy> SymbolsFromString(const std::string_view value) noexcept {
            using enum ReleaseSymbolPolicy;
            if (value == "omit")
                return Omit;
            if (value == "separate")
                return SeparateArtifact;
            if (value == "required-separate")
                return RequiredSeparateArtifact;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view SymbolsToString(const ReleaseSymbolPolicy value) noexcept {
            using enum ReleaseSymbolPolicy;
            switch (value) {
                case Omit:
                    return "omit";
                case SeparateArtifact:
                    return "separate";
                case RequiredSeparateArtifact:
                    return "required-separate";
            }
            return {};
        }

        [[nodiscard]] std::optional<ReleaseSigningPolicy> SigningFromString(const std::string_view value) noexcept {
            using enum ReleaseSigningPolicy;
            if (value == "disabled")
                return Disabled;
            if (value == "when-supported")
                return WhenSupported;
            if (value == "required")
                return Required;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view SigningToString(const ReleaseSigningPolicy value) noexcept {
            using enum ReleaseSigningPolicy;
            switch (value) {
                case Disabled:
                    return "disabled";
                case WhenSupported:
                    return "when-supported";
                case Required:
                    return "required";
            }
            return {};
        }

        [[nodiscard]] std::optional<ReleaseAssetPolicy> AssetsFromString(const std::string_view value) noexcept {
            using enum ReleaseAssetPolicy;
            if (value == "omit")
                return Omit;
            if (value == "single-package")
                return SinglePackage;
            if (value == "chunked")
                return Chunked;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view AssetsToString(const ReleaseAssetPolicy value) noexcept {
            using enum ReleaseAssetPolicy;
            switch (value) {
                case Omit:
                    return "omit";
                case SinglePackage:
                    return "single-package";
                case Chunked:
                    return "chunked";
            }
            return {};
        }

        [[nodiscard]] Result<ReleaseContentPolicy> DecodeContent(const Json &value) {
            if (!HasExactKeys(value, {"executables", "runtimeLibraries", "assets", "developerDiagnostics", "crashReports"}) ||
                !value.at("executables").is_boolean() || !value.at("runtimeLibraries").is_boolean() ||
                !value.at("developerDiagnostics").is_boolean() || !value.at("crashReports").is_boolean())
                return Result<ReleaseContentPolicy>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            if (!value.at("assets").is_string())
                return Result<ReleaseContentPolicy>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            const auto assets = AssetsFromString(value.at("assets").get_ref<const std::string &>());
            if (!assets.has_value())
                return Result<ReleaseContentPolicy>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            return Result<ReleaseContentPolicy>::Success({value.at("executables").get<bool>(), value.at("runtimeLibraries").get<bool>(),
                                                          *assets, value.at("developerDiagnostics").get<bool>(),
                                                          value.at("crashReports").get<bool>()});
        }

        template <typename Enum, typename Parser> [[nodiscard]] Result<Enum> DecodeEnum(const Json &value, Parser parser) {
            if (!value.is_string())
                return Result<Enum>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            const auto decoded = parser(value.get_ref<const std::string &>());
            if (!decoded.has_value())
                return Result<Enum>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            return Result<Enum>::Success(*decoded);
        }

        [[nodiscard]] Result<DistributionProductIdentity> DecodeProduct(const Json &value, const ReleaseProfileLimits &limits) {
            if (!HasExactKeys(value, {"kind"}, {"componentId"}) || !value.at("kind").is_string())
                return Result<DistributionProductIdentity>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            const std::string &name = value.at("kind").get_ref<const std::string &>();
            if (name == "renderer-component") {
                if (!value.contains("componentId") || !value.at("componentId").is_string() ||
                    !ValidBoundedIdentity(value.at("componentId").get_ref<const std::string &>(), limits.identityBytes))
                    return Result<DistributionProductIdentity>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
                return Result<DistributionProductIdentity>::Success(
                    {DistributionProductKind::RendererComponent, value.at("componentId").get<std::string>()});
            }
            const auto kind = ProductFromString(name);
            if (!kind.has_value() || value.contains("componentId"))
                return Result<DistributionProductIdentity>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            return Result<DistributionProductIdentity>::Success({*kind, {}});
        }

        [[nodiscard]] Result<void> DecodePresetPolicies(const Json &value, ReleaseProfilePreset &preset,
                                                        const ReleaseProfileLimits &limits) {
            if (value.contains("product")) {
                auto product = DecodeProduct(value.at("product"), limits);
                if (product.HasError())
                    return Result<void>::Failure(product.ErrorValue());
                preset.product = std::move(product).Value();
            }
            const auto decode = [&]<typename Value, typename Parser>(const std::string_view key, std::optional<Value> &target,
                                                                     Parser parser) -> Result<void> {
                if (!value.contains(key))
                    return Result<void>::Success();
                auto decoded = DecodeEnum<Value>(value.at(key), parser);
                if (decoded.HasError())
                    return Result<void>::Failure(decoded.ErrorValue());
                target = decoded.Value();
                return Result<void>::Success();
            };
            if (const auto decoded = decode("artifactClass", preset.artifactClass, ArtifactClassFromString);
                decoded.HasError() || decode("platform", preset.platform, PlatformFromString).HasError() ||
                decode("packageFormat", preset.packageFormat, FormatFromString).HasError() ||
                decode("symbols", preset.symbols, SymbolsFromString).HasError() ||
                decode("signing", preset.signing, SigningFromString).HasError())
                return Result<void>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            if (value.contains("content")) {
                auto content = DecodeContent(value.at("content"));
                if (content.HasError())
                    return Result<void>::Failure(content.ErrorValue());
                preset.content = content.Value();
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> DecodePresetBooleans(const Json &value, ReleaseProfilePreset &preset) {
            for (const auto &[key, target] :
                 std::array{std::pair{"notarizationRequired", &preset.notarizationRequired},
                            std::pair{"includeLicensesAndNotices", &preset.includeLicensesAndNotices},
                            std::pair{"includeReleaseNotes", &preset.includeReleaseNotes},
                            std::pair{"updateEligible", &preset.updateEligible}, std::pair{"patchEligible", &preset.patchEligible}}) {
                if (value.contains(key)) {
                    if (!value.at(key).is_boolean())
                        return Result<void>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
                    *target = value.at(key).get<bool>();
                }
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> DecodePresetLists(const Json &value, ReleaseProfilePreset &preset, const ReleaseProfileLimits &limits) {
            if (value.contains("eligibleDestinations")) {
                auto destinations =
                    DecodeIdentities<ReleaseDestinationId>(value.at("eligibleDestinations"), limits.destinationsPerPreset, limits);
                if (destinations.HasError())
                    return Result<void>::Failure(destinations.ErrorValue());
                preset.eligibleDestinations = std::move(destinations).Value();
            }
            if (value.contains("requiredCapabilities")) {
                auto capabilities =
                    DecodeIdentities<ReleaseCapabilityId>(value.at("requiredCapabilities"), limits.capabilitiesPerPreset, limits);
                if (capabilities.HasError())
                    return Result<void>::Failure(capabilities.ErrorValue());
                preset.requiredCapabilities = std::move(capabilities).Value();
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<ReleaseProfilePreset> DecodePreset(const Json &value, const ReleaseProfileLimits &limits) {
            if (const std::initializer_list<std::string_view> optional{"parent", "product", "artifactClass", "platform", "packageFormat",
                                                                       "content", "symbols", "signing", "notarizationRequired",
                                                                       "includeLicensesAndNotices", "includeReleaseNotes", "updateEligible",
                                                                       "patchEligible", "eligibleDestinations", "requiredCapabilities"};
                !HasExactKeys(value, {"id"}, optional) || !value.at("id").is_string())
                return Result<ReleaseProfilePreset>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            ReleaseProfilePreset preset{{value.at("id").get<std::string>()}};
            if (value.contains("parent"))
                preset.parent = ReleaseProfileId{value.at("parent").is_string() ? value.at("parent").get<std::string>() : std::string{}};
            if (const auto policies = DecodePresetPolicies(value, preset, limits); policies.HasError())
                return Result<ReleaseProfilePreset>::Failure(policies.ErrorValue());
            if (const auto booleans = DecodePresetBooleans(value, preset); booleans.HasError())
                return Result<ReleaseProfilePreset>::Failure(booleans.ErrorValue());
            if (const auto lists = DecodePresetLists(value, preset, limits); lists.HasError())
                return Result<ReleaseProfilePreset>::Failure(lists.ErrorValue());
            return Result<ReleaseProfilePreset>::Success(std::move(preset));
        }

        [[nodiscard]] OrderedJson EncodeProduct(const DistributionProductIdentity &product) {
            OrderedJson result{{"kind", ProductToString(product.kind)}};
            if (product.kind == DistributionProductKind::RendererComponent)
                result["componentId"] = product.componentId;
            return result;
        }

        [[nodiscard]] OrderedJson EncodeContent(const ReleaseContentPolicy &content) {
            return {{"executables", content.executables},
                    {"runtimeLibraries", content.runtimeLibraries},
                    {"assets", AssetsToString(content.assets)},
                    {"developerDiagnostics", content.developerDiagnostics},
                    {"crashReports", content.crashReports}};
        }

        template <typename Id> [[nodiscard]] OrderedJson EncodeIdentities(const std::vector<Id> &values) {
            OrderedJson result = OrderedJson::array();
            for (const Id &value : values)
                result.push_back(value.value);
            return result;
        }

        void EncodePresetTarget(const ReleaseProfilePreset &preset, OrderedJson &result) {
            if (preset.product)
                result["product"] = EncodeProduct(*preset.product);
            if (preset.artifactClass)
                result["artifactClass"] = ArtifactClassToString(*preset.artifactClass);
            if (preset.platform)
                result["platform"] = PlatformToString(*preset.platform);
            if (preset.packageFormat)
                result["packageFormat"] = FormatToString(*preset.packageFormat);
            if (preset.content)
                result["content"] = EncodeContent(*preset.content);
        }

        void EncodePresetPolicy(const ReleaseProfilePreset &preset, OrderedJson &result) {
            if (preset.symbols)
                result["symbols"] = SymbolsToString(*preset.symbols);
            if (preset.signing)
                result["signing"] = SigningToString(*preset.signing);
            if (preset.notarizationRequired.has_value())
                result["notarizationRequired"] = *preset.notarizationRequired;
            if (preset.includeLicensesAndNotices.has_value())
                result["includeLicensesAndNotices"] = *preset.includeLicensesAndNotices;
            if (preset.includeReleaseNotes.has_value())
                result["includeReleaseNotes"] = *preset.includeReleaseNotes;
            if (preset.updateEligible.has_value())
                result["updateEligible"] = *preset.updateEligible;
            if (preset.patchEligible.has_value())
                result["patchEligible"] = *preset.patchEligible;
        }

        [[nodiscard]] OrderedJson EncodePreset(const ReleaseProfilePreset &preset) {
            OrderedJson result{{"id", preset.id.value}};
            if (preset.parent)
                result["parent"] = preset.parent->value;
            EncodePresetTarget(preset, result);
            EncodePresetPolicy(preset, result);
            if (preset.eligibleDestinations)
                result["eligibleDestinations"] = EncodeIdentities(*preset.eligibleDestinations);
            if (preset.requiredCapabilities)
                result["requiredCapabilities"] = EncodeIdentities(*preset.requiredCapabilities);
            return result;
        }

        [[nodiscard]] bool ValidOptionalIdentity(const std::optional<ReleaseProfileId> &id, const ReleaseProfileLimits &limits) noexcept {
            return !id || ValidBoundedIdentity(id->value, limits.identityBytes);
        }

        [[nodiscard]] bool ValidProduct(const std::optional<DistributionProductIdentity> &product,
                                        const ReleaseProfileLimits &limits) noexcept {
            if (!product)
                return true;
            if (ProductToString(product->kind).empty())
                return false;
            const bool renderer = product->kind == DistributionProductKind::RendererComponent;
            return renderer ? ValidBoundedIdentity(product->componentId, limits.identityBytes) : product->componentId.empty();
        }

        template <typename Id>
        [[nodiscard]] bool CanonicalizeIdentities(std::optional<std::vector<Id>> &values, const std::size_t maximum,
                                                  const ReleaseProfileLimits &limits) {
            if (!values)
                return true;
            if (values->size() > maximum || !std::ranges::all_of(*values, [&](const Id &value) {
                return ValidBoundedIdentity(value.value, limits.identityBytes);
            }))
                return false;
            std::ranges::sort(*values, {}, &Id::value);
            return std::ranges::adjacent_find(*values, {}, &Id::value) == values->end();
        }

        [[nodiscard]] bool ValidPresetEnums(const ReleaseProfilePreset &preset) noexcept {
            return (!preset.artifactClass || !ArtifactClassToString(*preset.artifactClass).empty()) &&
                   (!preset.platform || !PlatformToString(*preset.platform).empty()) &&
                   (!preset.packageFormat || !FormatToString(*preset.packageFormat).empty()) &&
                   (!preset.content || !AssetsToString(preset.content->assets).empty()) &&
                   (!preset.symbols || !SymbolsToString(*preset.symbols).empty()) &&
                   (!preset.signing || !SigningToString(*preset.signing).empty());
        }

        [[nodiscard]] bool CanonicalizePreset(ReleaseProfilePreset &preset, const ReleaseProfileLimits &limits) {
            return ValidBoundedIdentity(preset.id.value, limits.identityBytes) && ValidOptionalIdentity(preset.parent, limits) &&
                   ValidProduct(preset.product, limits) && ValidPresetEnums(preset) &&
                   CanonicalizeIdentities(preset.eligibleDestinations, limits.destinationsPerPreset, limits) &&
                   CanonicalizeIdentities(preset.requiredCapabilities, limits.capabilitiesPerPreset, limits);
        }

        [[nodiscard]] const ReleaseProfilePreset *FindPreset(const std::vector<ReleaseProfilePreset> &presets,
                                                             const std::string_view id) noexcept {
            const auto preset = std::ranges::lower_bound(presets, id, {}, [](const ReleaseProfilePreset &candidate) {
                return candidate.id.value;
            });
            return preset == presets.end() || preset->id.value != id ? nullptr : std::to_address(preset);
        }

        [[nodiscard]] std::string CycleDetail(const std::vector<const ReleaseProfilePreset *> &lineage, const std::string_view repeated) {
            const auto first = std::ranges::find(lineage, repeated, [](const ReleaseProfilePreset *preset) {
                return preset->id.value;
            });
            std::string detail{"Release profile inheritance cycle: "};
            for (auto preset = first; preset != lineage.end(); ++preset)
                detail += (*preset)->id.value + " -> ";
            detail += repeated;
            return detail;
        }

        [[nodiscard]] Result<void> ValidateInheritance(const std::vector<ReleaseProfilePreset> &presets,
                                                       const ReleaseProfileLimits &limits) {
            for (const ReleaseProfilePreset &start : presets) {
                std::vector<const ReleaseProfilePreset *> lineage;
                const ReleaseProfilePreset *current = &start;
                while (current->parent) {
                    if (lineage.size() >= limits.inheritanceDepth)
                        return Result<void>::Failure(ProfileError(ReleaseErrors::ProfileLimitExceeded));
                    lineage.push_back(current);
                    const ReleaseProfilePreset *parent = FindPreset(presets, current->parent->value);
                    if (!parent)
                        return Result<void>::Failure(
                            ProfileError(ReleaseErrors::ProfileInvalid, "Missing inherited preset: " + current->parent->value));
                    if (std::ranges::find(lineage, parent->id.value, [](const ReleaseProfilePreset *preset) {
                        return preset->id.value;
                    }) != lineage.end())
                        return Result<void>::Failure(ProfileError(ReleaseErrors::ProfileConflict, CycleDetail(lineage, parent->id.value)));
                    current = parent;
                }
            }
            return Result<void>::Success();
        }

        struct EffectiveFields final {
            std::optional<DistributionProductIdentity> product;
            std::optional<DistributionArtifactClass> artifactClass;
            std::optional<DistributionPlatform> platform;
            std::optional<DistributionPackageFormat> packageFormat;
            std::optional<ReleaseContentPolicy> content;
            std::optional<ReleaseSymbolPolicy> symbols;
            std::optional<ReleaseSigningPolicy> signing;
            std::optional<bool> notarizationRequired;
            std::optional<bool> includeLicensesAndNotices;
            std::optional<bool> includeReleaseNotes;
            std::optional<bool> updateEligible;
            std::optional<bool> patchEligible;
            std::optional<std::vector<ReleaseDestinationId>> destinations;
            std::optional<std::vector<ReleaseCapabilityId>> capabilities;
        };

        template <typename T> void Apply(std::optional<T> &target, const std::optional<T> &source) {
            if (source)
                target = source;
        }

        void ApplyPreset(EffectiveFields &target, const ReleaseProfilePreset &preset) {
            Apply(target.product, preset.product);
            Apply(target.artifactClass, preset.artifactClass);
            Apply(target.platform, preset.platform);
            Apply(target.packageFormat, preset.packageFormat);
            Apply(target.content, preset.content);
            Apply(target.symbols, preset.symbols);
            Apply(target.signing, preset.signing);
            Apply(target.notarizationRequired, preset.notarizationRequired);
            Apply(target.includeLicensesAndNotices, preset.includeLicensesAndNotices);
            Apply(target.includeReleaseNotes, preset.includeReleaseNotes);
            Apply(target.updateEligible, preset.updateEligible);
            Apply(target.patchEligible, preset.patchEligible);
            Apply(target.destinations, preset.eligibleDestinations);
            Apply(target.capabilities, preset.requiredCapabilities);
        }

        [[nodiscard]] bool Complete(const EffectiveFields &fields) noexcept {
            return fields.product.has_value() && fields.artifactClass.has_value() && fields.platform.has_value() &&
                   fields.packageFormat.has_value() && fields.content.has_value() && fields.symbols.has_value() &&
                   fields.signing.has_value() && fields.notarizationRequired.has_value() && fields.includeLicensesAndNotices.has_value() &&
                   fields.includeReleaseNotes.has_value() && fields.updateEligible.has_value() && fields.patchEligible.has_value() &&
                   fields.destinations.has_value() && fields.capabilities.has_value();
        }

        [[nodiscard]] bool NotarizationFormat(const DistributionPackageFormat format) noexcept {
            using enum DistributionPackageFormat;
            return format == MacDmg || format == MacPkg || format == MacAppBundle;
        }

        [[nodiscard]] Result<void> ValidateEffectiveContent(const EffectiveFields &fields) {
            if (!Complete(fields) ||
                (!fields.content->executables && !fields.content->runtimeLibraries && fields.content->assets == ReleaseAssetPolicy::Omit &&
                 !fields.content->developerDiagnostics && !fields.content->crashReports) ||
                AssetsToString(fields.content->assets).empty())
                return Result<void>::Failure(ProfileError(ReleaseErrors::ProfileInvalid, "Resolved profile is incomplete or empty."));
            if (*fields.artifactClass == DistributionArtifactClass::Diagnostics && !fields.content->developerDiagnostics)
                return Result<void>::Failure(
                    ProfileError(ReleaseErrors::ProfileConflict, "Diagnostics artifact omits diagnostic content."));
            if (*fields.artifactClass == DistributionArtifactClass::Symbols && *fields.symbols == ReleaseSymbolPolicy::Omit)
                return Result<void>::Failure(ProfileError(ReleaseErrors::ProfileConflict, "Symbols artifact uses the omit policy."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateEffectiveSigning(const EffectiveFields &fields) {
            auto format =
                ValidateDistributionProductPackageFormat(*fields.product, *fields.artifactClass, *fields.platform, *fields.packageFormat);
            if (format.HasError())
                return Result<void>::Failure(format.ErrorValue());
            if ((format.Value().signing == DistributionCapability::Required && *fields.signing == ReleaseSigningPolicy::Disabled) ||
                (format.Value().signing == DistributionCapability::Unsupported && *fields.signing == ReleaseSigningPolicy::Required))
                return Result<void>::Failure(ProfileError(ReleaseErrors::ProfileConflict, "Signing policy conflicts with package format."));
            if (*fields.notarizationRequired &&
                (*fields.platform != DistributionPlatform::MacOS || !NotarizationFormat(*fields.packageFormat) ||
                 *fields.signing == ReleaseSigningPolicy::Disabled))
                return Result<void>::Failure(ProfileError(ReleaseErrors::ProfileConflict, "Notarization requires a signed macOS package."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateEffectiveCapabilities(const EffectiveFields &fields,
                                                                 const std::span<const ReleaseCapabilityId> availableCapabilities) {
            for (const ReleaseCapabilityId &required : *fields.capabilities) {
                if (std::ranges::find(availableCapabilities, required.value, &ReleaseCapabilityId::value) == availableCapabilities.end())
                    return Result<void>::Failure(
                        ProfileError(ReleaseErrors::ProfileCapabilityUnsupported, "Required capability is unavailable: " + required.value));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateEffective(const EffectiveFields &fields,
                                                     const std::span<const ReleaseCapabilityId> availableCapabilities) {
            if (const auto content = ValidateEffectiveContent(fields); content.HasError())
                return content;
            if (const auto signing = ValidateEffectiveSigning(fields); signing.HasError())
                return signing;
            return ValidateEffectiveCapabilities(fields, availableCapabilities);
        }
    }  // namespace

    EffectiveReleaseProfile::EffectiveReleaseProfile(ReleaseProfileId id, ReleaseProfilePreset resolved)
        : m_id(std::move(id)), m_product(std::move(*resolved.product)), m_artifactClass(*resolved.artifactClass),
          m_platform(*resolved.platform), m_packageFormat(*resolved.packageFormat), m_content(*resolved.content),
          m_symbols(*resolved.symbols), m_signing(*resolved.signing), m_notarizationRequired(*resolved.notarizationRequired),
          m_includeLicensesAndNotices(*resolved.includeLicensesAndNotices), m_includeReleaseNotes(*resolved.includeReleaseNotes),
          m_updateEligible(*resolved.updateEligible), m_patchEligible(*resolved.patchEligible),
          m_destinations(std::move(*resolved.eligibleDestinations)), m_capabilities(std::move(*resolved.requiredCapabilities)) {}

    /** @copydoc EffectiveReleaseProfile::Id */
    const ReleaseProfileId &EffectiveReleaseProfile::Id() const noexcept {
        return m_id;
    }

    /** @copydoc EffectiveReleaseProfile::Product */
    const DistributionProductIdentity &EffectiveReleaseProfile::Product() const noexcept {
        return m_product;
    }

    /** @copydoc EffectiveReleaseProfile::ArtifactClass */
    DistributionArtifactClass EffectiveReleaseProfile::ArtifactClass() const noexcept {
        return m_artifactClass;
    }

    /** @copydoc EffectiveReleaseProfile::Platform */
    DistributionPlatform EffectiveReleaseProfile::Platform() const noexcept {
        return m_platform;
    }

    /** @copydoc EffectiveReleaseProfile::PackageFormat */
    DistributionPackageFormat EffectiveReleaseProfile::PackageFormat() const noexcept {
        return m_packageFormat;
    }

    /** @copydoc EffectiveReleaseProfile::Content */
    const ReleaseContentPolicy &EffectiveReleaseProfile::Content() const noexcept {
        return m_content;
    }

    /** @copydoc EffectiveReleaseProfile::Symbols */
    ReleaseSymbolPolicy EffectiveReleaseProfile::Symbols() const noexcept {
        return m_symbols;
    }

    /** @copydoc EffectiveReleaseProfile::Signing */
    ReleaseSigningPolicy EffectiveReleaseProfile::Signing() const noexcept {
        return m_signing;
    }

    /** @copydoc EffectiveReleaseProfile::NotarizationRequired */
    bool EffectiveReleaseProfile::NotarizationRequired() const noexcept {
        return m_notarizationRequired;
    }

    /** @copydoc EffectiveReleaseProfile::IncludesLicensesAndNotices */
    bool EffectiveReleaseProfile::IncludesLicensesAndNotices() const noexcept {
        return m_includeLicensesAndNotices;
    }

    /** @copydoc EffectiveReleaseProfile::IncludesReleaseNotes */
    bool EffectiveReleaseProfile::IncludesReleaseNotes() const noexcept {
        return m_includeReleaseNotes;
    }

    /** @copydoc EffectiveReleaseProfile::UpdateEligible */
    bool EffectiveReleaseProfile::UpdateEligible() const noexcept {
        return m_updateEligible;
    }

    /** @copydoc EffectiveReleaseProfile::PatchEligible */
    bool EffectiveReleaseProfile::PatchEligible() const noexcept {
        return m_patchEligible;
    }

    /** @copydoc EffectiveReleaseProfile::EligibleDestinations */
    std::span<const ReleaseDestinationId> EffectiveReleaseProfile::EligibleDestinations() const noexcept {
        return m_destinations;
    }

    /** @copydoc EffectiveReleaseProfile::RequiredCapabilities */
    std::span<const ReleaseCapabilityId> EffectiveReleaseProfile::RequiredCapabilities() const noexcept {
        return m_capabilities;
    }

    /** @copydoc EffectiveReleaseProfile::SerializeCanonical */
    std::string EffectiveReleaseProfile::SerializeCanonical() const {
        OrderedJson result{{"schemaVersion", 1U},
                           {"id", m_id.value},
                           {"product", EncodeProduct(m_product)},
                           {"artifactClass", ArtifactClassToString(m_artifactClass)},
                           {"platform", PlatformToString(m_platform)},
                           {"packageFormat", FormatToString(m_packageFormat)},
                           {"content", EncodeContent(m_content)},
                           {"symbols", SymbolsToString(m_symbols)},
                           {"signing", SigningToString(m_signing)},
                           {"notarizationRequired", m_notarizationRequired},
                           {"includeLicensesAndNotices", m_includeLicensesAndNotices},
                           {"includeReleaseNotes", m_includeReleaseNotes},
                           {"updateEligible", m_updateEligible},
                           {"patchEligible", m_patchEligible},
                           {"eligibleDestinations", EncodeIdentities(m_destinations)},
                           {"requiredCapabilities", EncodeIdentities(m_capabilities)}};
        return result.dump(2) + '\n';
    }

    ReleaseProfileCatalog::ReleaseProfileCatalog(std::vector<ReleaseProfilePreset> presets, const ReleaseProfileLimits &limits)
        : m_presets(std::move(presets)), m_limits(limits) {}

    /** @copydoc ReleaseProfileCatalog::Create */
    Result<ReleaseProfileCatalog> ReleaseProfileCatalog::Create(std::vector<ReleaseProfilePreset> presets,
                                                                const ReleaseProfileLimits &limits) {
        if (!ValidLimits(limits))
            return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileLimitExceeded));
        if (presets.empty())
            return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
        if (presets.size() > limits.presets)
            return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileLimitExceeded));
        if (!std::ranges::all_of(presets, [&](ReleaseProfilePreset &preset) {
            return CanonicalizePreset(preset, limits);
        }))
            return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
        std::ranges::sort(presets, {}, [](const ReleaseProfilePreset &preset) {
            return preset.id.value;
        });
        if (std::ranges::adjacent_find(presets, {}, [](const ReleaseProfilePreset &preset) {
            return preset.id.value;
        }) != presets.end())
            return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileConflict, "Preset IDs must be unique."));
        if (const auto inheritance = ValidateInheritance(presets, limits); inheritance.HasError())
            return Result<ReleaseProfileCatalog>::Failure(inheritance.ErrorValue());
        return Result<ReleaseProfileCatalog>::Success(ReleaseProfileCatalog{std::move(presets), limits});
    }

    /** @copydoc ReleaseProfileCatalog::Parse */
    Result<ReleaseProfileCatalog> ReleaseProfileCatalog::Parse(const std::string_view json, const ReleaseProfileLimits &limits) {
        if (!ValidLimits(limits) || json.size() > limits.documentBytes)
            return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileLimitExceeded));
        try {
            DecodeGuard guard;
            const Json root = Json::parse(json, std::ref(guard));
            if (!guard.valid || !HasExactKeys(root, {"schemaVersion", "presets"}) || !root.at("schemaVersion").is_number_unsigned() ||
                root.at("schemaVersion") != 1U || !root.at("presets").is_array())
                return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            if (root.at("presets").empty())
                return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
            if (root.at("presets").size() > limits.presets)
                return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileLimitExceeded));
            std::vector<ReleaseProfilePreset> presets;
            presets.reserve(root.at("presets").size());
            for (const Json &encoded : root.at("presets")) {
                auto preset = DecodePreset(encoded, limits);
                if (preset.HasError())
                    return Result<ReleaseProfileCatalog>::Failure(preset.ErrorValue());
                presets.push_back(std::move(preset).Value());
            }
            return Create(std::move(presets), limits);
        } catch (const Json::exception &) {
            return Result<ReleaseProfileCatalog>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
        }
    }

    /** @copydoc ReleaseProfileCatalog::Resolve */
    Result<EffectiveReleaseProfile> ReleaseProfileCatalog::Resolve(ReleaseProfileId id,
                                                                   const std::span<const ReleaseCapabilityId> availableCapabilities) const {
        if (!ValidBoundedIdentity(id.value, m_limits.identityBytes))
            return Result<EffectiveReleaseProfile>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
        if (availableCapabilities.size() > m_limits.availableCapabilities)
            return Result<EffectiveReleaseProfile>::Failure(ProfileError(ReleaseErrors::ProfileLimitExceeded));
        std::set<std::string_view, std::less<>> uniqueCapabilities;
        for (const ReleaseCapabilityId &capability : availableCapabilities) {
            if (!ValidBoundedIdentity(capability.value, m_limits.identityBytes) || !uniqueCapabilities.insert(capability.value).second)
                return Result<EffectiveReleaseProfile>::Failure(ProfileError(ReleaseErrors::ProfileInvalid));
        }
        std::vector<const ReleaseProfilePreset *> lineage;
        lineage.reserve(std::min(m_presets.size(), m_limits.inheritanceDepth));
        std::set<std::string_view, std::less<>> visited;
        std::optional<ReleaseProfileId> current{std::move(id)};
        while (current) {
            if (lineage.size() >= m_limits.inheritanceDepth)
                return Result<EffectiveReleaseProfile>::Failure(ProfileError(ReleaseErrors::ProfileLimitExceeded));
            const ReleaseProfilePreset *preset = FindPreset(m_presets, current->value);
            if (!preset)
                return Result<EffectiveReleaseProfile>::Failure(
                    ProfileError(ReleaseErrors::ProfileInvalid, "Missing inherited preset: " + current->value));
            if (!visited.insert(preset->id.value).second)
                return Result<EffectiveReleaseProfile>::Failure(
                    ProfileError(ReleaseErrors::ProfileConflict, CycleDetail(lineage, preset->id.value)));
            lineage.push_back(preset);
            current = preset->parent;
        }
        EffectiveFields fields;
        for (auto preset = lineage.rbegin(); preset != lineage.rend(); ++preset)
            ApplyPreset(fields, **preset);
        if (auto validation = ValidateEffective(fields, availableCapabilities); validation.HasError())
            return Result<EffectiveReleaseProfile>::Failure(validation.ErrorValue());
        ReleaseProfilePreset resolved{.product = std::move(fields.product),
                                      .artifactClass = fields.artifactClass,
                                      .platform = fields.platform,
                                      .packageFormat = fields.packageFormat,
                                      .content = fields.content,
                                      .symbols = fields.symbols,
                                      .signing = fields.signing,
                                      .notarizationRequired = fields.notarizationRequired,
                                      .includeLicensesAndNotices = fields.includeLicensesAndNotices,
                                      .includeReleaseNotes = fields.includeReleaseNotes,
                                      .updateEligible = fields.updateEligible,
                                      .patchEligible = fields.patchEligible,
                                      .eligibleDestinations = std::move(fields.destinations),
                                      .requiredCapabilities = std::move(fields.capabilities)};
        return Result<EffectiveReleaseProfile>::Success(EffectiveReleaseProfile{lineage.front()->id, std::move(resolved)});
    }

    /** @copydoc ReleaseProfileCatalog::SerializeCanonical */
    std::string ReleaseProfileCatalog::SerializeCanonical() const {
        OrderedJson presets = OrderedJson::array();
        for (const ReleaseProfilePreset &preset : m_presets)
            presets.push_back(EncodePreset(preset));
        return OrderedJson{{"schemaVersion", 1U}, {"presets", std::move(presets)}}.dump(2) + '\n';
    }

    /** @copydoc ReleaseProfileCatalog::Presets */
    std::span<const ReleaseProfilePreset> ReleaseProfileCatalog::Presets() const noexcept {
        return m_presets;
    }
}  // namespace Horo::Release
