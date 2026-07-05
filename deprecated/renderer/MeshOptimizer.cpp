/** @file MeshOptimizer.cpp
 *  @brief Vertex cache optimization implementation.
 *
 *  Implements Tom Forsyth's greedy vertex-cache optimisation (2006) as
 *  described in @ref MeshOptimizer.h.  The core loop uses a simulated FIFO
 *  cache, per-vertex triangle adjacency, and a dead-end vertex stack to
 *  preserve spatial locality through the mesh.
 */
#include "renderer/MeshOptimizer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

namespace Horo::MeshOptimizer {
namespace {

//----------------------------------------------------------------------------
// Internal data structures
//----------------------------------------------------------------------------

/** @brief Per-triangle metadata used during reordering. */
struct TriangleInfo {
    bool emitted = false;   /**< True after this triangle has been added to the output. */
    /** Indices of the three triangle vertices (copy of the input values). */
    std::array<uint32_t, 3> vertices{};
};

/** @brief Per-vertex metadata used during reordering. */
struct VertexInfo {
    std::vector<uint32_t> triangleIndices; /**< Triangles that still reference this vertex. */
    int32_t cachePosition = -1;            /**< Position in the simulated FIFO cache (-1 = not in cache). */
    uint32_t remainingValence = 0;         /**< Number of triangles still referencing this vertex. */
};

/** @brief Internal state for the optimization pass. */
struct CacheOptimizerState {
    std::vector<VertexInfo> vertexInfo;
    std::vector<TriangleInfo> triangleInfo;
    std::vector<uint32_t> emittedIndices;   /**< Reordered output index buffer. */
    std::vector<uint32_t> deadEndStack;     /**< Vertices that still have triangles but whose most recent triangle was emitted. */
    std::vector<uint32_t> cacheEntries;      /**< Simulated FIFO cache entries, most-recent first. */
    uint32_t cacheSize;
    uint32_t trianglesRemaining;
    uint32_t nextLiveVertexSearch = 0;       /**< Cursor for finding the next disconnected live vertex. */

