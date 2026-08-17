#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Extensions/ExtensionManifest.h"
#include "Horo/Foundation/Logging/Logger.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace Horo::Extensions {
    using Json = nlohmann::json;

    namespace {
        [[nodiscard]] bool IsCanonicalSemanticVersion(const std::string_view value) {
            if (value.empty() || value.size() > 64 || value.find('+') != std::string_view::npos)
                return false;
            const std::size_t dash = value.find('-');
            const std::string_view core = value.substr(0, dash);
            std::size_t componentStart = 0;
            for (int component = 0; component < 3; ++component) {
                const std::size_t end = component == 2 ? core.size() : core.find('.', componentStart);
                if (end == std::string_view::npos || end == componentStart)
                    return false;
                const std::string_view digits = core.substr(componentStart, end - componentStart);
                if ((digits.size() > 1 && digits.front() == '0') || !std::ranges::all_of(digits, [](const unsigned char character) {
                    return std::isdigit(character) != 0;
                })) {
                    return false;
                }
                componentStart = end + 1;
            }
            if (componentStart != core.size() + 1)
                return false;
            if (dash == std::string_view::npos)
                return true;
            const std::string_view prerelease = value.substr(dash + 1);
            if (prerelease.empty())
                return false;
            std::size_t identifierStart = 0;
            while (identifierStart <= prerelease.size()) {
                const std::size_t end = prerelease.find('.', identifierStart);
                const std::string_view identifier =
                    prerelease.substr(identifierStart,
                                      end == std::string_view::npos ? prerelease.size() - identifierStart : end - identifierStart);
                if (identifier.empty() || !std::ranges::all_of(identifier, [](const unsigned char character) {
                    return std::isalnum(character) != 0 || character == '-';
                })) {
                    return false;
                }
                const bool numeric = std::ranges::all_of(identifier, [](const unsigned char character) {
                    return std::isdigit(character) != 0;
                });
                if (numeric && identifier.size() > 1 && identifier.front() == '0')
                    return false;
                if (end == std::string_view::npos)
                    return true;
                identifierStart = end + 1;
            }
            return false;
        }
    }  // namespace

    Result<ExtensionManifest> ParseExtensionManifest(const std::string &jsonContent) {
        try {
            Json json = Json::parse(jsonContent);

            ExtensionManifest manifest;

            if (!json.is_object())
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension manifest must be an object."));
            // Top-level package fields are canonical. The nested package object
            // remains accepted while existing development extensions migrate.
            const Json &package = json.contains("package") && json["package"].is_object() ? json["package"] : json;

            if (package.contains("id") && package["id"].is_string())
                manifest.id = package["id"].get<std::string>();
            else
                return Result<ExtensionManifest>::Failure(MakeError(ExtensionErrors::InvalidManifest, "Missing extension package 'id'."));

            if (package.contains("version") && package["version"].is_string())
                manifest.version = package["version"].get<std::string>();
            if (!IsCanonicalSemanticVersion(manifest.version))
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Package 'version' must be canonical semantic version text."));

            if (package.contains("kind") && package["kind"].is_string())
                manifest.kind = package["kind"].get<std::string>();

            if (package.contains("displayName") && package["displayName"].is_string())
                manifest.displayName = package["displayName"].get<std::string>();

            if (package.contains("description") && package["description"].is_string())
                manifest.description = package["description"].get<std::string>();

            if (package.contains("author") && package["author"].is_string())
                manifest.author = package["author"].get<std::string>();

            if (json.contains("compatibility") && json["compatibility"].is_object()) {
                const auto &compatibility = json["compatibility"];
                if (compatibility.contains("engineMin") && compatibility["engineMin"].is_string())
                    manifest.engineMin = compatibility["engineMin"].get<std::string>();
                if (compatibility.contains("engineMax") && compatibility["engineMax"].is_string())
                    manifest.engineMax = compatibility["engineMax"].get<std::string>();
                if (compatibility.contains("sdkAbi") && compatibility["sdkAbi"].is_string())
                    manifest.sdkAbi = compatibility["sdkAbi"].get<std::string>();
                if (compatibility.contains("platforms") && compatibility["platforms"].is_array()) {
                    for (const auto &platform : compatibility["platforms"]) {
                        if (platform.is_string())
                            manifest.platforms.push_back(platform.get<std::string>());
                    }
                }
            }

            if (json.contains("modules")) {
                if (!json["modules"].is_array())
                    return Result<ExtensionManifest>::Failure(MakeError(ExtensionErrors::InvalidManifest, "'modules' must be an array."));
                for (const auto &module : json["modules"]) {
                    if (!module.is_object() || !module.contains("id") || !module["id"].is_string())
                        return Result<ExtensionManifest>::Failure(
                            MakeError(ExtensionErrors::InvalidManifest, "Every extension module requires a stable 'id'."));
                    ExtensionModuleManifest parsed{
                        .id = module["id"].get<std::string>(),
                        .version = module.value("version", manifest.version),
                        .kind = module.value("kind", std::string{}),
                        .entry = module.value("entry", std::string{}),
                    };
                    if (parsed.id.empty() || !IsCanonicalSemanticVersion(parsed.version))
                        return Result<ExtensionManifest>::Failure(
                            MakeError(ExtensionErrors::InvalidManifest, "Every extension module requires a canonical semantic version."));
                    if (std::ranges::any_of(manifest.modules, [&parsed](const ExtensionModuleManifest &existing) {
                        return existing.id == parsed.id;
                    })) {
                        return Result<ExtensionManifest>::Failure(
                            MakeError(ExtensionErrors::InvalidManifest, "Extension module identities must be unique within a package."));
                    }
                    manifest.modules.push_back(std::move(parsed));
                }
            }
            if (manifest.modules.empty()) {
                manifest.modules.push_back(ExtensionModuleManifest{
                    .id = manifest.id,
                    .version = manifest.version,
                    .kind = manifest.kind,
                });
            }

            if (json.contains("contributions")) {
                if (!json["contributions"].is_array())
                    return Result<ExtensionManifest>::Failure(
                        MakeError(ExtensionErrors::InvalidManifest, "'contributions' must be an array."));
                for (const auto &contribution : json["contributions"]) {
                    if (!contribution.is_object() || !contribution.contains("type") || !contribution["type"].is_string() ||
                        !contribution.contains("id") || !contribution["id"].is_string() || !contribution.contains("module") ||
                        !contribution["module"].is_string()) {
                        return Result<ExtensionManifest>::Failure(
                            MakeError(ExtensionErrors::InvalidManifest, "Every contribution requires string type, id, and module fields."));
                    }
                    ExtensionContributionManifest parsed{
                        .type = contribution["type"].get<std::string>(),
                        .id = contribution["id"].get<std::string>(),
                        .module = contribution["module"].get<std::string>(),
                    };
                    if (parsed.type.empty() || parsed.id.empty() ||
                        !std::ranges::any_of(manifest.modules,
                                             [&parsed](const ExtensionModuleManifest &module) {
                        return module.id == parsed.module;
                    }) ||
                        std::ranges::any_of(manifest.contributions, [&parsed](const ExtensionContributionManifest &existing) {
                        return existing.id == parsed.id;
                    })) {
                        return Result<ExtensionManifest>::Failure(
                            MakeError(ExtensionErrors::InvalidManifest,
                                      "Contribution IDs must be unique and reference a module in the same package."));
                    }
                    manifest.contributions.push_back(std::move(parsed));
                }
            }

            return Result<ExtensionManifest>::Success(manifest);
        } catch (const Json::exception &e) {
            return Result<ExtensionManifest>::Failure(
                MakeError(ExtensionErrors::InvalidManifest, std::string("JSON parsing error: ") + e.what()));
        }
    }
}  // namespace Horo::Extensions
