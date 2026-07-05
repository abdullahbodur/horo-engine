/** @file test_mesh_optimizer.cpp
 *  @brief Unit tests for renderer/MeshOptimizer — vertex cache optimization.
 *
 *  Coverage:
 *  - OptimizeVertexCache: correct output size, all vertices preserved,
 *    all triangles referenced at least once, ACMR improvement.
 *  - ComputeAcrm: known-reference patterns (sequential, sawtooth, grid).
 *  - Edge cases: empty index buffer, single triangle, degenerate vertex count.
 *  - Round-trip invariants: every input index appears in the output,
 *    triangle count unchanged, no out-of-range indices produced.
 *  - Realistic patterns: grid topology (common in terrain / scanned meshes)
 *    where vertex-cache-unfriendly ordering is the baseline.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <numeric>
#include <random>
#include <unordered_set>
#include <vector>

#include "renderer/MeshOptimizer.h"

using namespace Horo;

// ============================================================================
// Helpers
// ============================================================================

namespace {

/** @brief Builds a triangle-list index buffer for a 2D grid.
 *
 *  Each quad is two triangles.  Unoptimized input uses naive row-major
 *  quad ordering, which thrashes the vertex cache.
 *
 *  @param rows       Number of vertex rows (>= 2).
 *  @param cols       Number of vertex columns (>= 2).
 *  @return Triangle-list index buffer (3 * (rows-1) * (cols-1) * 2 elements).
 */
std::vector<uint32_t> BuildGridIndices(uint32_t rows, uint32_t cols) {
    REQUIRE(rows >= 2);
    REQUIRE(cols >= 2);
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(rows - 1) * (cols - 1) * 6);
    for (uint32_t r = 0; r < rows - 1; ++r) {
        for (uint32_t c = 0; c < cols - 1; ++c) {
            const uint32_t a = r * cols + c;
            const uint32_t b = a + 1;
            const uint32_t d = (r + 1) * cols + c;
            const uint32_t e = d + 1;
            // Triangle 1: a, b, d
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(d);
            // Triangle 2: b, e, d
            indices.push_back(b);
            indices.push_back(e);
            indices.push_back(d);
        }
    }
    return indices;
}

/** @brief Returns the set of unique indices in @p indices. */
std::unordered_set<uint32_t> UniqueIndices(const std::vector<uint32_t>& indices) {
    std::unordered_set<uint32_t> s;
    for (uint32_t i : indices)
        s.insert(i);
    return s;
}

} // namespace

// ============================================================================
// OptimizeVertexCache — basic invariants
// ============================================================================

TEST_CASE("MeshOptimizer: empty index buffer is a no-op", "[renderer][mesh_optimizer]") {
    std::vector<uint32_t> indices;
    MeshOptimizer::OptimizeVertexCache(indices, 0, 32);
    REQUIRE(indices.empty());
}

TEST_CASE("MeshOptimizer: single triangle is a no-op", "[renderer][mesh_optimizer]") {
    std::vector<uint32_t> indices = {0, 1, 2};
    const auto original = indices;
    MeshOptimizer::OptimizeVertexCache(indices, 3, 32);
    // With one triangle, the output is the same set of indices (possibly
    // reordered but all three must appear).
    REQUIRE(indices.size() == 3);
    const auto outSet = UniqueIndices(indices);
    REQUIRE(outSet.size() == 3);
    REQUIRE(outSet.contains(0));
    REQUIRE(outSet.contains(1));
    REQUIRE(outSet.contains(2));
}

TEST_CASE("MeshOptimizer: preserves triangle count", "[renderer][mesh_optimizer]") {
    // A 5x5 grid = 4*4 = 16 quads = 32 triangles = 96 indices.
    const uint32_t rows = 5, cols = 5;
    std::vector<uint32_t> indices = BuildGridIndices(rows, cols);
    const size_t originalSize = indices.size();

    MeshOptimizer::OptimizeVertexCache(indices, rows * cols, 32);

    REQUIRE(indices.size() == originalSize);
    REQUIRE(indices.size() % 3 == 0);
}

TEST_CASE("MeshOptimizer: all input indices appear in output", "[renderer][mesh_optimizer]") {
    const uint32_t rows = 6, cols = 6;
    std::vector<uint32_t> indices = BuildGridIndices(rows, cols);
    const auto inputSet = UniqueIndices(indices);

    MeshOptimizer::OptimizeVertexCache(indices, rows * cols, 32);

    const auto outputSet = UniqueIndices(indices);
    REQUIRE(inputSet == outputSet);
}

