#pragma once

/**
 * @file SaveRootResolver.h
 * @brief Platform-backed product save-root resolution and containment.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Save/SaveIdentity.h"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace Horo {
    class ProcessService;
}

namespace Horo::Runtime {
    /** @brief Operating-system convention used to locate a product's approved user-state root. */
    enum class SaveRootPlatform : std::uint8_t {
        Windows,
        MacOS,
        Linux,
        Test
    };

    /** @brief Inert inputs for resolving one product-owned save root. */
    struct SaveRootResolutionRequest {
        ProductStorageId product;                           /**< Validated product configuration identity. */
        SaveRootPlatform platform{SaveRootPlatform::Test};  /**< Platform convention selected by host composition. */
        std::optional<std::filesystem::path> testStateRoot; /**< Explicit sandbox root used only by Test. */
    };

    /** @brief Owned canonical product save-root capability intended only for the save storage adapter. */
    class ProductSaveRoot final {
    public:
        /** @brief Constructs an invalid root for optional/default storage. */
        ProductSaveRoot() = default;

        /** @brief Returns the validated product identity. @return Product identity bound to this root. */
        [[nodiscard]] const ProductStorageId &Product() const noexcept;
        /** @brief Returns the selected platform convention. @return Platform bound to this root. */
        [[nodiscard]] SaveRootPlatform Platform() const noexcept;
        /** @brief Returns the canonical absolute path. @return Borrowed path owned by this value. */
        [[nodiscard]] const std::filesystem::path &CanonicalPath() const noexcept;
        /** @brief Reports whether this value owns a usable absolute root. @return True for a resolved root. */
        [[nodiscard]] bool IsValid() const noexcept;

    private:
        friend Result<ProductSaveRoot> ResolveProductSaveRoot(const SaveRootResolutionRequest &, const ProcessService &);
        /** @brief Constructs a validated owned capability after containment checks succeed. */
        ProductSaveRoot(ProductStorageId product, SaveRootPlatform platform, std::filesystem::path canonicalPath);

        ProductStorageId product_;
        SaveRootPlatform platform_{SaveRootPlatform::Test};
        std::filesystem::path canonicalPath_;
    };

    /**
     * @brief Resolves and creates a product save root through platform-owned environment access.
     *
     * The call retains no reference to the process service or request. Fixed Horo directory names and
     * the canonical ProductStorageId are the only appended path components. Existing symlink/reparse
     * entries or unexpected file types below the approved state directory fail closed.
     * Checks are performed before and after each directory creation, but this path-based API does not
     * claim to eliminate every filesystem time-of-check/time-of-use race. Storage operations must still
     * use their platform-specific no-follow and atomic-publication protections.
     *
     * @param request Validated product identity, platform convention, and optional test sandbox.
     * @param processes Borrowed platform process service used only for environment lookup during this call.
     * @return Owned canonical product root, or a stable save-root configuration, availability,
     * containment, or unsupported-platform error. Failure messages never contain a native path.
     */
    [[nodiscard]] Result<ProductSaveRoot> ResolveProductSaveRoot(const SaveRootResolutionRequest &request, const ProcessService &processes);
}  // namespace Horo::Runtime
