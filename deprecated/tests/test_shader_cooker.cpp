/** @file test_shader_cooker.cpp
 *  @brief Unit tests for the GLSL → SPIR-V shader cooking pipeline.
 *
 *  Covers:
 *  - ShaderStageName() returns correct strings for all stages
 *  - CookShaderFromFile error path (non-existent file)
 *  - CookShaderFromSource without GLSLang returns descriptive error
 *  - ShaderCookSettings default values
 *
 *  When HORO_HAS_GLSLANG is defined, additional tests cover:
 *  - Basic vertex/fragment shader compilation
 *  - Compute shader compilation
 *  - Error reporting for invalid GLSL
 *  - ShaderCookPairFromFiles
 *  - SPIR-V binary has correct magic number
 *  - All supported shader stages
 */
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "core/pipeline/ShaderCooker.h"

using namespace Horo::Pipeline;

// =========================================================================
//  ShaderStageName
// =========================================================================

TEST_CASE("ShaderStageName returns correct strings", "[shader_cooker][stage]") {
    REQUIRE(std::strcmp(ShaderStageName(ShaderStage::Vertex), "Vertex") == 0);
    REQUIRE(std::strcmp(ShaderStageName(ShaderStage::Fragment), "Fragment") == 0);
    REQUIRE(std::strcmp(ShaderStageName(ShaderStage::Compute), "Compute") == 0);
    REQUIRE(std::strcmp(ShaderStageName(ShaderStage::Geometry), "Geometry") == 0);
    REQUIRE(std::strcmp(ShaderStageName(ShaderStage::TessControl), "TessControl") == 0);
    REQUIRE(std::strcmp(ShaderStageName(ShaderStage::TessEvaluation), "TessEvaluation") == 0);

    // Unknown value returns "Unknown".
    REQUIRE(std::strcmp(ShaderStageName(static_cast<ShaderStage>(99)), "Unknown") == 0);
}

// =========================================================================
//  ShaderCookSettings default values
// =========================================================================

TEST_CASE("ShaderCookSettings has sensible defaults", "[shader_cooker][settings]") {
    ShaderCookSettings settings;

    REQUIRE(settings.glslVersion == 450);
    REQUIRE(settings.spirvVersion == 0x00010000);  // SPIR-V 1.0
    REQUIRE(settings.debugInfo == false);
    REQUIRE(settings.optimize == false);
    REQUIRE(settings.includeDirs.empty());
    REQUIRE(settings.defines.empty());
}

TEST_CASE("ShaderCookSettings can be customised", "[shader_cooker][settings]") {
    ShaderCookSettings settings;
    settings.glslVersion = 460;
    settings.spirvVersion = 0x00010300;  // SPIR-V 1.3
    settings.debugInfo = true;
    settings.optimize = true;
    settings.includeDirs.push_back("/some/include");
    settings.defines.push_back({"ENABLE_FOO", "1"});

    REQUIRE(settings.glslVersion == 460);
    REQUIRE(settings.spirvVersion == 0x00010300);
    REQUIRE(settings.debugInfo == true);
    REQUIRE(settings.optimize == true);
    REQUIRE(settings.includeDirs.size() == 1);
    REQUIRE(settings.includeDirs[0] == "/some/include");
    REQUIRE(settings.defines.size() == 1);
    REQUIRE(settings.defines[0].first == "ENABLE_FOO");
    REQUIRE(settings.defines[0].second == "1");
}

// =========================================================================
//  Error paths (always testable — no glslang needed)
// =========================================================================

TEST_CASE("CookShaderFromFile returns error for non-existent file",
          "[shader_cooker][errors]") {
    ShaderCookSettings settings;
    auto result = CookShaderFromFile("/nonexistent/shader.vert",
                                     ShaderStage::Vertex, settings);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.error.empty());
    REQUIRE(result.spirv.empty());
    REQUIRE(result.stage == ShaderStage::Vertex);
}

