#pragma once

/**
 * @file PacketBuffer.h
 * @brief Prepared bounded packet storage with generation-safe owned leases.
 */

#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <memory>
#include <span>

namespace Horo::Network {
    struct PacketBufferPoolState;

    /** @brief Setup-time bounds for fixed packet slots. */
    struct PacketBufferPoolDescriptor final {
        std::size_t maximumBuffers{};        /**< Positive slot count. */
        std::size_t maximumBytesPerBuffer{}; /**< Positive inclusive payload bound. */
        std::size_t inlineBytes{256};        /**< Bytes classified as the allocation-free small-message path; at most 256. */
    };

    /** @brief Move-only packet bytes whose lease keeps prepared pool state alive. */
    class PacketBuffer final {
    public:
        PacketBuffer() = default;
        ~PacketBuffer();
        PacketBuffer(PacketBuffer &&other) noexcept;
        PacketBuffer &operator=(PacketBuffer &&other) noexcept;
        PacketBuffer(const PacketBuffer &) = delete;
        PacketBuffer &operator=(const PacketBuffer &) = delete;

        /** @brief Returns borrowed bytes valid until this lease is reset or destroyed. */
        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
        /** @brief Reports whether this payload fits the pool's declared inline budget. */
        [[nodiscard]] bool UsesInlineStorage() const noexcept;
        /** @brief Reports whether this lease owns a live pool slot. */
        [[nodiscard]] bool IsValid() const noexcept;
        /** @brief Releases the exact generation once; repeated calls are harmless. */
        void Reset() noexcept;

    private:
        friend class PacketBufferPool;
        PacketBuffer(std::shared_ptr<PacketBufferPoolState> state, std::size_t slot, std::size_t generation) noexcept;
        std::shared_ptr<PacketBufferPoolState> state_;
        std::size_t slot_{};
        std::size_t generation_{};
    };

    /** @brief Exclusively admitted fixed pool; leases safely retain state after the pool owner moves or dies. */
    class PacketBufferPool final {
    public:
        /** @brief Prepares all slot storage. @return Pool or typed invalid/capacity/allocation failure. */
        [[nodiscard]] static Result<PacketBufferPool> Create(const PacketBufferPoolDescriptor &descriptor);
        /** @brief Copies bytes into a free prepared slot without allocation. */
        [[nodiscard]] Result<PacketBuffer> Acquire(std::span<const std::byte> bytes);
        /** @brief Number of currently leased slots. */
        [[nodiscard]] std::size_t Outstanding() const noexcept;

        PacketBufferPool(PacketBufferPool &&) noexcept = default;
        PacketBufferPool &operator=(PacketBufferPool &&) noexcept = default;
        PacketBufferPool(const PacketBufferPool &) = delete;
        PacketBufferPool &operator=(const PacketBufferPool &) = delete;

    private:
        explicit PacketBufferPool(std::shared_ptr<PacketBufferPoolState> state) noexcept : state_(std::move(state)) {}

        std::shared_ptr<PacketBufferPoolState> state_;
    };
}  // namespace Horo::Network
