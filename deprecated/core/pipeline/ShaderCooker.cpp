/** @file ShaderCooker.cpp
 *  @brief Offline GLSL → SPIR-V shader cooking using the glslang reference
 *         compiler.
 *
 *  When HORO_HAS_GLSLANG is defined, uses the full glslang C++ API.
 *  Without it, all cook functions return descriptive errors indicating
 *  that glslang support was not compiled in.
 */
#include "core/pipeline/ShaderCooker.h"

#include <array>
#include <fstream>
#include <sstream>
#include <string_view>

#if defined(HORO_HAS_GLSLANG)
#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <cstring>
#include <memory>
#endif

#include <cassert>

#include "core/Logger.h"

namespace Horo::Pipeline {

// =========================================================================
//  Stage name helper
// =========================================================================

const char* ShaderStageName(ShaderStage stage) {
    using enum ShaderStage;
    switch (stage) {
        case Vertex:          return "Vertex";
        case Fragment:        return "Fragment";
        case Compute:         return "Compute";
        case Geometry:        return "Geometry";
        case TessControl:     return "TessControl";
        case TessEvaluation:  return "TessEvaluation";
        default:              return "Unknown";
    }
}

#if defined(HORO_HAS_GLSLANG)

// =========================================================================
//  Internal helpers (glslang available)
// =========================================================================

namespace {

/** @brief Maps our ShaderStage enum to glslang's EShLanguage. */
EShLanguage ToGlslangStage(ShaderStage stage) {
    using enum ShaderStage;
    switch (stage) {
        case Vertex:          return EShLangVertex;
        case Fragment:        return EShLangFragment;
        case Compute:         return EShLangCompute;
        case Geometry:        return EShLangGeometry;
        case TessControl:     return EShLangTessControl;
        case TessEvaluation:  return EShLangTessEvaluation;
        default:
            assert(false && "Unknown ShaderStage");
            return EShLangVertex;
    }
}

/** @brief One-time glslang process initialisation.
 *
 *  glslang::InitializeProcess() allocates internal tables and must be called
 *  exactly once before any compilation.  glslang::FinalizeProcess() is called
 *  at process exit via atexit. */
void EnsureGlslangInit() {
    static bool s_initialised = false;
    if (!s_initialised) {
        glslang::InitializeProcess();
        s_initialised = true;
    }
}

/** @brief Builds desktop-oriented default GLSL resource limits. */
TBuiltInResource BuildDefaultResources() {
        TBuiltInResource r{};
        r.maxLights                                 = 32;
        r.maxClipPlanes                             = 6;
        r.maxTextureUnits                           = 32;
        r.maxTextureCoords                          = 32;
        r.maxVertexAttribs                          = 64;
        r.maxVertexUniformComponents                = 4096;
        r.maxVaryingFloats                          = 64;
        r.maxVertexTextureImageUnits                = 32;
        r.maxCombinedTextureImageUnits              = 80;
        r.maxTextureImageUnits                      = 32;
        r.maxFragmentUniformComponents              = 4096;
        r.maxDrawBuffers                            = 32;
        r.maxVertexUniformVectors                   = 128;
        r.maxVaryingVectors                         = 8;
        r.maxFragmentUniformVectors                 = 16;
        r.maxVertexOutputVectors                    = 16;
        r.maxFragmentInputVectors                   = 15;
        r.minProgramTexelOffset                     = -8;
        r.maxProgramTexelOffset                     = 7;
        r.maxClipDistances                          = 8;
        r.maxComputeWorkGroupCountX                 = 65535;
        r.maxComputeWorkGroupCountY                 = 65535;
        r.maxComputeWorkGroupCountZ                 = 65535;
        r.maxComputeWorkGroupSizeX                  = 1024;
        r.maxComputeWorkGroupSizeY                  = 1024;
        r.maxComputeWorkGroupSizeZ                  = 64;
        r.maxComputeUniformComponents               = 1024;
        r.maxComputeTextureImageUnits               = 16;
        r.maxComputeImageUniforms                   = 8;
        r.maxComputeAtomicCounters                  = 8;
        r.maxComputeAtomicCounterBuffers            = 1;
        r.maxVaryingComponents                      = 60;
        r.maxVertexOutputComponents                 = 64;
        r.maxGeometryInputComponents                = 64;
        r.maxGeometryOutputComponents               = 128;
        r.maxFragmentInputComponents                = 128;
        r.maxImageUnits                             = 8;
        r.maxCombinedImageUnitsAndFragmentOutputs   = 8;
        r.maxCombinedShaderOutputResources          = 8;
        r.maxImageSamples                           = 0;
        r.maxVertexImageUniforms                    = 0;
        r.maxTessControlImageUniforms               = 0;
        r.maxTessEvaluationImageUniforms            = 0;
        r.maxGeometryImageUniforms                  = 0;
        r.maxFragmentImageUniforms                  = 8;
        r.maxCombinedImageUniforms                  = 8;
        r.maxGeometryTextureImageUnits              = 16;
        r.maxGeometryOutputVertices                 = 256;
        r.maxGeometryTotalOutputComponents          = 1024;
        r.maxGeometryUniformComponents              = 1024;
        r.maxGeometryVaryingComponents              = 64;
        r.maxTessControlInputComponents             = 128;
        r.maxTessControlOutputComponents            = 128;
        r.maxTessControlTextureImageUnits           = 16;
        r.maxTessControlUniformComponents           = 1024;
        r.maxTessControlTotalOutputComponents       = 4096;
        r.maxTessEvaluationInputComponents          = 128;
        r.maxTessEvaluationOutputComponents         = 128;
        r.maxTessEvaluationTextureImageUnits        = 16;
        r.maxTessEvaluationUniformComponents        = 1024;
        r.maxTessPatchComponents                    = 120;
        r.maxPatchVertices                          = 32;
        r.maxTessGenLevel                           = 64;
        r.maxViewports                              = 16;
        r.maxVertexAtomicCounters                   = 0;
        r.maxTessControlAtomicCounters              = 0;
        r.maxTessEvaluationAtomicCounters           = 0;
        r.maxGeometryAtomicCounters                 = 0;
        r.maxFragmentAtomicCounters                 = 8;
        r.maxCombinedAtomicCounters                 = 8;
        r.maxAtomicCounterBindings                  = 1;
        r.maxVertexAtomicCounterBuffers             = 0;
        r.maxTessControlAtomicCounterBuffers        = 0;
        r.maxTessEvaluationAtomicCounterBuffers     = 0;
        r.maxGeometryAtomicCounterBuffers           = 0;
        r.maxFragmentAtomicCounterBuffers           = 1;
        r.maxCombinedAtomicCounterBuffers           = 1;
        r.maxAtomicCounterBufferSize                = 16384;
        r.maxTransformFeedbackBuffers               = 4;
        r.maxTransformFeedbackInterleavedComponents = 64;
        r.maxCullDistances                          = 8;
        r.maxCombinedClipAndCullDistances           = 8;
        r.maxSamples                                = 4;
        r.maxMeshOutputVerticesNV                   = 256;
        r.maxMeshOutputPrimitivesNV                 = 512;
        r.maxMeshWorkGroupSizeX_NV                  = 32;
        r.maxMeshWorkGroupSizeY_NV                  = 1;
        r.maxMeshWorkGroupSizeZ_NV                  = 1;
        r.maxTaskWorkGroupSizeX_NV                  = 32;
        r.maxTaskWorkGroupSizeY_NV                  = 1;
        r.maxTaskWorkGroupSizeZ_NV                  = 1;
        r.maxMeshViewCountNV                        = 4;
        r.maxDualSourceDrawBuffersEXT               = 1;
        r.limits.nonInductiveForLoops               = true;
        r.limits.whileLoops                         = true;
        r.limits.doWhileLoops                       = true;
        r.limits.generalUniformIndexing             = true;
        r.limits.generalAttributeMatrixVectorIndexing = true;
        r.limits.generalVaryingIndexing               = true;
        r.limits.generalSamplerIndexing               = true;
        r.limits.generalVariableIndexing              = true;
        r.limits.generalConstantMatrixVectorIndexing  = true;
        return r;
}

/** @brief Returns shared desktop-oriented GLSL resource limits. */
const TBuiltInResource& GetDefaultResources() {
    static const TBuiltInResource res = BuildDefaultResources();
    return res;
}

/** @brief Custom GLSL include handler that resolves #include directives.
 *
 *  Resolution order:
 *  1. Relative to the includer's directory (standard behaviour).
 *  2. Relative to each entry in the user-provided include search path. */
class DirectoryIncluder : public glslang::TShader::Includer {
public:
    explicit DirectoryIncluder(
        const std::vector<std::string>& searchDirs)
        : m_searchDirs(searchDirs) {}

