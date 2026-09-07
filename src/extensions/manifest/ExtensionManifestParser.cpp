#include "ExtensionManifestParsing.h"
#include "Horo/Extensions/ExtensionManifest.h"

#include <array>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>

namespace Horo::Extensions {
    namespace {
        using namespace ManifestParsing;

        constexpr std::size_t MaximumSemanticVersionBytes = 64;
        constexpr std::size_t MaximumEntryBytes = 512;

        [[nodiscard]] bool IsAsciiDigit(const unsigned char character) noexcept {
            return character >= '0' && character <= '9';
        }

        [[nodiscard]] bool IsAsciiLower(const unsigned char character) noexcept {
            return character >= 'a' && character <= 'z';
        }

        [[nodiscard]] bool IsCanonicalTokenCharacter(const unsigned char character) noexcept {
            return IsAsciiLower(character) || IsAsciiDigit(character) || character == '_' || character == '-';
        }

        [[nodiscard]] bool IsPrereleaseCharacter(const unsigned char character) noexcept {
            const bool upper = character >= 'A' && character <= 'Z';
            return IsAsciiLower(character) || upper || IsAsciiDigit(character) || character == '-';
        }

        [[nodiscard]] bool IsCanonicalNumericComponent(const std::string_view value) {
            const bool leadingZero = value.size() > 1 && value.front() == '0';
            return !value.empty() && !leadingZero && std::ranges::all_of(value, IsAsciiDigit);
        }

        [[nodiscard]] bool IsValidPrereleaseIdentifier(const std::string_view value) {
            const bool numeric = std::ranges::all_of(value, IsAsciiDigit);
            const bool leadingZero = numeric && value.size() > 1 && value.front() == '0';
            return !value.empty() && !leadingZero && std::ranges::all_of(value, IsPrereleaseCharacter);
        }

        [[nodiscard]] bool IsPrereleaseValid(const std::string_view prerelease) {
            if (prerelease.empty())
                return false;
            std::size_t identifierStart = 0;
            while (identifierStart <= prerelease.size()) {
                const std::size_t end = prerelease.find('.', identifierStart);
                if (const std::string_view identifier =
                        prerelease.substr(identifierStart,
                                          end == std::string_view::npos ? prerelease.size() - identifierStart : end - identifierStart);
                    !IsValidPrereleaseIdentifier(identifier)) {
                    return false;
                }
                if (end == std::string_view::npos)
                    return true;
                identifierStart = end + 1;
            }
            return false;
        }

        [[nodiscard]] bool IsCanonicalCoreVersion(const std::string_view core) {
            std::size_t componentStart = 0;
            for (int component = 0; component < 3; ++component) {
                const std::size_t end = component == 2 ? core.size() : core.find('.', componentStart);
                if (end == std::string_view::npos || end == componentStart)
                    return false;
                if (const std::string_view digits = core.substr(componentStart, end - componentStart);
                    !IsCanonicalNumericComponent(digits)) {
                    return false;
                }
                componentStart = end + 1;
            }
            return componentStart == core.size() + 1;
        }

        [[nodiscard]] bool IsCanonicalSemanticVersion(const std::string_view value) {
            if (value.empty() || value.size() > MaximumSemanticVersionBytes || value.find('+') != std::string_view::npos)
                return false;
            const std::size_t dash = value.find('-');
            if (!IsCanonicalCoreVersion(value.substr(0, dash)))
                return false;
            return dash == std::string_view::npos || IsPrereleaseValid(value.substr(dash + 1));
        }

        [[nodiscard]] bool IsCanonicalIdSegment(const std::string_view segment) {
            if (segment.empty())
                return false;
            if (!IsAsciiLower(static_cast<unsigned char>(segment.front())) || segment.back() == '-')
                return false;
            return std::ranges::all_of(segment, [](const unsigned char character) {
                return IsAsciiLower(character) || IsAsciiDigit(character) || character == '-';
            });
        }

        [[nodiscard]] bool IsCanonicalId(const std::string_view value, const std::size_t maximumBytes) {
            if (value.empty() || value.size() > maximumBytes)
                return false;
            std::size_t start = 0;
            while (start <= value.size()) {
                const std::size_t end = value.find('.', start);
                if (const std::string_view segment =
                        value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
                    !IsCanonicalIdSegment(segment)) {
                    return false;
                }
                if (end == std::string_view::npos)
                    return true;
                start = end + 1;
            }
            return false;
        }

