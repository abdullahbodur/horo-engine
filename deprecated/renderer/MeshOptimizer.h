/** @file MeshOptimizer.h
 *  @brief Vertex cache optimization utilities for GPU-bound mesh data.
 *
 *  Provides algorithms that reorder triangle indices to maximise the hit rate of
 *  the GPU post-transform vertex cache, reducing vertex shader invocations
 *  without altering geometry.  The primary interface is
 *  @ref OptimizeVertexCache, which implements Tom Forsyth's linear-speed
 *  greedy algorithm with a configurable simulated FIFO cache size.
 *
 *  These optimizations are applied during asset import (FBX path) so the
 *  engine-native `.mesh.bin` files are already cache-friendly at runtime;
 *  no per-frame cost is incurred.
 *
 *  Reference: Tom Forsyth, "Linear-Speed Vertex Cache Optimisation" (2006).
 */
#pragma once

#include <cstdint>
#include <vector>

namespace Horo::MeshOptimizer {

/** @brief Default simulated post-transform vertex cache size in entries.
 *
 *  GPU post-transform caches vary between vendors and generations; a
 *  conservative value of 32 entries has proven to produce excellent results
 *  across all major IHVs without over-fitting to a specific architecture.
 */
inline constexpr uint32_t kDefaultCacheSize = 32;

/** @brief Reorders @p indices for optimal post-transform vertex cache hits.
 *
 *  The vertex buffer is not modified — only the index drawing order changes.
 *  For meshes without vertex sharing (every vertex referenced exactly once),
 *  this is a near-no-op and returns quickly.
 *
 *  @param indices       Index buffer to reorder in-place.  Must contain a
 *                       multiple of 3 entries (triangle list).  Modifiable.
 *  @param vertexCount   Number of distinct vertices referenced by @p indices.
 *                       Indices outside [0, vertexCount) are undefined behavior.
 *  @param cacheSize     Size of the simulated FIFO post-transform cache.
 *                       Values above 64 provide diminishing returns; the
 *                       default (kDefaultCacheSize = 32) is recommended.
 *
 *  @par Algorithm
 *  - Builds per-vertex triangle adjacency lists (O(N) memory/cpu).
 *  - Greedily emits triangles by scoring candidate vertices against a simulated
 *    FIFO cache plus remaining valence.
 *  - Uses a dead-end vertex stack to maintain spatial locality through the
 *    mesh without any spatial acceleration structure.
 *
 *  @par Complexity
 *  O(T + V) time and memory where T = triangle count and V = vertex count.
 *  In typical game meshes (tens to hundreds of thousands of triangles) this
 *  completes in sub-millisecond time on a modern desktop CPU.
 */
void OptimizeVertexCache(std::vector<uint32_t>& indices,
                         uint32_t vertexCount,
                         uint32_t cacheSize = kDefaultCacheSize);

/** @brief Computes the approximate ACMR (Average Cache Miss Ratio) for the
 *         given index buffer using a simulated FIFO cache of @p cacheSize.
 *
 *  Useful for asserting that an optimization pass actually improved the
 *  cache hit rate.  An ACMR of 1.0 means every triangle references three
 *  uncached vertices (worst case).  An ACMR near 0.5 is typical for a
 *  well-optimized mesh on a 32-entry cache.
 *
 *  @param indices     Index buffer to measure (triangle list).
 *  @param cacheSize   Simulated FIFO cache size in entries.
 *  @return Approximate ACMR (number of vertex transforms per triangle on
 *          average). For a single-triangle mesh this is 3.0 / 3 = 1.0.
 */
float ComputeAcrm(const std::vector<uint32_t>& indices,
                  uint32_t cacheSize = kDefaultCacheSize);

} // namespace Horo::MeshOptimizer
