/** @file ShaderCooker.h
 *  @brief Offline GLSL → SPIR-V shader cooking pipeline.
 *
 *  The ShaderCooker converts human-authored GLSL shaders into SPIR-V binary
 *  modules suitable for direct loading by Vulkan (vkCreateShaderModule) or
 *  OpenGL 4.6+ (glShaderBinary with GL_SHADER_BINARY_FORMAT_SPIR_V).
 *
 *  It depends on glslang (Khronos reference GLSL compiler) and lives in
 *  core/pipeline with no GUI, editor, or window-system dependencies.
 *
 *  Typical workflow:
 *  @code
 *  Horo::Pipeline::ShaderCookSettings settings;
 *  settings.debugInfo = false;
 *  settings.optimize  = false;
 *
 *  auto result = Horo::Pipeline::CookShaderFromFile(
 *      "shaders/basic.vert", Horo::Pipeline::ShaderStage::Vertex, settings);
 *  if (result) {
 *      VkShaderModuleCreateInfo ci{};
 *      ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
 *      ci.codeSize = result.spirv.size() * sizeof(uint32_t);
 *      ci.pCode    = result.spirv.data();
 *      vkCreateShaderModule(device, &ci, nullptr, &module);
 *  }
 *  @endcode
 */
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Pipeline {

/** @brief Shader stages supported by the GLSL → SPIR-V cooker.
 *
 *  Maps to glslang's EShLanguage / VkShaderStageFlagBits.
 *  Only the stages that GLSL source files can represent are included. */
enum class ShaderStage : uint8_t {
    /** Vertex shader (.vert). */
    Vertex = 0,

    /** Fragment shader (.frag). */
    Fragment = 1,

    /** Compute shader (.comp). */
    Compute = 2,

    /** Geometry shader (.geom). */
    Geometry = 3,

    /** Tessellation control shader (.tesc). */
    TessControl = 4,

    /** Tessellation evaluation shader (.tese). */
    TessEvaluation = 5,

    /** Sentinel — must be last. */
    Count
};

/** @brief Configuration for one shader cook operation. */
struct ShaderCookSettings {
    /** Target GLSL version (e.g. 450 for Vulkan 1.0 semantics).
     *  Default: 450. */
    uint32_t glslVersion = 450;

    /** Target SPIR-V version encoded as (major << 16) | (minor << 8).
     *  Default: 0x00010000 (SPIR-V 1.0, maximum compatibility). */
    uint32_t spirvVersion = 0x00010000;

    /** When true, include full source-level debug info in the SPIR-V output.
     *  Increases binary size significantly.  Default: false. */
    bool debugInfo = false;

    /** When true, run SPIR-V optimisation passes.
     *  Requires glslang built with SPIRV-Tools support.  Default: false. */
    bool optimize = false;

    /** Include directories for GLSL #include resolution.
     *  Empty by default — shaders must be self-contained. */
    std::vector<std::string> includeDirs;

    /** Preprocessor macro definitions (name, value pairs).
     *  Equivalent to -DNAME=VALUE on the glslangValidator command line. */
    std::vector<std::pair<std::string, std::string>> defines;
};

/** @brief Result of a shader cook operation.
 *
 *  On success, `spirv` holds the SPIR-V binary module (array of 32-bit words
 *  as required by the SPIR-V specification, Section 2.3 "Physical Layout").
 *  The binary is ready for vkCreateShaderModule without further processing. */
struct CookedShader {
    /** Shader stage of the compiled SPIR-V module. */
    ShaderStage stage = ShaderStage::Vertex;

    /** SPIR-V binary as 32-bit words (SPIR-V spec §2.3). */
    std::vector<uint32_t> spirv;

    /** Error message when cooking failed.  Empty on success. */
    std::string error;

    /** @brief True when cooking succeeded (spirv non-empty, error empty). */
    explicit operator bool() const { return error.empty() && !spirv.empty(); }
};

/** @brief Cooks a GLSL source file into a SPIR-V binary.
 *
 *  Reads the file at `path`, determines the shader stage from `stage`,
 *  and compiles it to SPIR-V using the glslang reference compiler.
 *
 *  @param path     Filesystem path to the GLSL source (.vert, .frag, .comp, etc.).
 *  @param stage    Shader stage to compile for.
 *  @param settings Compilation options (GLSL version, SPIR-V version, etc.).
 *  @return CookedShader with SPIR-V binary, or error on failure. */
CookedShader CookShaderFromFile(const std::string& path,
                                ShaderStage stage,
                                const ShaderCookSettings& settings);

/** @brief Cooks a GLSL source string into a SPIR-V binary.
 *
 *  @param source   GLSL source code as a string.
 *  @param stage    Shader stage to compile for.
 *  @param fileName Logical file name for error messages (e.g. "basic.vert").
 *  @param settings Compilation options.
 *  @return CookedShader with SPIR-V binary, or error on failure. */
CookedShader CookShaderFromSource(const std::string& source,
                                  ShaderStage stage,
                                  const std::string& fileName,
                                  const ShaderCookSettings& settings);

/** @brief Cooks a vertex + fragment shader pair from files.
 *
 *  Convenience wrapper that cooks both files with the same settings.
 *  If either stage fails, the corresponding error is set on that half;
 *  the other half may still have succeeded. */
struct CookedShaderPair {
    CookedShader vertex;
    CookedShader fragment;

    /** @brief True when both vertex and fragment shaders cooked successfully. */
    bool Valid() const { return vertex && fragment; }
};

/** @brief Cooks a vertex/fragment shader pair from two source files.
 *
 *  @param vertPath  Filesystem path to the vertex shader source.
 *  @param fragPath  Filesystem path to the fragment shader source.
 *  @param settings  Compilation options (shared by both stages).
 *  @return CookedShaderPair with results for each stage. */
CookedShaderPair CookShaderPairFromFiles(const std::string& vertPath,
                                         const std::string& fragPath,
                                         const ShaderCookSettings& settings);

/** @brief Returns a human-readable name for a ShaderStage.
 *
 *  @param stage  The shader stage.
 *  @return C-string name (e.g. "Vertex", "Fragment", "Compute"). */
const char* ShaderStageName(ShaderStage stage);

} // namespace Horo::Pipeline