        [[nodiscard]] bool IsCanonicalToken(const std::string_view value, const std::size_t maximumBytes) {
            if (value.empty() || value.size() > maximumBytes)
                return false;
            if (value.front() == '_' || value.back() == '_')
                return false;
            return std::ranges::all_of(value, IsCanonicalTokenCharacter);
        }

        [[nodiscard]] bool HasSafeEntryPrefix(const std::string_view value) {
            if (value.empty() || value.size() > MaximumEntryBytes)
                return false;
            if (value.front() == '/' || value.front() == '\\')
                return false;
            return value.find('\\') == std::string_view::npos && value.find(':') == std::string_view::npos;
        }

        [[nodiscard]] bool IsSafeEntryComponent(const std::string_view value) {
            return !value.empty() && value != "." && value != "..";
        }

        [[nodiscard]] bool IsSafeEntry(const std::string_view value) {
            if (!HasSafeEntryPrefix(value))
                return false;
            std::size_t start = 0;
            while (start <= value.size()) {
                const std::size_t end = value.find('/', start);
                if (const std::string_view component =
                        value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
                    !IsSafeEntryComponent(component)) {
                    return false;
                }
                if (end == std::string_view::npos)
                    return true;
                start = end + 1;
            }
            return false;
        }

        class ManifestValidator final {
        public:
            explicit ManifestValidator(const ExtensionManifestLimits &limits) : limits_(limits) {}

            [[nodiscard]] Result<ExtensionManifest> Validate(const Json &document) {
                if (!document.is_object())
                    return Failure("$", "extension.manifest.invalid_type", "Manifest root must be an object.");
                ExtensionManifest manifest;
                if (!ValidateRootFields(document) || !ValidateSchemaVersion(document, manifest.schemaVersion))
                    return CurrentFailure();

                const Json *package = SelectPackage(document);
                if (package == nullptr)
                    return CurrentFailure();
                if (const std::string packagePath = document.contains("package") ? "$.package" : "$";
                    !ParseManifestSections(document, *package, packagePath, manifest))
                    return CurrentFailure();
                return Result<ExtensionManifest>::Success(std::move(manifest));
            }

        private:
            [[nodiscard]] bool Reject(const std::string_view path, const std::string_view code, const std::string_view reason) {
                failure_ = ManifestError(path, code, reason);
                return false;
            }

            [[nodiscard]] Result<ExtensionManifest> Failure(const std::string_view path, const std::string_view code,
                                                            const std::string_view reason) const {
                return Result<ExtensionManifest>::Failure(ManifestError(path, code, reason));
            }

            [[nodiscard]] Result<ExtensionManifest> CurrentFailure() {
                return Result<ExtensionManifest>::Failure(std::move(*failure_));
            }

            [[nodiscard]] bool AllowFields(const Json &object, const std::string_view path,
                                           const std::initializer_list<std::string_view> allowed) {
                for (const auto &[key, value] : object.items()) {
                    static_cast<void>(value);
                    if (std::ranges::find(allowed, std::string_view{key}) == allowed.end())
                        return Reject(ChildPath(path, key), "extension.manifest.unknown_field", "Unknown field is not allowed.");
                }
                return true;
            }

            [[nodiscard]] bool ReadString(const Json &object, const std::string_view key, const std::string_view path, std::string &output,
                                          const std::size_t maximumBytes, const bool required) {
                const auto found = object.find(key);
                if (found == object.end())
                    return !required || Reject(ChildPath(path, key), "extension.manifest.missing_field", "Required field is missing.");
                if (!found->is_string())
                    return Reject(ChildPath(path, key), "extension.manifest.invalid_type", "Field must be a string.");
                output = found->get<std::string>();
                if ((required && output.empty()) || output.size() > maximumBytes)
                    return Reject(ChildPath(path, key), "extension.manifest.invalid_value", "String value is empty or exceeds its limit.");
                return true;
            }

            [[nodiscard]] bool ReadId(const Json &object, const std::string_view key, const std::string_view path, std::string &output) {
                return ReadString(object, key, path, output, limits_.maximumIdentifierBytes, true) &&
                       (IsCanonicalId(output, limits_.maximumIdentifierBytes) ||
                        Reject(ChildPath(path, key), "extension.manifest.invalid_identifier",
                               "Identity must use canonical lowercase dot-separated segments."));
            }

