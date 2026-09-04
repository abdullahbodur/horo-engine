#pragma once

#include "Horo/Packages/PackageFileManifest.h"

#include <map>
#include <string>

namespace Horo::Packages::Detail {
    /** @brief Stable package-validation failure identities shared by the private decoder and archive adapter. */
    extern const ErrorCodeDescriptor InvalidManifest;
    extern const ErrorCodeDescriptor InvalidArchive;
    extern const ErrorCodeDescriptor ResourceLimit;
    extern const ErrorCodeDescriptor InventoryMismatch;

    /** @brief Validates uniqueness, prefix spelling and file/directory conflicts incrementally without I/O. */
    class PathInventory {
    public:
        /**
         * @brief Admits an entry after checking every directory prefix and its exact canonical spelling.
         * @param path Canonical package entry identity.
         * @param directory Whether this is an explicit directory instead of a regular file.
         * @return False for duplicate entries, aliases or file/directory conflicts.
         */
        [[nodiscard]] bool Add(const PackagePath &path, bool directory = false);

    private:
        struct Node {
            std::string spelling;
            bool directory = true;
            bool explicitEntry = false;
        };

        std::map<std::string, Node, std::less<>> m_nodes;
    };
}  // namespace Horo::Packages::Detail