    IncludeResult* includeSystem(const char* headerName,
                                 const char* /*includerName*/,
                                 size_t /*inclusionDepth*/) override {
        return TryInclude(headerName, true);
    }

    IncludeResult* includeLocal(const char* headerName,
                                const char* includerName,
                                size_t /*inclusionDepth*/) override {
        // First try relative to the includer's directory.
        std::string localPath = DirName(includerName) + "/" + headerName;
        if (IncludeResult* result = TryReadFile(localPath); result)
            return result;

        // Fall back to the search path.
        return TryInclude(headerName, false);
    }

    void releaseInclude(IncludeResult* result) override {
        if (!result)
            return;

        std::erase_if(m_ownedIncludes, [result](const auto& include) {
            return &include->result == result;
        });
    }

private:
    struct OwnedInclude {
        OwnedInclude(std::string sourcePath, std::vector<char> sourceData)
            : path(std::move(sourcePath))
            , data(std::move(sourceData))
            , result(path, data.data(), data.size() - 1, nullptr) {}

        std::string path;
        std::vector<char> data;
        IncludeResult result;
    };

    IncludeResult* TryInclude(const char* headerName, bool system) {
        // Try each search directory.
        for (const auto& dir : m_searchDirs) {
            std::string path = dir + "/" + headerName;
            if (IncludeResult* result = TryReadFile(path); result)
                return result;
        }
        // If system include, also try the header name directly.
        if (system) {
            if (IncludeResult* result = TryReadFile(headerName); result)
                return result;
        }
        return nullptr;
    }