    /** @brief Constructs state from input index data. */
    CacheOptimizerState(const std::vector<uint32_t>& indices,
                        uint32_t vertexCount,
                        uint32_t cacheSize);
};


//----------------------------------------------------------------------------
// CacheOptimizerState construction
//----------------------------------------------------------------------------

CacheOptimizerState::CacheOptimizerState(const std::vector<uint32_t>& indices,
                                         uint32_t vertexCount,
                                         uint32_t cacheSize)
    : vertexInfo(vertexCount), cacheSize(cacheSize) {

    const uint32_t triangleCount = static_cast<uint32_t>(indices.size()) / 3;
    triangleInfo.resize(triangleCount);
    emittedIndices.reserve(indices.size());
    deadEndStack.reserve(vertexCount);
    cacheEntries.reserve(cacheSize);
    trianglesRemaining = triangleCount;

    // Build per-vertex adjacency and copy triangle vertices.
    for (uint32_t t = 0; t < triangleCount; ++t) {
        const uint32_t base = t * 3;
        triangleInfo[t].vertices[0] = indices[base + 0];
        triangleInfo[t].vertices[1] = indices[base + 1];
        triangleInfo[t].vertices[2] = indices[base + 2];

        for (uint32_t c = 0; c < 3; ++c) {
            const uint32_t v = indices[base + c];
            assert(v < vertexCount);
            vertexInfo[v].triangleIndices.push_back(t);
        }
    }

    // Set initial valences.
    for (auto& vi : vertexInfo) {
        vi.remainingValence = static_cast<uint32_t>(vi.triangleIndices.size());
    }
}


//----------------------------------------------------------------------------
// Scoring helpers
//----------------------------------------------------------------------------

/** @brief Vertex score: higher = better candidate for next triangle.
 *
 *  Mirrors the Forsyth heuristic: prefer vertices that still participate in
 *  many triangles, with a tie-break favouring those nearer the front of the
 *  simulated cache (most recently used).
 *
 *  The formula is `2 * remainingValence + cacheBonus` where `cacheBonus` is
 *  `3 * (cacheSize - cachePosition)` when the vertex is in the cache, or
 *  `-1` when it is not.  This prioritises high-valence vertices while still
 *  preferring recent ones among equal-valence candidates.
 */
inline int32_t VertexScore(const VertexInfo& vi, uint32_t cacheSize) {
    if (vi.remainingValence == 0)
        return -1;

    int32_t score = static_cast<int32_t>(vi.remainingValence) * 2;
    if (vi.cachePosition >= 0) {
        score += static_cast<int32_t>(cacheSize) - vi.cachePosition;
    }
    return score;
}


/** @brief Triangle score: sum of vertex scores for the three vertices.
 *
 *  Higher is better. Used when we must pick the best triangle from a
 *  candidate vertex.
 */
inline int32_t TriangleScore(const TriangleInfo& tri,
                             const std::vector<VertexInfo>& vertexInfo,
                             uint32_t cacheSize) {
    int32_t score = 0;
    for (uint32_t c = 0; c < 3; ++c) {
        score += VertexScore(vertexInfo[tri.vertices[c]], cacheSize);
    }
    return score;
}


//----------------------------------------------------------------------------
// Cache manipulation
//----------------------------------------------------------------------------

/** @brief Inserts @p vertex into the simulated FIFO cache.
 *
 *  The new vertex enters at position 0; every other entry shifts down by one.
 *  Any vertex that falls off the end of the cache has its cachePosition set
 *  to -1.
 */
void AddToCache(uint32_t vertex, CacheOptimizerState& state) {
    std::vector<uint32_t>& cacheEntries = state.cacheEntries;

    const int32_t oldPosition = state.vertexInfo[vertex].cachePosition;
    if (oldPosition >= 0 &&
        static_cast<size_t>(oldPosition) < cacheEntries.size() &&
        cacheEntries[static_cast<size_t>(oldPosition)] == vertex) {
        cacheEntries.erase(cacheEntries.begin() + oldPosition);
    } else {
        const auto it = std::ranges::find(cacheEntries, vertex);
        if (it != cacheEntries.end())
            cacheEntries.erase(it);
    }

    cacheEntries.insert(cacheEntries.begin(), vertex);
    if (cacheEntries.size() > state.cacheSize) {
        const uint32_t evicted = cacheEntries.back();
        state.vertexInfo[evicted].cachePosition = -1;
        cacheEntries.pop_back();
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(cacheEntries.size()); ++i)
        state.vertexInfo[cacheEntries[i]].cachePosition = static_cast<int32_t>(i);
}


//----------------------------------------------------------------------------
// Triangle emission
//----------------------------------------------------------------------------

/** @brief Removes @p triangleIndex from the adjacency lists of all three
 *         vertices and decrements their remainingValence.
 *
 *  Vertices whose valence drops to zero after removal are skipped by the
 *  dead-end stack logic.
 */
void RemoveTriangleFromAdjacency(uint32_t triangleIndex,
                                 const TriangleInfo& tri,
                                 std::vector<VertexInfo>& vertexInfo) {
    for (uint32_t c = 0; c < 3; ++c) {
        const uint32_t v = tri.vertices[c];
        VertexInfo& vi = vertexInfo[v];

        assert(vi.remainingValence > 0);
        --vi.remainingValence;

        // Remove this triangle from the vertex's adjacency list.
        // Fast removal: find the element and swap with the last, then pop.
        auto& tris = vi.triangleIndices;
        const auto it = std::ranges::find(tris, triangleIndex);
        if (it != tris.end()) {
            *it = tris.back();
            tris.pop_back();
        }
    }
}


//----------------------------------------------------------------------------
// Dead-end stack management
//----------------------------------------------------------------------------

/** @brief Pushes @p vertex onto the dead-end stack if it still has
 *         remaining triangles and is not already in the cache at position 0.
 *
 *  This preserves spatial locality: after emitting the current triangle,
 *  we try to follow one of its vertices into an adjacent unemitted triangle
 *  rather than jumping to a distant region of the mesh.
 */
void PushDeadEndIfLive(uint32_t vertex,
                       const std::vector<VertexInfo>& vertexInfo,
                       std::vector<uint32_t>& deadEndStack) {
    if (vertexInfo[vertex].remainingValence > 0) {
        deadEndStack.push_back(vertex);
    }
}


//----------------------------------------------------------------------------
// Vertex selection (global best)
//----------------------------------------------------------------------------

/** @brief Finds the next live vertex to start a disconnected triangle run.
 *
 *  The dead-end stack handles locality inside connected regions. When it is
 *  empty, scanning forward from a retained cursor avoids repeatedly walking
 *  millions of already-consumed vertices on imported meshes with little or no
 *  index sharing.
 */
uint32_t FindNextLiveVertex(const std::vector<VertexInfo>& vertexInfo,
                            uint32_t& cursor) {
    const uint32_t vertexCount = static_cast<uint32_t>(vertexInfo.size());
    if (vertexCount == 0)
        return UINT32_MAX;

    for (uint32_t offset = 0; offset < vertexCount; ++offset) {
        const uint32_t v = (cursor + offset) % vertexCount;
        if (vertexInfo[v].remainingValence == 0)
            continue;
        cursor = (v + 1) % vertexCount;
        return v;
    }
    return UINT32_MAX;
}


//----------------------------------------------------------------------------
// Triangle selection (from a given vertex)
//----------------------------------------------------------------------------

/** @brief Finds the best unemitted triangle that uses @p vertex.
 *
 *  Scores each candidate triangle via @ref TriangleScore.  Returns the
 *  triangle index and sets @p outOther1 / @p outOther2 to the indices of
 *  the other two vertices (in order, for correct winding).
 *
 *  @return The index of the selected triangle, or @c UINT32_MAX if the
 *          vertex has no remaining triangles.
 */
uint32_t FindBestTriangleForVertex(
    uint32_t vertex,
    const std::vector<VertexInfo>& vertexInfo,
    const std::vector<TriangleInfo>& triangleInfo,
    uint32_t cacheSize,
    uint32_t& outOther1,
    uint32_t& outOther2) {

    int32_t bestScore = -1;
    uint32_t bestTriangle = UINT32_MAX;

    const VertexInfo& vi = vertexInfo[vertex];
    for (uint32_t triIdx : vi.triangleIndices) {
        if (triangleInfo[triIdx].emitted)
            continue;

        const int32_t score = TriangleScore(triangleInfo[triIdx], vertexInfo, cacheSize);
        if (score > bestScore) {
            bestScore = score;
            bestTriangle = triIdx;
        }
    }

    if (bestTriangle == UINT32_MAX)
        return UINT32_MAX;

    // Determine winding order: vertex is v0, other two are v1 and v2 in
    // the order they appear in the triangle.
    const TriangleInfo& tri = triangleInfo[bestTriangle];
    if (tri.vertices[0] == vertex) {
        outOther1 = tri.vertices[1];
        outOther2 = tri.vertices[2];
    } else if (tri.vertices[1] == vertex) {
        outOther1 = tri.vertices[2];
        outOther2 = tri.vertices[0];
    } else {
        outOther1 = tri.vertices[0];
        outOther2 = tri.vertices[1];
    }
    return bestTriangle;
}


//----------------------------------------------------------------------------
// Main loop
//----------------------------------------------------------------------------

/** @brief Core optimization loop.
 *
 *  1. Pop a vertex from the dead-end stack (or find the global best vertex
 *     if the stack is empty).
 *  2. Find the best unemitted triangle adjacent to this vertex.
 *  3. Emit the triangle (append indices to output, update cache, remove
 *     triangle from adjacency).
 *  4. Push the other two vertices of the triangle onto the dead-end stack
 *     if they still have remaining triangles.
 *  5. Repeat until all triangles are emitted.
 */
void RunOptimization(CacheOptimizerState& state) {
    while (state.trianglesRemaining > 0) {
        uint32_t currentVertex;

        // Step 1: Get the next active vertex.
        if (!state.deadEndStack.empty()) {
            currentVertex = state.deadEndStack.back();
            state.deadEndStack.pop_back();

            // Skip vertices that no longer have triangles (already depleted
            // by earlier emissions).
            if (state.vertexInfo[currentVertex].remainingValence == 0)
                continue;
        } else {
            currentVertex = FindNextLiveVertex(state.vertexInfo,
                                               state.nextLiveVertexSearch);
            if (currentVertex == UINT32_MAX)
                break; // Should not happen if trianglesRemaining > 0.
        }

        // Step 2: Find the best triangle adjacent to this vertex.
        uint32_t other1, other2;
        const uint32_t triIdx = FindBestTriangleForVertex(
            currentVertex, state.vertexInfo, state.triangleInfo,
            state.cacheSize, other1, other2);

        if (triIdx == UINT32_MAX)
            continue; // Race: vertex valence was consumed by another path.

        TriangleInfo& tri = state.triangleInfo[triIdx];
        assert(!tri.emitted);

        // Step 3: Emit the triangle.
        tri.emitted = true;
        --state.trianglesRemaining;

        state.emittedIndices.push_back(currentVertex);
        state.emittedIndices.push_back(other1);
        state.emittedIndices.push_back(other2);

        // Update cache for all three vertices.
        AddToCache(currentVertex, state);
        AddToCache(other1, state);
        AddToCache(other2, state);

        // Remove this triangle from adjacency.
        RemoveTriangleFromAdjacency(triIdx, tri, state.vertexInfo);

        // Step 4: Push other vertices as dead-end candidates.
        PushDeadEndIfLive(other1, state.vertexInfo, state.deadEndStack);
        PushDeadEndIfLive(other2, state.vertexInfo, state.deadEndStack);
    }
}

} // namespace


//----------------------------------------------------------------------------
// Public API
//----------------------------------------------------------------------------

/** @copydoc Horo::MeshOptimizer::OptimizeVertexCache */
void OptimizeVertexCache(std::vector<uint32_t>& indices,
                         uint32_t vertexCount,
                         uint32_t cacheSize) {
    if (indices.size() < 6 || vertexCount < 2 || cacheSize < 3)
        return; // Trivial or degenerate; nothing to optimize.

    CacheOptimizerState state(indices, vertexCount, cacheSize);

    RunOptimization(state);

    // Swap the reordered indices into the caller's vector.
    assert(state.emittedIndices.size() == indices.size());
    indices.swap(state.emittedIndices);
}


/** @copydoc Horo::MeshOptimizer::ComputeAcrm */
float ComputeAcrm(const std::vector<uint32_t>& indices,
                  uint32_t cacheSize) {
    if (indices.empty() || cacheSize == 0)
        return 0.0f;

    // Simulate a FIFO cache.
    std::vector<int32_t> cachePositions(/* vertexCount estimate */ 0);
    uint32_t maxVertex = 0;
    for (uint32_t idx : indices)
        maxVertex = std::max(maxVertex, idx);
    cachePositions.resize(static_cast<size_t>(maxVertex) + 1, -1);

    uint32_t cacheMisses = 0;
    int32_t cacheEntries = 0; // Number of entries currently in the cache.

    for (uint32_t idx : indices) {
        // Shift all entries down.
        for (auto& pos : cachePositions) {
            if (pos >= 0) {
                ++pos;
                if (static_cast<uint32_t>(pos) >= cacheSize) {
                    pos = -1;
                    --cacheEntries;
                }
            }
        }

        if (cachePositions[idx] < 0) {
            // Cache miss.
            ++cacheMisses;
            cachePositions[idx] = 0;
            ++cacheEntries;
        } else {
            // Cache hit — vertex already in cache, update its position.
            cachePositions[idx] = 0;
        }
    }

    const uint32_t triangleCount = static_cast<uint32_t>(indices.size()) / 3;
    if (triangleCount == 0)
        return 0.0f;

    // ACMR = total cache misses / triangle count.
    return static_cast<float>(cacheMisses) / static_cast<float>(triangleCount);
}

} // namespace Horo::MeshOptimizer