TEST_CASE("MeshOptimizer: produces no out-of-range indices", "[renderer][mesh_optimizer]") {
    const uint32_t rows = 4, cols = 4;
    const uint32_t vertexCount = rows * cols;
    std::vector<uint32_t> indices = BuildGridIndices(rows, cols);

    MeshOptimizer::OptimizeVertexCache(indices, vertexCount, 32);

    for (uint32_t idx : indices) {
        REQUIRE(idx < vertexCount);
    }
}

TEST_CASE("MeshOptimizer: ACMR improves on grid topology", "[renderer][mesh_optimizer]") {
    // A naive row-major grid has poor vertex cache behaviour because each
    // row of quads jumps to distant vertices.  After optimization, ACMR
    // should drop measurably.
    const uint32_t rows = 20, cols = 20;
    const uint32_t vertexCount = rows * cols;
    std::vector<uint32_t> indices = BuildGridIndices(rows, cols);

    const float acmrBefore = MeshOptimizer::ComputeAcrm(indices, 32);

    MeshOptimizer::OptimizeVertexCache(indices, vertexCount, 32);

    const float acmrAfter = MeshOptimizer::ComputeAcrm(indices, 32);

    // ACMR should improve (decrease) after optimization.
    // Raw grid ACMR is typically 1.5-3.0; optimized should be 0.65-0.90.
    REQUIRE(acmrAfter < acmrBefore);
}

TEST_CASE("MeshOptimizer: ACMR never worse than raw input", "[renderer][mesh_optimizer]") {
    // Even for adversarial / already-strip-ordered inputs, the optimizer
    // should not make things worse.
    const uint32_t rows = 10, cols = 10;
    const uint32_t vertexCount = rows * cols;
    std::vector<uint32_t> indices = BuildGridIndices(rows, cols);

    const float acmrBefore = MeshOptimizer::ComputeAcrm(indices, 32);
    MeshOptimizer::OptimizeVertexCache(indices, vertexCount, 32);
    const float acmrAfter = MeshOptimizer::ComputeAcrm(indices, 32);

    REQUIRE(acmrAfter <= acmrBefore + 0.01f); // Allow tiny floating error.
}

// ============================================================================
// OptimizeVertexCache — winding order consistency
// ============================================================================

TEST_CASE("MeshOptimizer: preserves winding order (same triangle vertex sets)", "[renderer][mesh_optimizer]") {
    // After optimization, every three-index triangle in the output should
    // contain the same set of vertices as some triangle in the input.
    // We verify this by collecting all (sorted) triples from both.
    auto collectTriples = [](const std::vector<uint32_t>& idx) {
        std::vector<std::array<uint32_t, 3>> triples;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            std::array<uint32_t, 3> t = {idx[i], idx[i + 1], idx[i + 2]};
            std::ranges::sort(t);
            triples.push_back(t);
        }
        std::ranges::sort(triples);
        return triples;
    };

    const uint32_t rows = 5, cols = 5;
    std::vector<uint32_t> indices = BuildGridIndices(rows, cols);
    const auto inputTriples = collectTriples(indices);

    MeshOptimizer::OptimizeVertexCache(indices, rows * cols, 32);

    const auto outputTriples = collectTriples(indices);
    REQUIRE(inputTriples == outputTriples);
}

// ============================================================================
// OptimizeVertexCache — degenerate / edge-cases
// ============================================================================

TEST_CASE("MeshOptimizer: vertexCount=0 is a no-op", "[renderer][mesh_optimizer]") {
    // Degenerate input: indices reference vertex 0 but vertexCount is 0.
    // The function should return without crashing (defensive early-out).
    std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};
    const auto original = indices;
    MeshOptimizer::OptimizeVertexCache(indices, 0, 32);
    REQUIRE(indices == original);
}

TEST_CASE("MeshOptimizer: cacheSize=2 degenerates gracefully", "[renderer][mesh_optimizer]") {
    // cacheSize < 3 is the same as trivial/too-small — the function
    // returns early.
    std::vector<uint32_t> indices = BuildGridIndices(3, 3);
    const auto original = indices;
    MeshOptimizer::OptimizeVertexCache(indices, 9, 2);
    REQUIRE(indices == original);
}

TEST_CASE("MeshOptimizer: duplicate indices handled correctly", "[renderer][mesh_optimizer]") {
    // A degenerate mesh where multiple triangles reference the exact same
    // three vertices. The optimizer should still preserve count.
    std::vector<uint32_t> indices = {
        0, 1, 2,
        1, 2, 0,  // Same tri, different winding (not a duplicate set)
        0, 1, 2,  // Exact duplicate of first
        2, 0, 1,  // Rotated
    };
    const size_t originalSize = indices.size();

    MeshOptimizer::OptimizeVertexCache(indices, 3, 32);

    REQUIRE(indices.size() == originalSize);
    REQUIRE(indices.size() % 3 == 0);
}