    IncludeResult* TryReadFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return nullptr;

        const auto size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::vector<char> data(size + 1, '\0');
        f.read(data.data(), static_cast<std::streamsize>(size));

        auto owned = std::make_unique<OwnedInclude>(path, std::move(data));
        IncludeResult* result = &owned->result;
        m_ownedIncludes.push_back(std::move(owned));
        return result;
    }

    static std::string DirName(std::string_view path) {
        const auto pos = path.find_last_of("/\\");
        return pos != std::string_view::npos
                   ? std::string(path.substr(0, pos))
                   : ".";
    }

    const std::vector<std::string>& m_searchDirs;
    std::vector<std::unique_ptr<OwnedInclude>> m_ownedIncludes;
};

/** @brief Reads a file into a string. */
std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// =========================================================================
//  Public API (glslang available)
// =========================================================================

CookedShader CookShaderFromFile(const std::string& path,
                                ShaderStage stage,
                                const ShaderCookSettings& settings) {
    const std::string source = ReadFile(path);
    if (source.empty()) {
        CookedShader result;
        result.stage = stage;
        result.error = "Cannot open shader file: " + path;
        return result;
    }
    return CookShaderFromSource(source, stage, path, settings);
}

CookedShader CookShaderFromSource(const std::string& source,
                                  ShaderStage stage,
                                  const std::string& fileName,
                                  const ShaderCookSettings& settings) {
    CookedShader result;
    result.stage = stage;

    EnsureGlslangInit();

    const EShLanguage glslangStage = ToGlslangStage(stage);

    glslang::TShader shader(glslangStage);

    // Set source with file name for #line directives and error messages.
    const std::array srcStrings = {source.c_str()};
    const std::array srcLengths = {static_cast<int>(source.size())};
    const std::array srcNames = {fileName.c_str()};
    shader.setStringsWithLengthsAndNames(
        srcStrings.data(), srcLengths.data(), srcNames.data(), 1);

    // Set environment targets.
    const uint32_t major = (settings.spirvVersion >> 16) & 0xFF;
    const uint32_t minor = (settings.spirvVersion >> 8) & 0xFF;

    // Map SPIR-V version from settings to glslang enum.
    // Values match the encoding: (1 << 16) | (minor << 8).
    glslang::EShTargetLanguageVersion targetSpvVer = glslang::EShTargetSpv_1_0;
    if (major == 1 && minor == 0)      targetSpvVer = glslang::EShTargetSpv_1_0;
    else if (major == 1 && minor == 1) targetSpvVer = glslang::EShTargetSpv_1_1;
    else if (major == 1 && minor == 2) targetSpvVer = glslang::EShTargetSpv_1_2;
    else if (major == 1 && minor == 3) targetSpvVer = glslang::EShTargetSpv_1_3;
    else if (major == 1 && minor == 4) targetSpvVer = glslang::EShTargetSpv_1_4;
    else if (major == 1 && minor == 5) targetSpvVer = glslang::EShTargetSpv_1_5;
    else if (major == 1 && minor == 6) targetSpvVer = glslang::EShTargetSpv_1_6;

    shader.setEnvInput(glslang::EShSourceGlsl, glslangStage,
                       glslang::EShClientVulkan, 100);  // Vulkan 1.0
    shader.setEnvClient(glslang::EShClientVulkan,
                        glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, targetSpvVer);

    // Build preprocessor preamble for macro definitions.
    std::string preamble;
    for (const auto& [name, value] : settings.defines) {
        preamble += "#define " + name + " " + value + "\n";
    }
    if (!preamble.empty()) {
        shader.setPreamble(preamble.c_str());
    }

    // Wire the custom include resolver if search dirs are specified.
    DirectoryIncluder includer(settings.includeDirs);

    // Build message flags.
    EShMessages messages = EShMsgDefault;
    if (settings.debugInfo) {
        messages = EShMessages(messages | EShMsgDebugInfo);
    }
    // Note: ESshMsgVulkanRules, EShMsgSpvRules are implied by the
    // Vulkan/SPIR-V target environment.

    // Parse — passing the includer for #include resolution.
    if (const TBuiltInResource& resources = GetDefaultResources();
        !shader.parse(&resources, settings.glslVersion, false, messages,
                      includer)) {
        result.error = std::string("Shader parse error in '") + fileName +
                       "':\n" + shader.getInfoLog() + "\n" +
                       shader.getInfoDebugLog();
        LogWarn("ShaderCooker: {}", result.error);
        return result;
    }

    // Link (required even for single-shader "programs" — glslang uses the
    // link step to finalize the intermediate representation).
    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        result.error = std::string("Shader link error in '") + fileName +
                       "':\n" + program.getInfoLog() + "\n" +
                       program.getInfoDebugLog();
        LogWarn("ShaderCooker: {}", result.error);
        return result;
    }