            [[nodiscard]] bool ValidateRootFields(const Json &document) {
                return AllowFields(document, "$",
                                   {"schemaVersion", "package", "id", "version", "kind", "displayName", "description", "author",
                                    "compatibility", "modules", "contributions"});
            }

            [[nodiscard]] bool ValidateSchemaVersion(const Json &document, std::uint32_t &schemaVersion) {
                const auto found = document.find("schemaVersion");
                if (found == document.end()) {
                    schemaVersion = 1;
                    return true;
                }
                if (!found->is_number_unsigned() || found->get<std::uint64_t>() != 1)
                    return Reject("$.schemaVersion", "extension.manifest.unsupported_schema", "Only integer schemaVersion 1 is supported.");
                schemaVersion = 1;
                return true;
            }

            [[nodiscard]] const Json *SelectPackage(const Json &document) {
                const auto nested = document.find("package");
                if (nested == document.end())
                    return &document;
                if (!nested->is_object()) {
                    static_cast<void>(Reject("$.package", "extension.manifest.invalid_type", "Package field must be an object."));
                    return nullptr;
                }
                constexpr std::array packageFields = {std::string_view{"id"},          std::string_view{"version"},
                                                      std::string_view{"kind"},        std::string_view{"displayName"},
                                                      std::string_view{"description"}, std::string_view{"author"}};
                for (const std::string_view field : packageFields) {
                    if (document.contains(field)) {
                        static_cast<void>(Reject(ChildPath("$", field), "extension.manifest.ambiguous_field",
                                                 "Package fields cannot appear both at the root and in $.package."));
                        return nullptr;
                    }
                }
                if (!AllowFields(*nested, "$.package", {"id", "version", "kind", "displayName", "description", "author"}))
                    return nullptr;
                return std::to_address(nested);
            }

            [[nodiscard]] bool ParsePackage(const Json &package, const std::string_view path, ExtensionManifest &manifest) {
                if (!ReadPackageIdentity(package, path, manifest))
                    return false;
                if (!IsCanonicalSemanticVersion(manifest.version))
                    return Reject(ChildPath(path, "version"), "extension.manifest.invalid_version",
                                  "Version must be canonical semantic version text.");
                if (!ReadPackageMetadata(package, path, manifest))
                    return false;
                return manifest.kind.empty() || IsCanonicalToken(manifest.kind, limits_.maximumIdentifierBytes) ||
                       Reject(ChildPath(path, "kind"), "extension.manifest.invalid_value", "Package kind is not canonical.");
            }

            [[nodiscard]] bool ReadPackageIdentity(const Json &package, const std::string_view path, ExtensionManifest &manifest) {
                return ReadId(package, "id", path, manifest.id) &&
                       ReadString(package, "version", path, manifest.version, MaximumSemanticVersionBytes, true);
            }

            [[nodiscard]] bool ReadPackageMetadata(const Json &package, const std::string_view path, ExtensionManifest &manifest) {
                return ReadString(package, "kind", path, manifest.kind, limits_.maximumIdentifierBytes, false) &&
                       ReadString(package, "displayName", path, manifest.displayName, limits_.maximumStringBytes, false) &&
                       ReadString(package, "description", path, manifest.description, limits_.maximumStringBytes, false) &&
                       ReadString(package, "author", path, manifest.author, limits_.maximumStringBytes, false);
            }

            [[nodiscard]] bool ParseManifestSections(const Json &document, const Json &package, const std::string_view packagePath,
                                                     ExtensionManifest &manifest) {
                if (!ParsePackage(package, packagePath, manifest))
                    return false;
                if (!ParseCompatibility(document, manifest))
                    return false;
                if (!ParseModules(document, manifest))
                    return false;
                return ParseContributions(document, manifest);
            }

            [[nodiscard]] bool ParseCompatibility(const Json &document, ExtensionManifest &manifest) {
                const auto found = document.find("compatibility");
                if (found == document.end())
                    return true;
                if (!found->is_object())
                    return Reject("$.compatibility", "extension.manifest.invalid_type", "Compatibility field must be an object.");
                if (!ReadCompatibilityFields(*found, manifest))
                    return false;
                if (!ValidateCompatibilityValues(manifest))
                    return false;
                return ParsePlatforms(*found, manifest.platforms);
            }