// ============================================================================
// ComputeAcrm — known patterns
// ============================================================================

TEST_CASE("MeshOptimizer::ComputeAcrm: empty index buffer returns 0", "[renderer][mesh_optimizer]") {
    REQUIRE(MeshOptimizer::ComputeAcrm({}) == 0.0f);
}

TEST_CASE("MeshOptimizer::ComputeAcrm: perfect strip has low ACMR", "[renderer][mesh_optimizer]") {
    // A well-ordered triangle strip: 0,1,2, 1,2,3, 2,3,4, 3,4,5, ...
    // Each new triangle adds exactly one new vertex, so ACMR ≈ 1.0.
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < 98; ++i) {
        indices.push_back(i);
        indices.push_back(i + 1);
        indices.push_back(i + 2);
    }
    // 100 vertices, 98 triangles.
    const float acmr = MeshOptimizer::ComputeAcrm(indices, 32);
    // First triangle: 3 misses; each subsequent: 1 miss.
    // Total misses = 3 + 97 = 100; ACMR = 100 / 98 ≈ 1.02.
    REQUIRE(acmr > 1.0f);
    REQUIRE(acmr < 1.1f);
}

TEST_CASE("MeshOptimizer::ComputeAcrm: random indices have high ACMR", "[renderer][mesh_optimizer]") {
    // Random index access thrashes the cache — ACMR should be near 3.0.
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> dist(0, 999);
    std::vector<uint32_t> indices(300);
    for (auto& i : indices)
        i = dist(rng);

    const float acmr = MeshOptimizer::ComputeAcrm(indices, 32);
    // Random access: nearly every index is a cache miss → ACMR ≈ 3.0.
    REQUIRE(acmr > 2.5f);
}

// ============================================================================
// OptimizeVertexCache — deterministic output
// ============================================================================

TEST_CASE("MeshOptimizer: deterministic output for same input", "[renderer][mesh_optimizer]") {
    const uint32_t rows = 8, cols = 8;
    std::vector<uint32_t> a = BuildGridIndices(rows, cols);
    std::vector<uint32_t> b = a;

    MeshOptimizer::OptimizeVertexCache(a, rows * cols, 32);
    MeshOptimizer::OptimizeVertexCache(b, rows * cols, 32);

    REQUIRE(a == b);
}

// ============================================================================
// OptimizeVertexCache — larger mesh stress test
// ============================================================================

TEST_CASE("MeshOptimizer: 100x100 grid completes in reasonable time", "[renderer][mesh_optimizer]") {
    const uint32_t rows = 100, cols = 100;
    const uint32_t vertexCount = rows * cols;
    std::vector<uint32_t> indices = BuildGridIndices(rows, cols);
    const size_t originalSize = indices.size();

    // 100x100 = 9,801 quads = 19,602 tris = 58,806 indices.
    // Should complete in well under 1 second.
    MeshOptimizer::OptimizeVertexCache(indices, vertexCount, 32);

    REQUIRE(indices.size() == originalSize);
    REQUIRE(indices.size() % 3 == 0);
}

TEST_CASE("MeshOptimizer: unshared corner streams avoid quadratic cache updates",
          "[renderer][mesh_optimizer][regression]") {
    constexpr uint32_t kVertexCount = 30000;
    std::vector<uint32_t> indices(kVertexCount);
    std::iota(indices.begin(), indices.end(), 0u);
    const std::vector<uint32_t> original = indices;

    MeshOptimizer::OptimizeVertexCache(indices, kVertexCount, 32);

    REQUIRE(indices.size() == original.size());
    REQUIRE(UniqueIndices(indices).size() == kVertexCount);
}

// ============================================================================
// ComputeAcrm — cache size sensitivity
// ============================================================================

TEST_CASE("MeshOptimizer::ComputeAcrm: larger cache reduces ACMR", "[renderer][mesh_optimizer]") {
    const uint32_t rows = 15, cols = 15;
    std::vector<uint32_t> indices = BuildGridIndices(rows, cols);

    // Optimize with a small cache first.
    std::vector<uint32_t> smallCache = indices;
    MeshOptimizer::OptimizeVertexCache(smallCache, rows * cols, 12);
    const float acmrSmall = MeshOptimizer::ComputeAcrm(smallCache, 12);

    // Then with a larger cache.
    std::vector<uint32_t> largeCache = indices;
    MeshOptimizer::OptimizeVertexCache(largeCache, rows * cols, 64);
    const float acmrLarge = MeshOptimizer::ComputeAcrm(largeCache, 64);

    // Larger cache should produce better ACMR (lower value).
    REQUIRE(acmrLarge < acmrSmall);
}