    // Generate SPIR-V from the intermediate representation.
    glslang::GlslangToSpv(*program.getIntermediate(glslangStage),
                          result.spirv);

    if (result.spirv.empty()) {
        result.error = std::string("SPIR-V generation produced empty output"
                                   " for '") + fileName + "'";
        LogWarn("ShaderCooker: {}", result.error);
    }

    return result;
}

CookedShaderPair CookShaderPairFromFiles(
    const std::string& vertPath,
    const std::string& fragPath,
    const ShaderCookSettings& settings) {

    CookedShaderPair pair;
    pair.vertex   = CookShaderFromFile(vertPath, ShaderStage::Vertex, settings);
    pair.fragment = CookShaderFromFile(fragPath, ShaderStage::Fragment, settings);
    return pair;
}

#else // !defined(HORO_HAS_GLSLANG)

// =========================================================================
//  Stub implementations (glslang not available)
// =========================================================================

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

CookedShader CookShaderFromFile(const std::string& path,
                                ShaderStage stage,
                                const ShaderCookSettings& /*settings*/) {
    CookedShader result;
    result.stage = stage;
    // Try to read the file so that "file not found" is still diagnosed.
    if (ReadFile(path).empty()) {
        result.error = "Cannot open shader file: " + path;
    } else {
        result.error = "Shader cooking requires glslang; "
                       "rebuild with HORO_ENGINE_ENABLE_SHADER_COOKER=ON";
    }
    return result;
}

CookedShader CookShaderFromSource(const std::string& /*source*/,
                                  ShaderStage stage,
                                  const std::string& /*fileName*/,
                                  const ShaderCookSettings& /*settings*/) {
    CookedShader result;
    result.stage = stage;
    result.error = "Shader cooking requires glslang; "
                   "rebuild with HORO_ENGINE_ENABLE_SHADER_COOKER=ON";
    return result;
}

CookedShaderPair CookShaderPairFromFiles(
    const std::string& vertPath,
    const std::string& fragPath,
    const ShaderCookSettings& /*settings*/) {
    CookedShaderPair pair;
    pair.vertex   = CookShaderFromFile(vertPath, ShaderStage::Vertex, {});
    pair.fragment = CookShaderFromFile(fragPath, ShaderStage::Fragment, {});
    return pair;
}

#endif // HORO_HAS_GLSLANG

} // namespace Horo::Pipeline