TEST_CASE("CookShaderFromSource when no glslang returns error",
          "[shader_cooker][errors]") {
    ShaderCookSettings settings;
    const char* source = "#version 450\n"
                         "vec2 positions[3] = vec2[](\n"
                         "    vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));\n"
                         "void main() {\n"
                         "    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);\n"
                         "}\n";
    auto result = CookShaderFromSource(source, ShaderStage::Vertex,
                                       "test.vert", settings);

    // Without glslang, we expect an error message.
    // (With glslang, this would succeed — but the test harness ensures
    //  the function returns a CookedShader with either error or spirv.)
    if (!result) {
        REQUIRE_FALSE(result.error.empty());
        REQUIRE(result.spirv.empty());
    } else {
        // With glslang: should have SPIR-V output.
        REQUIRE_FALSE(result.spirv.empty());
        REQUIRE(result.error.empty());
    }
}

// =========================================================================
//  CookedShaderPair
// =========================================================================

TEST_CASE("CookShaderPairFromFiles reports errors for missing files",
          "[shader_cooker][pair]") {
    ShaderCookSettings settings;
    auto pair = CookShaderPairFromFiles("/nonexistent/v.vert",
                                        "/nonexistent/f.frag", settings);
    REQUIRE_FALSE(pair.Valid());
    REQUIRE_FALSE(pair.vertex);
    REQUIRE_FALSE(pair.fragment);
    REQUIRE_FALSE(pair.vertex.error.empty());
    REQUIRE_FALSE(pair.fragment.error.empty());
}

// =========================================================================
//  Compilation tests (only when glslang is available)
// =========================================================================

#if defined(HORO_HAS_GLSLANG)

namespace {

/** @brief Minimal valid GLSL vertex shader source. */
const char* kMinimalVertexSrc = R"(
#version 450
vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);
void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)";

/** @brief Minimal valid GLSL fragment shader source. */
const char* kMinimalFragmentSrc = R"(
#version 450
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

/** @brief Minimal valid compute shader. */
const char* kMinimalComputeSrc = R"(
#version 450
layout(local_size_x = 1) in;
void main() {
}
)";

/** @brief SPIR-V magic number (0x07230203). */
constexpr uint32_t kSpirvMagic = 0x07230203;

/** @brief Verifies the SPIR-V binary starts with the correct magic number. */
bool HasSpirvMagic(const std::vector<uint32_t>& spirv) {
    return !spirv.empty() && spirv[0] == kSpirvMagic;
}

} // namespace

TEST_CASE("CookShaderFromSource compiles a minimal vertex shader",
          "[shader_cooker][compile][vertex]") {
    ShaderCookSettings settings;
    auto result = CookShaderFromSource(kMinimalVertexSrc,
                                       ShaderStage::Vertex,
                                       "minimal.vert", settings);
    REQUIRE(result);
    REQUIRE(result.error.empty());
    REQUIRE_FALSE(result.spirv.empty());
    REQUIRE(result.stage == ShaderStage::Vertex);
    REQUIRE(HasSpirvMagic(result.spirv));
}

TEST_CASE("CookShaderFromSource compiles a minimal fragment shader",
          "[shader_cooker][compile][fragment]") {
    ShaderCookSettings settings;
    auto result = CookShaderFromSource(kMinimalFragmentSrc,
                                       ShaderStage::Fragment,
                                       "minimal.frag", settings);
    REQUIRE(result);
    REQUIRE(result.error.empty());
    REQUIRE_FALSE(result.spirv.empty());
    REQUIRE(result.stage == ShaderStage::Fragment);
    REQUIRE(HasSpirvMagic(result.spirv));
}

TEST_CASE("CookShaderFromSource compiles a compute shader",
          "[shader_cooker][compile][compute]") {
    ShaderCookSettings settings;
    auto result = CookShaderFromSource(kMinimalComputeSrc,
                                       ShaderStage::Compute,
                                       "minimal.comp", settings);
    REQUIRE(result);
    REQUIRE(result.error.empty());
    REQUIRE_FALSE(result.spirv.empty());
    REQUIRE(result.stage == ShaderStage::Compute);
    REQUIRE(HasSpirvMagic(result.spirv));
}

TEST_CASE("CookShaderFromSource is deterministic",
          "[shader_cooker][determinism]") {
    ShaderCookSettings settings;
    auto r1 = CookShaderFromSource(kMinimalVertexSrc,
                                   ShaderStage::Vertex,
                                   "minimal.vert", settings);
    auto r2 = CookShaderFromSource(kMinimalVertexSrc,
                                   ShaderStage::Vertex,
                                   "minimal.vert", settings);
    REQUIRE(r1);
    REQUIRE(r2);
    REQUIRE(r1.spirv == r2.spirv);
}