            [[nodiscard]] bool ReadCompatibilityFields(const Json &compatibility, ExtensionManifest &manifest) {
                return AllowFields(compatibility, "$.compatibility", {"engineMin", "engineMax", "sdkAbi", "platforms"}) &&
                       ReadString(compatibility, "engineMin", "$.compatibility", manifest.engineMin, MaximumSemanticVersionBytes, false) &&
                       ReadString(compatibility, "engineMax", "$.compatibility", manifest.engineMax, MaximumSemanticVersionBytes, false) &&
                       ReadString(compatibility, "sdkAbi", "$.compatibility", manifest.sdkAbi, limits_.maximumIdentifierBytes, false);
            }

            [[nodiscard]] bool ValidateCompatibilityValues(const ExtensionManifest &manifest) {
                const bool invalidMinimum = !manifest.engineMin.empty() && !IsCanonicalSemanticVersion(manifest.engineMin);
                if (const bool invalidMaximum = !manifest.engineMax.empty() && !IsCanonicalSemanticVersion(manifest.engineMax);
                    invalidMinimum || invalidMaximum) {
                    return Reject("$.compatibility", "extension.manifest.invalid_version",
                                  "Engine compatibility values must be canonical semantic versions.");
                }
                if (!manifest.sdkAbi.empty() && !IsCanonicalId(manifest.sdkAbi, limits_.maximumIdentifierBytes))
                    return Reject("$.compatibility.sdkAbi", "extension.manifest.invalid_identifier", "SDK ABI ID is not canonical.");
                return true;
            }

            [[nodiscard]] bool ParsePlatforms(const Json &compatibility, std::vector<std::string> &platforms) {
                const auto found = compatibility.find("platforms");
                if (found == compatibility.end())
                    return true;
                if (!found->is_array())
                    return Reject("$.compatibility.platforms", "extension.manifest.invalid_type", "Platforms must be an array.");
                if (found->size() > limits_.maximumPlatforms)
                    return Reject("$.compatibility.platforms", "extension.manifest.collection_limit",
                                  "Platform count exceeds the configured limit.");
                std::set<std::string, std::less<>> identities;
                platforms.reserve(found->size());
                for (std::size_t index = 0; index < found->size(); ++index) {
                    const std::string path = ElementPath("$.compatibility.platforms", index);
                    if (!AppendPlatform((*found)[index], path, identities, platforms))
                        return false;
                }
                return true;
            }

            [[nodiscard]] bool AppendPlatform(const Json &value, const std::string_view path,
                                              std::set<std::string, std::less<>> &identities, std::vector<std::string> &platforms) {
                if (!value.is_string())
                    return Reject(path, "extension.manifest.invalid_type", "Platform ID must be a string.");
                std::string platform = value.get<std::string>();
                if (!IsCanonicalToken(platform, limits_.maximumIdentifierBytes))
                    return Reject(path, "extension.manifest.invalid_identifier", "Platform ID is not canonical.");
                if (!identities.insert(platform).second)
                    return Reject(path, "extension.manifest.duplicate_identifier", "Platform ID must be unique.");
                platforms.push_back(std::move(platform));
                return true;
            }

            [[nodiscard]] bool ParseModules(const Json &document, ExtensionManifest &manifest) {
                const auto found = document.find("modules");
                if (found == document.end())
                    return Reject("$.modules", "extension.manifest.missing_field", "At least one explicit module is required.");
                if (!found->is_array())
                    return Reject("$.modules", "extension.manifest.invalid_type", "Modules must be an array.");
                if (found->empty() || found->size() > limits_.maximumModules)
                    return Reject("$.modules", "extension.manifest.collection_limit", "Module count must be within configured limits.");

                std::set<std::string, std::less<>> identities;
                manifest.modules.reserve(found->size());
                for (std::size_t index = 0; index < found->size(); ++index) {
                    const std::string path = ElementPath("$.modules", index);
                    if (!AppendModule((*found)[index], path, identities, manifest.modules))
                        return false;
                }
                return true;
            }

