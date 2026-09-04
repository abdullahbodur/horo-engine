#include "Horo/Packages/PackageFileManifest.h"

#include "PackageValidationDetail.h"

#include <array>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

namespace Horo::Packages {
    namespace {
        using Json = nlohmann::json;

        /** @brief Refuses duplicate JSON object keys and excessive nesting before building their DOM values. */
        struct DecodeGuard {
            std::array<std::set<std::string>, 9> keys;
            bool valid = true;

            bool operator()(const int depth, const Json::parse_event_t event, Json &value) {
                if (depth >= 8) {
                    valid = false;
                    return false;
                }
                const auto index = static_cast<std::size_t>(depth);
                if (event == Json::parse_event_t::object_start) {
                    keys[index + 1].clear();
                } else if (event == Json::parse_event_t::key) {
                    valid &= keys[index].insert(value.get<std::string>()).second;
                }
                return valid;
            }
        };

        /** @brief Decodes a single exact-shape entry and verifies optional contribution-root containment. */
        [[nodiscard]] Result<PackageFileEntry> DecodeEntry(const Json &value) {
            if (!value.is_object() || value.size() != 5 || !value.at("size").is_number_unsigned() || !value.at("executable").is_boolean()) {
                return Result<PackageFileEntry>::Failure(MakeError(Detail::InvalidManifest));
            }
            auto path = PackagePath::Parse(value.at("path").get<std::string>());
            auto digest = ParseSha256(value.at("sha256").get<std::string>());
            if (path.HasError() || digest.HasError()) {
                return Result<PackageFileEntry>::Failure(MakeError(Detail::InvalidManifest));
            }
            std::optional<PackagePath> root;
            if (!value.at("contributionRoot").is_null()) {
                auto parsed = PackagePath::Parse(value.at("contributionRoot").get<std::string>());
                if (parsed.HasError() || !path.Value().Value().starts_with(parsed.Value().Value() + "/")) {
                    return Result<PackageFileEntry>::Failure(MakeError(Detail::InvalidManifest));
                }
                root.emplace(std::move(parsed).Value());
            }
            return Result<PackageFileEntry>::Success({.id = {.path = std::move(path).Value()},
                                                      .digest = digest.Value(),
                                                      .size = value.at("size").get<std::uint64_t>(),
                                                      .executable = value.at("executable").get<bool>(),
                                                      .contributionRoot = std::move(root)});
        }

        /** @brief Checks the exact root schema before iterating a bounded file array. */
        [[nodiscard]] bool HasSchema(const Json &root, const PackageValidationLimits &limits) {
            return root.is_object() && root.size() == 2 && root.at("schemaVersion").is_number_unsigned() && root.at("schemaVersion") == 1 &&
                   root.at("files").is_array() && root.at("files").size() <= limits.entries;
        }

        /** @brief Converts all entries atomically; no partial inventory escapes on error. */
        [[nodiscard]] Result<std::vector<PackageFileEntry>> DecodeFiles(const Json &root, const PackageValidationLimits &limits) {
            std::vector<PackageFileEntry> entries;
            Detail::PathInventory inventory;
            std::uint64_t total = 0;
            for (const auto &value : root.at("files")) {
                auto entry = DecodeEntry(value);
                if (entry.HasError()) {
                    return Result<std::vector<PackageFileEntry>>::Failure(entry.ErrorValue());
                }
                const auto &file = entry.Value();
                if (file.size > limits.fileBytes || file.size > limits.expandedBytes - total) {
                    return Result<std::vector<PackageFileEntry>>::Failure(MakeError(Detail::ResourceLimit));
                }
                if (file.id.path.CollisionKey() == "files.manifest.json" || !inventory.Add(file.id.path)) {
                    return Result<std::vector<PackageFileEntry>>::Failure(MakeError(Detail::InvalidManifest));
                }
                total += file.size;
                entries.push_back(std::move(entry).Value());
            }
            return Result<std::vector<PackageFileEntry>>::Success(std::move(entries));
        }
    }  // namespace

    /** @copydoc ValidatedPackageFileManifestV1::Parse */
    Result<ValidatedPackageFileManifestV1> ValidatedPackageFileManifestV1::Parse(const std::string_view json,
                                                                                 const PackageValidationLimits &limits) {
        if (json.size() > limits.manifestBytes) {
            return Result<ValidatedPackageFileManifestV1>::Failure(MakeError(Detail::ResourceLimit));
        }
        try {
            DecodeGuard guard;
            const auto root = Json::parse(json, std::ref(guard));
            if (!guard.valid || !HasSchema(root, limits)) {
                return Result<ValidatedPackageFileManifestV1>::Failure(MakeError(Detail::InvalidManifest));
            }
            auto entries = DecodeFiles(root, limits);
            if (entries.HasError()) {
                return Result<ValidatedPackageFileManifestV1>::Failure(entries.ErrorValue());
            }
            return Result<ValidatedPackageFileManifestV1>::Success(
                ValidatedPackageFileManifestV1{std::move(entries).Value(), ComputeSha256(std::as_bytes(std::span{json}))});
        } catch (const Json::exception &) {
            return Result<ValidatedPackageFileManifestV1>::Failure(MakeError(Detail::InvalidManifest));
        }
    }

    /** @copydoc ValidatedPackageFileManifestV1::ValidatedPackageFileManifestV1 */
    ValidatedPackageFileManifestV1::ValidatedPackageFileManifestV1(std::vector<PackageFileEntry> entries, Sha256Digest digest)
        : m_entries(std::move(entries)), m_digest(digest) {}

    /** @copydoc ValidatedPackageFileManifestV1::Entries */
    std::span<const PackageFileEntry> ValidatedPackageFileManifestV1::Entries() const noexcept {
        return m_entries;
    }

    /** @copydoc ValidatedPackageFileManifestV1::Digest */
    const Sha256Digest &ValidatedPackageFileManifestV1::Digest() const noexcept {
        return m_digest;
    }
}  // namespace Horo::Packages