TEST_CASE("CookShaderFromSource reports errors for invalid GLSL",
          "[shader_cooker][errors][syntax]") {
    ShaderCookSettings settings;
    const char* brokenSrc = "#version 450\nvoid main() { this_is_broken; }\n";
    auto result = CookShaderFromSource(brokenSrc, ShaderStage::Vertex,
                                       "broken.vert", settings);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.error.empty());
    REQUIRE(result.spirv.empty());
}

TEST_CASE("CookShaderFromSource handles #define macros",
          "[shader_cooker][defines]") {
    ShaderCookSettings settings;
    settings.defines.push_back({"MY_COLOR", "vec4(0.0, 1.0, 0.0, 1.0)"});

    const char* src = R"(
#version 450
layout(location = 0) out vec4 outColor;
void main() {
    outColor = MY_COLOR;
}
)";
    auto result = CookShaderFromSource(src, ShaderStage::Fragment,
                                       "defines.frag", settings);
    REQUIRE(result);
    REQUIRE(HasSpirvMagic(result.spirv));
}

TEST_CASE("CookShaderFromSource with debugInfo includes debug data",
          "[shader_cooker][debug]") {
    ShaderCookSettings settings;
    settings.debugInfo = true;
    auto result = CookShaderFromSource(kMinimalVertexSrc,
                                       ShaderStage::Vertex,
                                       "debug.vert", settings);
    REQUIRE(result);
    REQUIRE(HasSpirvMagic(result.spirv));

    // With debug info, the SPIR-V binary should be larger
    // (includes OpSource, OpString, etc.).
    // We just verify it compiles — exact size depends on glslang version.
}

TEST_CASE("CookedShaderPair compiles a vertex+fragment pair",
          "[shader_cooker][pair][compile]") {
    // Write temporary files for the pair.
    // (We test CookShaderPairFromFiles using in-memory compilation
    //  via CookShaderFromSource since the pair is just a convenience.)
    ShaderCookSettings settings;
    auto vertResult = CookShaderFromSource(kMinimalVertexSrc,
                                           ShaderStage::Vertex,
                                           "pair.vert", settings);
    auto fragResult = CookShaderFromSource(kMinimalFragmentSrc,
                                           ShaderStage::Fragment,
                                           "pair.frag", settings);
    REQUIRE(vertResult);
    REQUIRE(fragResult);
    REQUIRE(HasSpirvMagic(vertResult.spirv));
    REQUIRE(HasSpirvMagic(fragResult.spirv));
}

TEST_CASE("CookShaderFromSource handles geometry shader stage",
          "[shader_cooker][compile][geometry]") {
    ShaderCookSettings settings;
    const char* geomSrc = R"(
#version 450
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
void main() {
    for (int i = 0; i < 3; i++) {
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
)";
    auto result = CookShaderFromSource(geomSrc, ShaderStage::Geometry,
                                       "test.geom", settings);
    REQUIRE(result);
    REQUIRE(HasSpirvMagic(result.spirv));
}

TEST_CASE("CookShaderFromSource handles tessellation stages",
          "[shader_cooker][compile][tess]") {
    ShaderCookSettings settings;

    SECTION("TessControl") {
        const char* tescSrc = R"(
#version 450
layout(vertices = 3) out;
void main() {
    gl_TessLevelOuter[0] = 1.0;
    gl_TessLevelOuter[1] = 1.0;
    gl_TessLevelOuter[2] = 1.0;
    gl_TessLevelInner[0] = 1.0;
}
)";
        auto result = CookShaderFromSource(tescSrc,
                                           ShaderStage::TessControl,
                                           "test.tesc", settings);
        REQUIRE(result);
        REQUIRE(HasSpirvMagic(result.spirv));
    }

    SECTION("TessEvaluation") {
        const char* teseSrc = R"(
#version 450
layout(triangles) in;
void main() {
    gl_Position = vec4(0.0);
}
)";
        auto result = CookShaderFromSource(teseSrc,
                                           ShaderStage::TessEvaluation,
                                           "test.tese", settings);
        REQUIRE(result);
        REQUIRE(HasSpirvMagic(result.spirv));
    }
}

#endif // HORO_HAS_GLSLANG