            [[nodiscard]] bool AppendModule(const Json &value, const std::string_view path, std::set<std::string, std::less<>> &identities,
                                            std::vector<ExtensionModuleManifest> &modules) {
                ExtensionModuleManifest moduleManifest;
                if (!ParseModule(value, path, moduleManifest))
                    return false;
                if (!identities.insert(moduleManifest.id).second)
                    return Reject(ChildPath(path, "id"), "extension.manifest.duplicate_identifier",
                                  "Module ID must be unique within the package.");
                modules.push_back(std::move(moduleManifest));
                return true;
            }

            [[nodiscard]] bool ParseModule(const Json &value, const std::string_view path, ExtensionModuleManifest &moduleManifest) {
                if (!value.is_object())
                    return Reject(path, "extension.manifest.invalid_type", "Module must be an object.");
                if (!AllowFields(value, path, {"id", "version", "kind", "entry", "roles", "dependencies", "exports", "imports"}))
                    return false;
                return ReadModuleFields(value, path, moduleManifest) && ValidateModuleValues(path, moduleManifest) &&
                       ParseModuleRoles(value, path, moduleManifest.roles) &&
                       ParseModuleDependencies(value, path, moduleManifest.dependencies) &&
                       ParseModuleExports(value, path, moduleManifest.exports) && ParseModuleImports(value, path, moduleManifest.imports);
            }

            [[nodiscard]] bool ReadModuleFields(const Json &value, const std::string_view path, ExtensionModuleManifest &moduleManifest) {
                return ReadId(value, "id", path, moduleManifest.id) &&
                       ReadString(value, "version", path, moduleManifest.version, MaximumSemanticVersionBytes, true) &&
                       ReadString(value, "kind", path, moduleManifest.kind, limits_.maximumIdentifierBytes, true) &&
                       ReadString(value, "entry", path, moduleManifest.entry, MaximumEntryBytes, false);
            }

            [[nodiscard]] bool ValidateModuleValues(const std::string_view path, const ExtensionModuleManifest &moduleManifest) {
                if (!IsCanonicalSemanticVersion(moduleManifest.version))
                    return Reject(ChildPath(path, "version"), "extension.manifest.invalid_version",
                                  "Module version must be canonical semantic version text.");
                if (!IsCanonicalToken(moduleManifest.kind, limits_.maximumIdentifierBytes))
                    return Reject(ChildPath(path, "kind"), "extension.manifest.invalid_value", "Module kind is not canonical.");
                if (!moduleManifest.entry.empty() && !IsSafeEntry(moduleManifest.entry))
                    return Reject(ChildPath(path, "entry"), "extension.manifest.invalid_path",
                                  "Module entry must be a safe package-relative path.");
                return true;
            }

            [[nodiscard]] static std::optional<ExtensionModuleRole> ParseRole(const std::string_view value) {
                if (value == "backend-capability")
                    return ExtensionModuleRole::BackendCapability;
                if (value == "editor-presentation")
                    return ExtensionModuleRole::EditorPresentation;
                if (value == "headless-tooling")
                    return ExtensionModuleRole::HeadlessTooling;
                if (value == "script-provider")
                    return ExtensionModuleRole::ScriptProvider;
                if (value == "runtime-participant")
                    return ExtensionModuleRole::RuntimeParticipant;
                return std::nullopt;
            }

            [[nodiscard]] bool ParseModuleRoles(const Json &module, const std::string_view path, std::vector<ExtensionModuleRole> &roles) {
                const auto found = module.find("roles");
                const std::string rolesPath = ChildPath(path, "roles");
                if (found == module.end())
                    return true;
                if (!found->is_array())
                    return Reject(rolesPath, "extension.manifest.invalid_type", "Module roles must be an array.");
                if (found->empty() || found->size() > 5)
                    return Reject(rolesPath, "extension.manifest.collection_limit", "Module role count must be between one and five.");
                for (std::size_t index = 0; index < found->size(); ++index) {
                    const Json &encoded = (*found)[index];
                    const std::string elementPath = ElementPath(rolesPath, index);
                    if (!encoded.is_string())
                        return Reject(elementPath, "extension.manifest.invalid_type", "Module role must be a string.");
                    const auto role = ParseRole(encoded.get_ref<const std::string &>());
                    if (!role.has_value())
                        return Reject(elementPath, "extension.manifest.invalid_value", "Module role is not recognized.");
                    if (std::ranges::find(roles, *role) != roles.end())
                        return Reject(elementPath, "extension.manifest.duplicate_identifier", "Module role must be unique.");
                    roles.push_back(*role);
                }
                return true;
            }

