#pragma once

/**
 * @file ReplicationDescriptorRegistry.h
 * @brief Transactional immutable replication descriptor snapshots.
 */

#include "Horo/Foundation/Sha256.h"
#include "Horo/Network/ReplicationDescriptor.h"

#include <memory>
#include <span>
#include <vector>

namespace Horo::Network {
    /**
     * @brief Immutable identity-sorted schema set with canonical compatibility fingerprint.
     *
     * Callers pin this object with `ReplicationDescriptorSnapshotPtr`. Spans and descriptor
     * pointers returned by the snapshot remain valid only while that owning pin remains alive.
     */
    class ReplicationDescriptorSnapshot final {
    private:
        /** @brief Restricts construction to the transactional builder while permitting make_shared. */
        struct ConstructionKey final {};

    public:
        /** @brief Returns identity-sorted registry-owned schemas. */
        [[nodiscard]] std::span<const ReplicationSchemaDescriptor> Schemas() const noexcept;

        /**
         * @brief Finds an exact stable schema identity without fallback.
         * @param id Stable schema identity.
         * @return Snapshot-owned descriptor or NetworkErrors::ReplicationSchemaUnknown.
         */
        [[nodiscard]] Result<const ReplicationSchemaDescriptor *> Find(ReplicationSchemaId id) const;

        /** @brief Returns the canonical semantic schema-set digest used by compatibility negotiation. */
        [[nodiscard]] const Sha256Digest &Fingerprint() const noexcept;

        /** @brief Constructs canonical storage when presented with the builder-only key. */
        ReplicationDescriptorSnapshot(ConstructionKey, std::vector<ReplicationSchemaDescriptor> schemas, const Sha256Digest &fingerprint);

    private:
        friend Result<std::shared_ptr<const ReplicationDescriptorSnapshot>> BuildReplicationDescriptorSnapshot(
            std::span<const ReplicationSchemaDescriptor>, const ReplicationDescriptorLimits &);
        friend Result<std::shared_ptr<const ReplicationDescriptorSnapshot>> BuildReplicationDescriptorReplacement(
            const std::shared_ptr<const ReplicationDescriptorSnapshot> &, std::span<const ReplicationSchemaDescriptor>,
            const ReplicationDescriptorLimits &);

        std::vector<ReplicationSchemaDescriptor> schemas_;
        Sha256Digest fingerprint_;
    };

    /** @brief Shared immutable snapshot pin used by sessions and later runtime owners. */
    using ReplicationDescriptorSnapshotPtr = std::shared_ptr<const ReplicationDescriptorSnapshot>;

    /**
     * @brief Builds an initial complete descriptor snapshot transactionally.
     * @param descriptors Complete inert schema contributions in arbitrary input order.
     * @param limits Explicit finite construction bounds.
     * @return Owned immutable snapshot or a typed error; failure publishes no partial state.
     */
    [[nodiscard]] Result<ReplicationDescriptorSnapshotPtr> BuildReplicationDescriptorSnapshot(
        std::span<const ReplicationSchemaDescriptor> descriptors, const ReplicationDescriptorLimits &limits);

    /**
     * @brief Builds and validates a complete replacement without mutating the pinned prior snapshot.
     * @param previous Prior immutable generation that remains valid on success and failure.
     * @param descriptors Complete replacement schema contributions.
     * @param limits Explicit finite construction bounds.
     * @return New immutable snapshot, or a typed compatibility error with `previous` unchanged.
     */
    [[nodiscard]] Result<ReplicationDescriptorSnapshotPtr> BuildReplicationDescriptorReplacement(
        const ReplicationDescriptorSnapshotPtr &previous, std::span<const ReplicationSchemaDescriptor> descriptors,
        const ReplicationDescriptorLimits &limits);
}  // namespace Horo::Network
