#include "PackageValidationDetail.h"

namespace Horo::Packages::Detail {
    const ErrorCodeDescriptor InvalidManifest{
        .domain = ErrorDomainId{"packages"},
        .code = ErrorCode{"packages.manifest.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Package file manifest is invalid.",
        .remediationHint = "Provide a complete schema-v1 file inventory with canonical unique paths and SHA-256 digests.",
    };
    const ErrorCodeDescriptor InvalidArchive{
        .domain = ErrorDomainId{"packages"},
        .code = ErrorCode{"packages.archive.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Package archive contains invalid or unsupported entries.",
        .remediationHint = "Rebuild the package as a non-encrypted ZIP of regular files and directories without links.",
    };
    const ErrorCodeDescriptor ResourceLimit{
        .domain = ErrorDomainId{"packages"},
        .code = ErrorCode{"packages.validation.limit"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Package exceeds the validation resource policy.",
        .remediationHint = "Reduce archive size, expanded sizes, entry count or manifest complexity.",
    };
    const ErrorCodeDescriptor InventoryMismatch{
        .domain = ErrorDomainId{"packages"},
        .code = ErrorCode{"packages.archive.inventory_mismatch"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Package archive does not match its complete file inventory.",
        .remediationHint = "Regenerate the inventory from the exact package files, hashes, sizes and executable modes.",
    };

    /** @copydoc PathInventory::Add */
    bool PathInventory::Add(const PackagePath &path, const bool directory) {
        const auto &value = path.Value();
        std::size_t offset = 0;
        do {
            const auto separator = value.find('/', offset);
            const bool final = separator == std::string::npos;
            const auto prefix = PackagePath::Parse(value.substr(0, separator));
            if (prefix.HasError()) {
                return false;
            }
            const bool isDirectory = !final || directory;
            const auto [node, inserted] =
                m_nodes.try_emplace(prefix.Value().CollisionKey(), Node{prefix.Value().Value(), isDirectory, final});
            if (!inserted) {
                if (node->second.spelling != prefix.Value().Value() || node->second.directory != isDirectory ||
                    (final && node->second.explicitEntry)) {
                    return false;
                }
                node->second.explicitEntry |= final;
            }
            if (final) {
                return true;
            }
            offset = separator + 1;
        } while (offset < value.size());
        return false;
    }
}  // namespace Horo::Packages::Detail