            [[nodiscard]] bool ParseModuleDependencies(const Json &module, const std::string_view path,
                                                       std::vector<std::string> &dependencies) {
                const auto found = module.find("dependencies");
                if (found == module.end())
                    return true;
                const std::string dependenciesPath = ChildPath(path, "dependencies");
                if (!found->is_array())
                    return Reject(dependenciesPath, "extension.manifest.invalid_type", "Module dependencies must be an array.");
                if (found->size() > limits_.maximumModules)
                    return Reject(dependenciesPath, "extension.manifest.collection_limit", "Module dependency count exceeds its limit.");
                for (std::size_t index = 0; index < found->size(); ++index) {
                    const Json &encoded = (*found)[index];
                    const std::string elementPath = ElementPath(dependenciesPath, index);
                    if (!encoded.is_string())
                        return Reject(elementPath, "extension.manifest.invalid_type", "Module dependency must be a string.");
                    std::string dependency = encoded.get<std::string>();
                    if (!IsCanonicalId(dependency, limits_.maximumIdentifierBytes))
                        return Reject(elementPath, "extension.manifest.invalid_identifier", "Module dependency is not canonical.");
                    if (std::ranges::find(dependencies, dependency) != dependencies.end())
                        return Reject(elementPath, "extension.manifest.duplicate_identifier", "Module dependency must be unique.");
                    dependencies.push_back(std::move(dependency));
                }
                return true;
            }

            template <typename Entry, typename ParseEntry>
            [[nodiscard]] bool ParseModuleServiceEntries(const Json &module, const std::string_view path, const std::string_view fieldName,
                                                         const std::string_view entryName, std::vector<Entry> &entries,
                                                         ParseEntry parseEntry) {
                const auto found = module.find(fieldName);
                if (found == module.end())
                    return true;
                const std::string entriesPath = ChildPath(path, fieldName);
                if (!found->is_array())
                    return Reject(entriesPath, "extension.manifest.invalid_type",
                                  "Module " + std::string{entryName} + "s must be an array.");
                if (found->size() > limits_.maximumContributions)
                    return Reject(entriesPath, "extension.manifest.collection_limit",
                                  "Module " + std::string{entryName} + " count exceeds its limit.");

                std::set<std::string, std::less<>> identities;
                for (std::size_t index = 0; index < found->size(); ++index) {
                    const Json &encoded = (*found)[index];
                    const std::string elementPath = ElementPath(entriesPath, index);
                    Entry value;
                    if (!encoded.is_object())
                        return Reject(elementPath, "extension.manifest.invalid_type",
                                      "Module " + std::string{entryName} + " must be an object.");
                    if (!parseEntry(encoded, elementPath, value))
                        return false;
                    if (!identities.insert(value.id).second)
                        return Reject(ChildPath(elementPath, "id"), "extension.manifest.duplicate_identifier",
                                      "Service " + std::string{entryName} + " ID must be unique within a module.");
                    entries.push_back(std::move(value));
                }
                return true;
            }

            [[nodiscard]] bool ParseModuleExports(const Json &module, const std::string_view path,
                                                  std::vector<ExtensionServiceExportManifest> &exports) {
                return ParseModuleServiceEntries(module, path, "exports", "export", exports,
                                                 [this](const Json &encoded, const std::string_view elementPath,
                                                        ExtensionServiceExportManifest &value) {
                    if (!AllowFields(encoded, elementPath, {"id", "contract", "version"}) ||
                        !ReadId(encoded, "id", elementPath, value.id) || !ReadId(encoded, "contract", elementPath, value.contract) ||
                        !ReadString(encoded, "version", elementPath, value.version, MaximumSemanticVersionBytes, true))
                        return false;
                    return IsCanonicalSemanticVersion(value.version) ||
                           Reject(ChildPath(elementPath, "version"), "extension.manifest.invalid_version",
                                  "Service export version must be canonical semantic version text.");
                });
            }

            [[nodiscard]] bool ParseModuleImports(const Json &module, const std::string_view path,
                                                  std::vector<ExtensionServiceImportManifest> &imports) {
                return ParseModuleServiceEntries(module, path, "imports", "import", imports,
                                                 [this](const Json &encoded, const std::string_view elementPath,
                                                        ExtensionServiceImportManifest &value) {
                    if (!AllowFields(encoded, elementPath, {"id", "service", "contract", "minimumVersion"}) ||
                        !ReadId(encoded, "id", elementPath, value.id) || !ReadId(encoded, "service", elementPath, value.service) ||
                        !ReadId(encoded, "contract", elementPath, value.contract) ||
                        !ReadString(encoded, "minimumVersion", elementPath, value.minimumVersion, MaximumSemanticVersionBytes, true))
                        return false;
                    return IsCanonicalSemanticVersion(value.minimumVersion) ||
                           Reject(ChildPath(elementPath, "minimumVersion"), "extension.manifest.invalid_version",
                                  "Service import minimum version must be canonical semantic version text.");
                });
            }

            [[nodiscard]] bool ParseContributions(const Json &document, ExtensionManifest &manifest) {
                const auto found = document.find("contributions");
                if (found == document.end())
                    return true;
                if (!found->is_array())
                    return Reject("$.contributions", "extension.manifest.invalid_type", "Contributions must be an array.");
                if (found->size() > limits_.maximumContributions)
                    return Reject("$.contributions", "extension.manifest.collection_limit",
                                  "Contribution count exceeds the configured limit.");

                std::set<std::string, std::less<>> identities;
                manifest.contributions.reserve(found->size());
                for (std::size_t index = 0; index < found->size(); ++index) {
                    const std::string path = ElementPath("$.contributions", index);
                    if (!AppendContribution((*found)[index], path, identities, manifest.modules, manifest.contributions))
                        return false;
                }
                return true;
            }

            [[nodiscard]] bool AppendContribution(const Json &value, const std::string_view path,
                                                  std::set<std::string, std::less<>> &identities,
                                                  const std::vector<ExtensionModuleManifest> &modules,
                                                  std::vector<ExtensionContributionManifest> &contributions) {
                ExtensionContributionManifest contribution;
                if (!ParseContribution(value, path, contribution))
                    return false;
                if (!identities.insert(contribution.id).second)
                    return Reject(ChildPath(path, "id"), "extension.manifest.duplicate_identifier",
                                  "Contribution ID must be unique within the package.");
                if (!HasOwningModule(modules, contribution.owningModule))
                    return Reject(ChildPath(path, "module"), "extension.manifest.unresolved_reference",
                                  "Contribution references an undeclared module.");
                contributions.push_back(std::move(contribution));
                return true;
            }

            [[nodiscard]] bool ParseContribution(const Json &value, const std::string_view path,
                                                 ExtensionContributionManifest &contribution) {
                if (!value.is_object())
                    return Reject(path, "extension.manifest.invalid_type", "Contribution must be an object.");
                if (!AllowFields(value, path, {"type", "id", "module"}))
                    return false;
                if (!ReadString(value, "type", path, contribution.type, limits_.maximumIdentifierBytes, true) ||
                    !ReadId(value, "id", path, contribution.id) || !ReadId(value, "module", path, contribution.owningModule)) {
                    return false;
                }
                return IsCanonicalId(contribution.type, limits_.maximumIdentifierBytes) ||
                       Reject(ChildPath(path, "type"), "extension.manifest.invalid_identifier", "Contribution type is not canonical.");
            }

            [[nodiscard]] static bool HasOwningModule(const std::vector<ExtensionModuleManifest> &modules,
                                                      const std::string_view moduleId) {
                return std::ranges::any_of(modules, [moduleId](const ExtensionModuleManifest &moduleManifest) {
                    return moduleManifest.id == moduleId;
                });
            }

            const ExtensionManifestLimits &limits_;
            std::optional<Error> failure_;
        };
    }  // namespace

    /** @copydoc ParseExtensionManifest */
    Result<ExtensionManifest> ParseExtensionManifest(const std::string_view jsonContent, const ExtensionManifestLimits &limits) {
        auto parsed = ManifestParsing::ParseBoundedJson(jsonContent, limits);
        if (parsed.HasError())
            return Result<ExtensionManifest>::Failure(parsed.ErrorValue());
        return ManifestValidator{limits}.Validate(parsed.Value());
    }
}  // namespace Horo::Extensions
