#include "renderer/VulkanRenderBackend.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/Assert.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"

#if defined(MONOLITH_HAS_VULKAN)
#include <volk.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#endif

namespace Monolith
{

#if defined(MONOLITH_HAS_VULKAN)

  namespace
  {

    constexpr uint32_t kInvalidQueueFamily = std::numeric_limits<uint32_t>::max();
    constexpr VkFormat kOffscreenTargetFormat = VK_FORMAT_R8G8B8A8_UNORM;
    constexpr const char *kEditorViewportTargetKey = "__editor.viewport.scene";
    constexpr std::array<const char *, static_cast<size_t>(SceneTextureSemantic::Count)> kSceneTextureTargetKeys = {
        "__scene.gbuffer.base_color",
        "__scene.gbuffer.depth",
        "__scene.gbuffer.normal",
        "__scene.gbuffer.roughness_metallic",
        "__scene.gbuffer.emissive",
        "__scene.velocity"};
    constexpr std::array<const char *, static_cast<size_t>(GiHistorySemantic::Count)> kGiHistoryTargetKeys = {
        "__gi.history.diffuse_irradiance",
        "__gi.history.specular_irradiance",
        "__gi.history.validation",
        "__gi.history.moments"};
    constexpr const char *kMeshDistanceFieldTargetKey = "__scene.tracing.mesh_distance_field";
    constexpr const char *kGlobalDistanceFieldTargetKey = "__scene.tracing.global_distance_field";
    constexpr const char *kCachedHitLightingRadianceTargetKey = "__scene.cached_hit_lighting.radiance";
    constexpr const char *kCachedHitLightingMomentsTargetKey = "__scene.cached_hit_lighting.moments";

    uint64_t HashResourceKey(const std::string &key)
    {
      constexpr uint64_t kFNVOffset = 14695981039346656037ull;
      constexpr uint64_t kFNVPrime = 1099511628211ull;

      uint64_t hash = kFNVOffset;
      for (unsigned char c : key)
      {
        hash ^= static_cast<uint64_t>(c);
        hash *= kFNVPrime;
      }
      return hash;
    }

    const char *SceneTextureTargetKey(SceneTextureSemantic semantic)
    {
      return kSceneTextureTargetKeys[static_cast<size_t>(semantic)];
    }

    const char *GiHistoryTargetKey(GiHistorySemantic semantic)
    {
      return kGiHistoryTargetKeys[static_cast<size_t>(semantic)];
    }

    const char *VkResultName(VkResult result)
    {
      switch (result)
      {
      case VK_SUCCESS:
        return "VK_SUCCESS";
      case VK_NOT_READY:
        return "VK_NOT_READY";
      case VK_TIMEOUT:
        return "VK_TIMEOUT";
      case VK_EVENT_SET:
        return "VK_EVENT_SET";
      case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
      case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
      case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
      case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
      case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
      case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
      case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
      case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
      case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
      case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
      default:
        return "VK_ERROR_UNKNOWN";
      }
    }

    bool CheckVk(VkResult result, std::string &outError, const char *context)
    {
      if (result == VK_SUCCESS)
        return true;
      outError = std::string(context) + " failed: " + VkResultName(result);
      return false;
    }

    std::vector<char> ReadBinaryFile(const std::string &filePath)
    {
      std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
      if (!stream)
        return {};

      const std::streamsize fileSize = stream.tellg();
      if (fileSize <= 0)
        return {};

      std::vector<char> bytes(static_cast<size_t>(fileSize));
      stream.seekg(0, std::ios::beg);
      if (!stream.read(bytes.data(), fileSize))
        return {};
      return bytes;
    }

    bool TryCreateShaderModule(VkDevice device,
                               const std::vector<char> &spirvBytes,
                               VkShaderModule *outShaderModule,
                               std::string &outError,
                               const char *context)
    {
      if (!outShaderModule)
        return false;
      *outShaderModule = VK_NULL_HANDLE;

      if (spirvBytes.empty())
        return false;
      if ((spirvBytes.size() % sizeof(uint32_t)) != 0)
      {
        outError = std::string(context) + " failed: SPIR-V bytecode size is not a multiple of 4.";
        return false;
      }

      const VkShaderModuleCreateInfo shaderModuleInfo{
          .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .codeSize = spirvBytes.size(),
          .pCode = reinterpret_cast<const uint32_t *>(spirvBytes.data()),
      };
      return CheckVk(vkCreateShaderModule(device, &shaderModuleInfo, nullptr, outShaderModule),
                     outError,
                     context);
    }

    uint32_t FindMemoryTypeIndex(VkPhysicalDevice physicalDevice,
                                 uint32_t memoryTypeBits,
                                 VkMemoryPropertyFlags requiredFlags)
    {
      VkPhysicalDeviceMemoryProperties memoryProperties{};
      vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
      for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
      {
        if ((memoryTypeBits & (1u << i)) == 0)
          continue;
        if ((memoryProperties.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags)
          return i;
      }
      return kInvalidQueueFamily;
    }

    bool CreateHostVisibleBuffer(VkPhysicalDevice physicalDevice,
                                 VkDevice device,
                                 VkDeviceSize size,
                                 VkBufferUsageFlags usage,
                                 VkBuffer *outBuffer,
                                 VkDeviceMemory *outMemory,
                                 std::string &outError,
                                 const char *context)
    {
      if (!outBuffer || !outMemory)
        return false;

      *outBuffer = VK_NULL_HANDLE;
      *outMemory = VK_NULL_HANDLE;

      const VkBufferCreateInfo bufferInfo{
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .size = size,
          .usage = usage,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 0,
          .pQueueFamilyIndices = nullptr,
      };
      if (!CheckVk(vkCreateBuffer(device, &bufferInfo, nullptr, outBuffer), outError, context))
        return false;

      VkMemoryRequirements memoryRequirements{};
      vkGetBufferMemoryRequirements(device, *outBuffer, &memoryRequirements);
      const uint32_t memoryTypeIndex =
          FindMemoryTypeIndex(physicalDevice,
                              memoryRequirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      if (memoryTypeIndex == kInvalidQueueFamily)
      {
        outError = std::string(context) + " failed: no host-visible coherent memory type.";
        vkDestroyBuffer(device, *outBuffer, nullptr);
        *outBuffer = VK_NULL_HANDLE;
        return false;
      }

      const VkMemoryAllocateInfo allocInfo{
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = nullptr,
          .allocationSize = memoryRequirements.size,
          .memoryTypeIndex = memoryTypeIndex,
      };
      if (!CheckVk(vkAllocateMemory(device, &allocInfo, nullptr, outMemory), outError, context))
      {
        vkDestroyBuffer(device, *outBuffer, nullptr);
        *outBuffer = VK_NULL_HANDLE;
        return false;
      }

      if (!CheckVk(vkBindBufferMemory(device, *outBuffer, *outMemory, 0), outError, context))
      {
        vkFreeMemory(device, *outMemory, nullptr);
        vkDestroyBuffer(device, *outBuffer, nullptr);
        *outMemory = VK_NULL_HANDLE;
        *outBuffer = VK_NULL_HANDLE;
        return false;
      }

      return true;
    }

    bool HasFormatFeature(VkPhysicalDevice physicalDevice,
                          VkFormat format,
                          VkFormatFeatureFlags requiredFeatures)
    {
      VkFormatProperties properties{};
      vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
      return (properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
    }

    bool FindSupportedDepthFormat(VkPhysicalDevice physicalDevice,
                                  bool requireTransferSrc,
                                  VkFormat *outFormat)
    {
      if (!outFormat)
        return false;
      *outFormat = VK_FORMAT_UNDEFINED;

      const std::array<VkFormat, 3> candidates = {
          VK_FORMAT_D32_SFLOAT,
          VK_FORMAT_D32_SFLOAT_S8_UINT,
          VK_FORMAT_D24_UNORM_S8_UINT,
      };
      for (VkFormat candidate : candidates)
      {
        if (!HasFormatFeature(physicalDevice,
                              candidate,
                              VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
          continue;
        }

        if (requireTransferSrc &&
            !HasFormatFeature(physicalDevice, candidate, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        {
          continue;
        }

        *outFormat = candidate;
        return true;
      }

      return false;
    }

    struct OpaqueMaterialGpuData
    {
      float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      float roughness = 0.5f;
      float metallic = 0.0f;
      float uvScale = 1.0f;
      float padding = 0.0f;
    };

    constexpr uint32_t kOpaqueMaterialDescriptorCapacity = 2048u;
    constexpr uint32_t kFallbackOpaqueIndexCount = 3u;
    constexpr std::array<uint32_t, kFallbackOpaqueIndexCount> kFallbackOpaqueIndices = {0u, 1u, 2u};

    bool IsImGuiVulkanTextureApiReady()
    {
      if (ImGui::GetCurrentContext() == nullptr)
        return false;
      const ImGuiIO &io = ImGui::GetIO();
      return io.BackendRendererUserData != nullptr;
    }

  } // namespace

  struct VulkanRenderBackend::Context
  {
    struct MeshGpuBuffers
    {
      VkBuffer vertexBuffer = VK_NULL_HANDLE;
      VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
      VkBuffer indexBuffer = VK_NULL_HANDLE;
      VkDeviceMemory indexMemory = VK_NULL_HANDLE;
      uint32_t indexCount = 0;
    };

    struct OffscreenRenderTarget
    {
      uint32_t width = 0;
      uint32_t height = 0;
      uint64_t generation = 0;
      VkImage image = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      VkImageView imageView = VK_NULL_HANDLE;
      VkSampler sampler = VK_NULL_HANDLE;
      VkFramebuffer framebuffer = VK_NULL_HANDLE;
      VkDescriptorSet imguiDescriptorSet = VK_NULL_HANDLE;
      bool readyForSampling = false;
      VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    GLFWwindow *window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = kInvalidQueueFamily;
    uint32_t presentQueueFamily = kInvalidQueueFamily;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent = {1, 1};
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkRenderPass opaqueRenderPass = VK_NULL_HANDLE;
    VkRenderPass offscreenRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout opaquePipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache opaquePipelineCache = VK_NULL_HANDLE;
    std::array<std::string, 2> opaqueShaderEntryNames = {"main", "main"};
    std::array<VkPipelineShaderStageCreateInfo, 2> opaqueShaderStages{};
    VkShaderModule opaqueVertexShaderModule = VK_NULL_HANDLE;
    VkShaderModule opaqueFragmentShaderModule = VK_NULL_HANDLE;
    VkPipeline opaqueGraphicsPipeline = VK_NULL_HANDLE;
    std::unordered_map<uint8_t, VkPipeline> opaquePipelineCacheByKey;
    std::unordered_map<const Mesh *, MeshGpuBuffers> opaqueMeshGpuBuffers;
    std::unordered_map<std::string, OffscreenRenderTarget> offscreenTargets;
    VkDescriptorSetLayout opaqueMaterialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool opaqueMaterialDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> opaqueMaterialDescriptorSets;
    VkBuffer opaqueMaterialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory opaqueMaterialBufferMemory = VK_NULL_HANDLE;
    void *opaqueMaterialBufferMapped = nullptr;
    VkBuffer opaqueFallbackIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory opaqueFallbackIndexBufferMemory = VK_NULL_HANDLE;
    bool opaqueShaderPipelineScaffoldReady = false;
    bool opaqueGraphicsPipelineScaffoldReady = false;
    bool opaqueGraphicsPipelineExecutable = false;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> opaqueFramebuffers;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<bool> imageWasPresented;
    VkBuffer colorReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory colorReadbackMemory = VK_NULL_HANDLE;
    VkBuffer depthReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory depthReadbackMemory = VK_NULL_HANDLE;
    VkDeviceSize colorReadbackSize = 0;
    VkDeviceSize depthReadbackSize = 0;
    bool supportsColorReadback = false;
    bool supportsDepthReadback = false;
    bool hasColorReadbackData = false;
    bool hasDepthReadbackData = false;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
    uint32_t activeImageIndex = 0;
    bool frameCommandsRecorded = false;
  };

  namespace
  {

    constexpr bool IsSupportedVulkanScenePass(const RenderPassId passId)
    {
      return passId == RenderPassId::OpaqueScene ||
             passId == RenderPassId::DeferredOpaque ||
             passId == RenderPassId::CompatibilityScene;
    }

    uint8_t PackOpaquePipelineKey(const VulkanRenderBackend::OpaquePipelineKey &key)
    {
      uint8_t packed = 0u;
      packed |= key.usesAlbedoMap ? (1u << 0) : 0u;
      packed |= key.usesCustomShader ? (1u << 1) : 0u;
      packed |= key.writesDepth ? (1u << 2) : 0u;
      packed |= key.depthTestEnabled ? (1u << 3) : 0u;
      return packed;
    }

  } // namespace

  VulkanRenderBackend::TranslatedMaterialState VulkanRenderBackend::TranslateMaterialState(
      const Material &material)
  {
    TranslatedMaterialState translated;
    translated.baseColor = material.color;
    translated.roughness = material.roughness;
    translated.metallic = material.metallic;
    translated.uvScale = material.uvScale;
    translated.usesAlbedoMap = material.albedoMap && material.albedoMap->IsValid();
    translated.usesCustomShader = material.shader && material.shader->IsValid();
    return translated;
  }

  int VulkanRenderBackend::ResolveIndexCount(const Mesh &mesh)
  {
    return mesh.GetIndexCount();
  }

  VulkanRenderBackend::OpaquePipelineKey VulkanRenderBackend::BuildOpaquePipelineKey(
      const TranslatedMaterialState &materialState)
  {
    OpaquePipelineKey key;
    key.usesAlbedoMap = materialState.usesAlbedoMap;
    key.usesCustomShader = materialState.usesCustomShader;
    key.writesDepth = true;
    key.depthTestEnabled = true;
    return key;
  }

  VulkanRenderBackend::VulkanRenderBackend(void *nativeWindowHandle)
  {
    Initialize(nativeWindowHandle);
  }

  VulkanRenderBackend::~VulkanRenderBackend()
  {
    Shutdown();
  }

  RenderBackendCapabilities VulkanRenderBackend::GetCapabilities() const
  {
    RenderBackendCapabilities caps = GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
    caps.supportsDebugDraw = false;
    caps.supportsDebugLabels = false;
    caps.supportsOffscreenTargets = true;
    caps.supportsNativeTextureHandles = true;
    caps.supportsReadback = m_context && m_context->supportsColorReadback;
    caps.supportsDepthReadback = m_context && m_context->supportsDepthReadback;
    caps.supportsDebugHud = false;
    caps.supportsComputePasses = false;
    caps.supportsGpuTimestamps = false;
    caps.supportsBindlessResources = false;
    return caps;
  }

  bool VulkanRenderBackend::IsInitialized() const
  {
    return m_context && m_context->device != VK_NULL_HANDLE && m_context->swapchain != VK_NULL_HANDLE;
  }

  bool VulkanRenderBackend::HasOpaqueRasterScaffold() const
  {
    return m_context && m_context->opaqueRenderPass != VK_NULL_HANDLE &&
           m_context->opaquePipelineLayout != VK_NULL_HANDLE &&
           m_context->opaqueFramebuffers.size() == m_context->swapchainImageViews.size() &&
           !m_context->opaqueFramebuffers.empty();
  }

  bool VulkanRenderBackend::HasOpaquePipelineCreationScaffold() const
  {
    return HasOpaqueRasterScaffold() && m_context && m_context->opaquePipelineCache != VK_NULL_HANDLE;
  }

  bool VulkanRenderBackend::HasOpaqueShaderPipelineScaffold() const
  {
    return HasOpaquePipelineCreationScaffold() && m_context && m_context->opaqueShaderPipelineScaffoldReady;
  }

  bool VulkanRenderBackend::HasOpaqueGraphicsPipelineScaffold() const
  {
    return HasOpaqueShaderPipelineScaffold() && m_context && m_context->opaqueGraphicsPipelineScaffoldReady;
  }

  bool VulkanRenderBackend::HasOpaqueDrawExecutionReady() const
  {
    return HasOpaqueGraphicsPipelineScaffold() && m_context &&
           m_context->opaqueGraphicsPipelineExecutable &&
           m_context->opaqueFallbackIndexBuffer != VK_NULL_HANDLE &&
           m_context->opaqueMaterialSetLayout != VK_NULL_HANDLE &&
           !m_context->opaqueMaterialDescriptorSets.empty() &&
           m_context->opaqueMaterialBufferMapped != nullptr;
  }

  bool VulkanRenderBackend::TryGetImGuiVulkanInitData(void **outInstance,
                                                      void **outPhysicalDevice,
                                                      void **outDevice,
                                                      uint32_t *outQueueFamily,
                                                      void **outQueue,
                                                      void **outRenderPass,
                                                      uint32_t *outImageCount) const
  {
    if (!IsInitialized() || !m_context)
      return false;

    if (outInstance)
      *outInstance = reinterpret_cast<void *>(m_context->instance);
    if (outPhysicalDevice)
      *outPhysicalDevice = reinterpret_cast<void *>(m_context->physicalDevice);
    if (outDevice)
      *outDevice = reinterpret_cast<void *>(m_context->device);
    if (outQueueFamily)
      *outQueueFamily = m_context->graphicsQueueFamily;
    if (outQueue)
      *outQueue = reinterpret_cast<void *>(m_context->graphicsQueue);
    if (outRenderPass)
      *outRenderPass = reinterpret_cast<void *>(m_context->opaqueRenderPass);
    if (outImageCount)
      *outImageCount = static_cast<uint32_t>(m_context->swapchainImages.size());

    return m_context->instance != VK_NULL_HANDLE &&
           m_context->physicalDevice != VK_NULL_HANDLE &&
           m_context->device != VK_NULL_HANDLE &&
           m_context->graphicsQueue != VK_NULL_HANDLE &&
           m_context->opaqueRenderPass != VK_NULL_HANDLE &&
           !m_context->swapchainImages.empty();
  }

  void *VulkanRenderBackend::GetActiveCommandBufferHandle() const
  {
    if (!m_context || m_context->commandBuffers.empty() ||
        m_context->activeImageIndex >= m_context->commandBuffers.size())
    {
      return nullptr;
    }

    return reinterpret_cast<void *>(m_context->commandBuffers[m_context->activeImageIndex]);
  }

  void VulkanRenderBackend::QueueOverlayRenderCallback(OverlayRenderCallback callback,
                                                       void *userData)
  {
    if (!m_frameActive)
    {
      m_pendingOverlayRenderCallback = nullptr;
      m_pendingOverlayRenderUserData = nullptr;
      m_lastError =
          "Vulkan overlay render callbacks must be queued during an active frame.";
      return;
    }

    if (!callback || !userData)
    {
      m_pendingOverlayRenderCallback = nullptr;
      m_pendingOverlayRenderUserData = nullptr;
      return;
    }

    m_pendingOverlayRenderCallback = callback;
    m_pendingOverlayRenderUserData = userData;
  }

  bool VulkanRenderBackend::ReadbackColorBgr8(int width,
                                              int height,
                                              std::vector<uint8_t> &outPixels,
                                              std::string *outError)
  {
    if (!m_context || !m_context->supportsColorReadback || m_context->colorReadbackBuffer == VK_NULL_HANDLE)
    {
      if (outError)
        *outError =
            "Vulkan color readback is unavailable (swapchain transfer-source usage not supported).";
      return false;
    }
    if (m_frameActive)
    {
      if (outError)
        *outError = "Vulkan color readback must be requested outside active BeginFrame/EndFrame scope.";
      return false;
    }
    if (width <= 0 || height <= 0)
    {
      if (outError)
        *outError = "Vulkan color readback requires positive dimensions.";
      return false;
    }
    if (static_cast<uint32_t>(width) != m_context->swapchainExtent.width ||
        static_cast<uint32_t>(height) != m_context->swapchainExtent.height)
    {
      if (outError)
      {
        *outError = "Vulkan color readback dimensions must match swapchain extent (" +
                    std::to_string(m_context->swapchainExtent.width) + "x" +
                    std::to_string(m_context->swapchainExtent.height) + ").";
      }
      return false;
    }
    if (!m_context->hasColorReadbackData)
    {
      if (outError)
        *outError =
            "Vulkan color readback buffer has no captured frame yet. Render at least one frame first.";
      return false;
    }

    if (!CheckVk(vkWaitForFences(m_context->device, 1, &m_context->inFlightFence, VK_TRUE, UINT64_MAX),
                 m_lastError,
                 "vkWaitForFences(colorReadback)"))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    const size_t pixelCount =
        static_cast<size_t>(m_context->swapchainExtent.width) * m_context->swapchainExtent.height;
    void *mapped = nullptr;
    if (!CheckVk(vkMapMemory(m_context->device,
                             m_context->colorReadbackMemory,
                             0,
                             m_context->colorReadbackSize,
                             0,
                             &mapped),
                 m_lastError,
                 "vkMapMemory(colorReadback)"))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    outPixels.resize(pixelCount * 3u);
    const auto *src = static_cast<const uint8_t *>(mapped);
    const bool formatIsBgra = m_context->swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                              m_context->swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;
    for (uint32_t y = 0; y < m_context->swapchainExtent.height; ++y)
    {
      const uint32_t srcY = m_context->swapchainExtent.height - 1u - y;
      for (uint32_t x = 0; x < m_context->swapchainExtent.width; ++x)
      {
        const size_t srcIndex = (static_cast<size_t>(srcY) * m_context->swapchainExtent.width + x) * 4u;
        const size_t dstIndex = (static_cast<size_t>(y) * m_context->swapchainExtent.width + x) * 3u;
        if (formatIsBgra)
        {
          outPixels[dstIndex + 0] = src[srcIndex + 0];
          outPixels[dstIndex + 1] = src[srcIndex + 1];
          outPixels[dstIndex + 2] = src[srcIndex + 2];
        }
        else
        {
          outPixels[dstIndex + 0] = src[srcIndex + 2];
          outPixels[dstIndex + 1] = src[srcIndex + 1];
          outPixels[dstIndex + 2] = src[srcIndex + 0];
        }
      }
    }

    vkUnmapMemory(m_context->device, m_context->colorReadbackMemory);
    return true;
  }

  bool VulkanRenderBackend::ReadbackDepth32F(int width,
                                             int height,
                                             std::vector<float> &outDepth,
                                             std::string *outError)
  {
    if (!m_context || !m_context->supportsDepthReadback || m_context->depthReadbackBuffer == VK_NULL_HANDLE)
    {
      if (outError)
      {
        *outError =
            "Vulkan depth readback is unavailable (depth transfer-source format support not available).";
      }
      return false;
    }
    if (m_frameActive)
    {
      if (outError)
        *outError = "Vulkan depth readback must be requested outside active BeginFrame/EndFrame scope.";
      return false;
    }
    if (width <= 0 || height <= 0)
    {
      if (outError)
        *outError = "Vulkan depth readback requires positive dimensions.";
      return false;
    }
    if (static_cast<uint32_t>(width) != m_context->swapchainExtent.width ||
        static_cast<uint32_t>(height) != m_context->swapchainExtent.height)
    {
      if (outError)
      {
        *outError = "Vulkan depth readback dimensions must match swapchain extent (" +
                    std::to_string(m_context->swapchainExtent.width) + "x" +
                    std::to_string(m_context->swapchainExtent.height) + ").";
      }
      return false;
    }
    if (!m_context->hasDepthReadbackData)
    {
      if (outError)
        *outError =
            "Vulkan depth readback buffer has no captured frame yet. Render at least one frame first.";
      return false;
    }

    if (!CheckVk(vkWaitForFences(m_context->device, 1, &m_context->inFlightFence, VK_TRUE, UINT64_MAX),
                 m_lastError,
                 "vkWaitForFences(depthReadback)"))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    const size_t pixelCount =
        static_cast<size_t>(m_context->swapchainExtent.width) * m_context->swapchainExtent.height;
    void *mapped = nullptr;
    if (!CheckVk(vkMapMemory(m_context->device,
                             m_context->depthReadbackMemory,
                             0,
                             m_context->depthReadbackSize,
                             0,
                             &mapped),
                 m_lastError,
                 "vkMapMemory(depthReadback)"))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    outDepth.resize(pixelCount);
    const auto *src = static_cast<const float *>(mapped);
    for (uint32_t y = 0; y < m_context->swapchainExtent.height; ++y)
    {
      const uint32_t srcY = m_context->swapchainExtent.height - 1u - y;
      for (uint32_t x = 0; x < m_context->swapchainExtent.width; ++x)
      {
        const size_t srcIndex = static_cast<size_t>(srcY) * m_context->swapchainExtent.width + x;
        const size_t dstIndex = static_cast<size_t>(y) * m_context->swapchainExtent.width + x;
        outDepth[dstIndex] = src[srcIndex];
      }
    }

    vkUnmapMemory(m_context->device, m_context->depthReadbackMemory);
    return true;
  }

  bool VulkanRenderBackend::EnsureEditorViewportRenderTarget(uint32_t width,
                                                             uint32_t height,
                                                             std::string *outError)
  {
    const bool ok = EnsureOffscreenRenderTarget(kEditorViewportTargetKey, width, height);
    if (!ok && outError)
      *outError = m_lastError;
    return ok;
  }

  bool VulkanRenderBackend::TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                                                   bool needsYFlip,
                                                                   std::string *outError)
  {
    const bool ok = TryGetOffscreenRenderTargetHandle(kEditorViewportTargetKey, outHandle, needsYFlip);
    if (!ok && outError)
      *outError = m_lastError;
    return ok;
  }

  bool VulkanRenderBackend::EnsureSceneTextureResources(uint32_t width,
                                                        uint32_t height,
                                                        std::string *outError)
  {
    m_sceneTextureCatalog = {};
    if (width == 0 || height == 0)
    {
      m_lastError = "Scene texture abstraction dimensions must be non-zero.";
      if (outError)
        *outError = m_lastError;
      return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(SceneTextureSemantic::Count); ++i)
    {
      const auto semantic = static_cast<SceneTextureSemantic>(i);
      if (!EnsureOffscreenRenderTarget(SceneTextureTargetKey(semantic), width, height))
      {
        if (outError)
          *outError = m_lastError;
        return false;
      }

      BackendResourceHandle handle{};
      if (!BuildOffscreenResourceHandle(SceneTextureTargetKey(semantic), &handle))
      {
        if (outError)
          *outError = m_lastError;
        return false;
      }
      m_sceneTextureCatalog.Set(semantic, handle);
    }

    if (!EnsureOffscreenRenderTarget(kMeshDistanceFieldTargetKey, width, height))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }
    if (!EnsureOffscreenRenderTarget(kGlobalDistanceFieldTargetKey, width, height))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    BackendResourceHandle meshDistanceFieldHandle{};
    if (!BuildOffscreenResourceHandle(kMeshDistanceFieldTargetKey, &meshDistanceFieldHandle))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    BackendResourceHandle globalDistanceFieldHandle{};
    if (!BuildOffscreenResourceHandle(kGlobalDistanceFieldTargetKey, &globalDistanceFieldHandle))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    const uint64_t previousRadianceGeneration = m_cachedHitLightingRadianceHandle.generation;
    if (!EnsureOffscreenRenderTarget(kCachedHitLightingRadianceTargetKey, width, height))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }
    if (!EnsureOffscreenRenderTarget(kCachedHitLightingMomentsTargetKey, width, height))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    if (!BuildOffscreenResourceHandle(kCachedHitLightingRadianceTargetKey, &m_cachedHitLightingRadianceHandle))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }
    if (!BuildOffscreenResourceHandle(kCachedHitLightingMomentsTargetKey, &m_cachedHitLightingMomentsHandle))
    {
      if (outError)
        *outError = m_lastError;
      return false;
    }

    m_meshDistanceFieldTracingStructure.atlas = meshDistanceFieldHandle;
    m_meshDistanceFieldTracingStructure.meshCount = 0;
    m_meshDistanceFieldTracingStructure.instanceCount = 0;
    m_meshDistanceFieldTracingStructure.pageCount = std::max(1u, (width * height) / 256u);
    m_meshDistanceFieldTracingStructure.revision = meshDistanceFieldHandle.generation;

    m_globalDistanceFieldTracingStructure.volume = globalDistanceFieldHandle;
    m_globalDistanceFieldTracingStructure.clipmapCount = 4;
    m_globalDistanceFieldTracingStructure.voxelResolution = std::min(width, height);
    m_globalDistanceFieldTracingStructure.worldExtent = 512.0f;
    m_globalDistanceFieldTracingStructure.revision = globalDistanceFieldHandle.generation;
    m_cachedHitLightingMaxSurfaceCount = std::max(1u, (width * height) / 4u);
    m_cachedHitLightingResidentSurfaceCount = std::min(
        m_cachedHitLightingResidentSurfaceCount,
        m_cachedHitLightingMaxSurfaceCount);
    if (previousRadianceGeneration != 0 &&
        previousRadianceGeneration != m_cachedHitLightingRadianceHandle.generation)
    {
      m_cachedHitLightingInvalidationReason = CachedHitLightingInvalidationReason::ViewportResize;
      m_cachedHitLightingResidentSurfaceCount = 0;
      ++m_cachedHitLightingRevision;
    }

    ++m_sceneTextureCatalog.frameSerial;
    m_lastSceneTracingRepresentationContract = {};
    m_hasSceneTracingRepresentationContract = false;
    m_lastCachedHitLightingRepresentationContract = {};
    m_hasCachedHitLightingRepresentationContract = false;
    return true;
  }

  bool VulkanRenderBackend::TryGetSceneTextureCatalog(SceneTextureCatalog *outCatalog,
                                                      std::string *outError) const
  {
    if (!outCatalog)
      return false;
    if (!m_sceneTextureCatalog.HasDeferredGBuffer())
    {
      *outCatalog = {};
      if (outError)
        *outError = "Scene texture abstraction catalog has not been initialized.";
      return false;
    }

    *outCatalog = m_sceneTextureCatalog;
    return true;
  }

  bool VulkanRenderBackend::EnsureGiHistoryResources(uint32_t width,
                                                     uint32_t height,
                                                     std::string *outError)
  {
    if (width == 0 || height == 0)
    {
      m_lastError = "GI history abstraction dimensions must be non-zero.";
      if (outError)
        *outError = m_lastError;
      return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(GiHistorySemantic::Count); ++i)
    {
      const auto semantic = static_cast<GiHistorySemantic>(i);
      if (!EnsureOffscreenRenderTarget(GiHistoryTargetKey(semantic), width, height))
      {
        if (outError)
          *outError = m_lastError;
        return false;
      }

      BackendResourceHandle handle{};
      if (!BuildOffscreenResourceHandle(GiHistoryTargetKey(semantic), &handle))
      {
        if (outError)
          *outError = m_lastError;
        return false;
      }
      m_giHistoryCatalog.Set(semantic, handle);
    }

    ++m_giHistoryCatalog.revision;
    m_giHistoryCatalog.lastResetReason = GiHistoryResetReason::None;
    m_giHistoryCatalog.ownerState = m_lastTemporalHistoryState;
    m_giHistoryCatalog.validForTemporalReuse = false;
    return true;
  }

  bool VulkanRenderBackend::TryGetGiHistoryCatalog(GiHistoryCatalog *outCatalog,
                                                   std::string *outError) const
  {
    if (!outCatalog)
      return false;
    if (!m_giHistoryCatalog.Has(GiHistorySemantic::DiffuseIrradiance))
    {
      *outCatalog = {};
      if (outError)
        *outError = "GI history abstraction catalog has not been initialized.";
      return false;
    }

    *outCatalog = m_giHistoryCatalog;
    return true;
  }

  bool VulkanRenderBackend::TryGetScreenSpaceReflectionPassContract(
      ScreenSpaceReflectionPassContract *outContract, std::string *outError) const
  {
    if (!outContract)
      return false;

    if (!m_hasSsrPassContract)
    {
      *outContract = {};
      if (outError)
        *outError = "Screen-space reflection pass contract has not been produced for this frame.";
      return false;
    }

    *outContract = m_lastSsrPassContract;
    return true;
  }

  bool VulkanRenderBackend::TryGetScreenSpaceGlobalIlluminationPassContract(
      ScreenSpaceGlobalIlluminationPassContract *outContract,
      std::string *outError) const
  {
    if (!outContract)
      return false;

    if (!m_hasSsgiPassContract)
    {
      *outContract = {};
      if (outError)
        *outError = "Screen-space global illumination pass contract has not been produced for this frame.";
      return false;
    }

    *outContract = m_lastSsgiPassContract;
    return true;
  }

  bool VulkanRenderBackend::TryGetTemporalGiResolvePassContract(
      TemporalGiResolvePassContract *outContract,
      std::string *outError) const
  {
    if (!outContract)
      return false;

    if (!m_hasTemporalGiResolvePassContract)
    {
      *outContract = {};
      if (outError)
        *outError = "Temporal GI resolve pass contract has not been produced for this frame.";
      return false;
    }

    *outContract = m_lastTemporalGiResolvePassContract;
    return true;
  }

  bool VulkanRenderBackend::TryGetLightingCompositePassContract(
      LightingCompositePassContract *outContract,
      std::string *outError) const
  {
    if (!outContract)
      return false;

    if (!m_hasLightingCompositePassContract)
    {
      *outContract = {};
      if (outError)
        *outError = "Lighting composite pass contract has not been produced for this frame.";
      return false;
    }

    *outContract = m_lastLightingCompositePassContract;
    return true;
  }

  bool VulkanRenderBackend::TryGetSceneTracingRepresentationContract(
      SceneTracingRepresentationContract *outContract,
      std::string *outError) const
  {
    if (!outContract)
      return false;

    if (!m_hasSceneTracingRepresentationContract)
    {
      *outContract = {};
      if (outError)
        *outError = "Scene tracing representation contract has not been produced for this frame.";
      return false;
    }

    *outContract = m_lastSceneTracingRepresentationContract;
    return true;
  }

  bool VulkanRenderBackend::TryGetCachedHitLightingRepresentationContract(
      CachedHitLightingRepresentationContract *outContract,
      std::string *outError) const
  {
    if (!outContract)
      return false;

    if (!m_hasCachedHitLightingRepresentationContract)
    {
      *outContract = {};
      if (outError)
        *outError = "Cached hit-lighting representation contract has not been produced for this frame.";
      return false;
    }

    *outContract = m_lastCachedHitLightingRepresentationContract;
    return true;
  }

  bool VulkanRenderBackend::InvalidateGiHistory(GiHistoryResetReason reason,
                                                std::string *outError)
  {
    if (!m_giHistoryCatalog.Has(GiHistorySemantic::DiffuseIrradiance))
    {
      m_lastError = "GI history abstraction invalidation requires initialized history resources.";
      if (outError)
        *outError = m_lastError;
      return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(GiHistorySemantic::Count); ++i)
    {
      const auto semantic = static_cast<GiHistorySemantic>(i);
      const char *targetKey = GiHistoryTargetKey(semantic);
      OffscreenTargetMetadata metadata{};
      if (!TryGetOffscreenRenderTargetMetadata(targetKey, &metadata))
      {
        m_lastError = "GI history abstraction invalidation encountered a missing history surface.";
        if (outError)
          *outError = m_lastError;
        return false;
      }

      DestroyOffscreenRenderTargetResources(targetKey);
      if (!CreateOffscreenRenderTargetResources(targetKey, metadata.width, metadata.height, metadata.generation))
      {
        if (outError)
          *outError = m_lastError;
        return false;
      }

      BackendResourceHandle handle{};
      if (!BuildOffscreenResourceHandle(targetKey, &handle))
      {
        if (outError)
          *outError = m_lastError;
        return false;
      }
      m_giHistoryCatalog.Set(semantic, handle);
    }

    ++m_giHistoryCatalog.revision;
    m_giHistoryCatalog.lastResetReason = reason;
    m_giHistoryCatalog.ownerState = m_lastTemporalHistoryState;
    m_giHistoryCatalog.validForTemporalReuse = false;
    m_cachedHitLightingInvalidationReason = ToCachedHitLightingInvalidationReason(reason);
    m_cachedHitLightingResidentSurfaceCount = 0;
    ++m_cachedHitLightingRevision;
    return true;
  }

  void VulkanRenderBackend::ExecuteScreenSpaceReflectionPass()
  {
    const TemporalQualityTier qualityTier = m_activeFrame.temporal.jitter.qualityTier;
    const bool ssrEnabled = qualityTier != TemporalQualityTier::Disabled;
    const ScreenSpaceReflectionMissPolicy missPolicy = m_activeFrame.temporal.enableTemporalReprojection
                                                           ? ScreenSpaceReflectionMissPolicy::ProbeFallback
                                                           : ScreenSpaceReflectionMissPolicy::SkyFallback;

    m_lastSsrPassContract = BuildScreenSpaceReflectionPassContract(
        m_sceneTextureCatalog,
        m_giHistoryCatalog,
        qualityTier,
        ssrEnabled,
        missPolicy);
    m_hasSsrPassContract = true;
  }

  void VulkanRenderBackend::ExecuteScreenSpaceGlobalIlluminationPass()
  {
    const TemporalQualityTier qualityTier = m_activeFrame.temporal.jitter.qualityTier;
    const bool ssgiEnabled = qualityTier != TemporalQualityTier::Disabled;
    m_lastSsgiPassContract = BuildScreenSpaceGlobalIlluminationPassContract(
        m_sceneTextureCatalog,
        m_giHistoryCatalog,
        qualityTier,
        ssgiEnabled);
    m_hasSsgiPassContract = true;
  }

  void VulkanRenderBackend::ExecuteTemporalGiResolvePass()
  {
    m_lastTemporalGiResolvePassContract = BuildTemporalGiResolvePassContract(
        m_lastSsrPassContract,
        m_lastSsgiPassContract,
        m_giHistoryCatalog);
    m_hasTemporalGiResolvePassContract = true;
  }

  void VulkanRenderBackend::ExecuteLightingCompositePass()
  {
    m_lastLightingCompositePassContract = BuildLightingCompositePassContract(
        m_sceneTextureCatalog,
        m_lastTemporalGiResolvePassContract);
    m_hasLightingCompositePassContract = true;

    m_meshDistanceFieldTracingStructure.meshCount =
        static_cast<uint32_t>(m_context ? m_context->opaqueMeshGpuBuffers.size() : 0u);
    m_meshDistanceFieldTracingStructure.instanceCount =
        static_cast<uint32_t>(m_pendingOpaqueDraws.size());
    m_meshDistanceFieldTracingStructure.revision = std::max(
        m_meshDistanceFieldTracingStructure.revision,
        m_meshDistanceFieldTracingStructure.atlas.generation);
    m_globalDistanceFieldTracingStructure.revision = std::max(
        m_globalDistanceFieldTracingStructure.revision,
        m_globalDistanceFieldTracingStructure.volume.generation);

    const bool tracingEnabled = m_activeFrame.temporal.jitter.qualityTier != TemporalQualityTier::Disabled;
    m_lastSceneTracingRepresentationContract = BuildSceneTracingRepresentationContract(
        m_lastSsrPassContract,
        m_lastSsgiPassContract,
        m_lastTemporalGiResolvePassContract,
        m_meshDistanceFieldTracingStructure,
        m_globalDistanceFieldTracingStructure,
        SceneTracingRepresentationOwnership::BackendOwnedPersistent,
        SceneTracingRepresentationUpdatePolicy::PerFrameAndSceneBarrier,
        tracingEnabled);
    m_hasSceneTracingRepresentationContract = true;

    const bool hitLightingEnabled = m_activeFrame.temporal.jitter.qualityTier >= TemporalQualityTier::Medium;
    CachedHitLightingCapturePolicy capturePolicy = CachedHitLightingCapturePolicy::Disabled;
    CachedHitLightingUpdatePolicy updatePolicy = CachedHitLightingUpdatePolicy::Static;
    if (hitLightingEnabled)
    {
      capturePolicy = m_cachedHitLightingInvalidationReason != CachedHitLightingInvalidationReason::None
                          ? CachedHitLightingCapturePolicy::FullReseedOnInvalidation
                          : CachedHitLightingCapturePolicy::ScreenSpaceMissesAndDisocclusions;
      updatePolicy = CachedHitLightingUpdatePolicy::PerFrameBudgeted;
    }

    if (hitLightingEnabled &&
        m_lastSceneTracingRepresentationContract.IsValidForOffscreenQueries() &&
        m_cachedHitLightingMaxSurfaceCount > 0u)
    {
      const uint32_t budget = std::max(1u, m_cachedHitLightingMaxSurfaceCount / 16u);
      m_cachedHitLightingResidentSurfaceCount = std::min(
          m_cachedHitLightingMaxSurfaceCount,
          m_cachedHitLightingResidentSurfaceCount + budget);
    }

    m_lastCachedHitLightingRepresentationContract = BuildCachedHitLightingRepresentationContract(
        m_lastSceneTracingRepresentationContract,
        m_lastLightingCompositePassContract,
        m_cachedHitLightingRadianceHandle,
        m_cachedHitLightingMomentsHandle,
        m_cachedHitLightingRevision,
        m_cachedHitLightingMaxSurfaceCount,
        m_cachedHitLightingResidentSurfaceCount,
        capturePolicy,
        updatePolicy,
        m_cachedHitLightingInvalidationReason,
        hitLightingEnabled);
    m_hasCachedHitLightingRepresentationContract = true;
    if (m_lastCachedHitLightingRepresentationContract.validationStatus ==
            CachedHitLightingRepresentationValidationStatus::Valid &&
        m_cachedHitLightingInvalidationReason != CachedHitLightingInvalidationReason::None)
    {
      m_cachedHitLightingInvalidationReason = CachedHitLightingInvalidationReason::None;
    }
  }

  bool VulkanRenderBackend::EnsureOffscreenRenderTarget(const std::string &targetKey,
                                                        uint32_t width,
                                                        uint32_t height)
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE || m_context->graphicsQueue == VK_NULL_HANDLE)
    {
      m_lastError = "Cannot create Vulkan offscreen target before backend initialization.";
      return false;
    }
    if (targetKey.empty())
    {
      m_lastError = "Vulkan offscreen target key cannot be empty.";
      return false;
    }
    if (width == 0 || height == 0)
    {
      m_lastError = "Vulkan offscreen target dimensions must be non-zero.";
      return false;
    }

    auto it = m_context->offscreenTargets.find(targetKey);
    if (it != m_context->offscreenTargets.end() &&
        it->second.width == width &&
        it->second.height == height &&
        it->second.readyForSampling)
    {
      return true;
    }

    uint64_t previousGeneration = 0;
    if (it != m_context->offscreenTargets.end())
      previousGeneration = it->second.generation;

    DestroyOffscreenRenderTargetResources(targetKey);
    return CreateOffscreenRenderTargetResources(targetKey, width, height, previousGeneration);
  }

  bool VulkanRenderBackend::TryGetOffscreenRenderTargetHandle(const std::string &targetKey,
                                                              RenderTargetHandle *outHandle,
                                                              bool needsYFlip)
  {
    if (!outHandle)
      return false;
    *outHandle = {};
    if (!m_context)
    {
      m_lastError = "Vulkan offscreen target handle request requires an initialized backend.";
      return false;
    }

    const auto it = m_context->offscreenTargets.find(targetKey);
    if (it == m_context->offscreenTargets.end())
    {
      m_lastError = "Vulkan offscreen target handle request references an unknown target key: " + targetKey;
      return false;
    }

    return TryRegisterOffscreenTargetImGuiDescriptor(targetKey, outHandle, needsYFlip);
  }

  bool VulkanRenderBackend::TryGetOffscreenRenderTargetMetadata(const std::string &targetKey,
                                                                OffscreenTargetMetadata *outMetadata) const
  {
    if (!outMetadata || !m_context)
      return false;

    const auto it = m_context->offscreenTargets.find(targetKey);
    if (it == m_context->offscreenTargets.end())
      return false;

    const Context::OffscreenRenderTarget &target = it->second;
    outMetadata->width = target.width;
    outMetadata->height = target.height;
    outMetadata->generation = target.generation;
    outMetadata->readyForSampling = target.readyForSampling;
    outMetadata->hasImGuiDescriptor = target.imguiDescriptorSet != VK_NULL_HANDLE;
    return true;
  }

  void VulkanRenderBackend::DestroyOffscreenRenderTarget(const std::string &targetKey)
  {
    DestroyOffscreenRenderTargetResources(targetKey);
  }

  bool VulkanRenderBackend::BuildOffscreenResourceHandle(const std::string &targetKey,
                                                         BackendResourceHandle *outHandle) const
  {
    if (!m_context || !outHandle)
      return false;

    const auto it = m_context->offscreenTargets.find(targetKey);
    if (it == m_context->offscreenTargets.end())
      return false;

    const Context::OffscreenRenderTarget &target = it->second;
    *outHandle = {
        RenderBackendId::Vulkan,
        HashResourceKey(targetKey),
        target.width,
        target.height,
        target.generation};
    return outHandle->IsValid();
  }

  void VulkanRenderBackend::DestroyAllOffscreenRenderTargets()
  {
    if (!m_context)
      return;

    std::vector<std::string> keys;
    keys.reserve(m_context->offscreenTargets.size());
    for (const auto &entry : m_context->offscreenTargets)
      keys.push_back(entry.first);

    for (const std::string &key : keys)
      DestroyOffscreenRenderTargetResources(key);
  }

  bool VulkanRenderBackend::EnsureOffscreenRenderPass()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return false;
    if (m_context->offscreenRenderPass != VK_NULL_HANDLE)
      return true;

    const VkAttachmentDescription colorAttachment{
        .flags = 0,
        .format = kOffscreenTargetFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkAttachmentReference colorAttachmentRef{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };
    const std::array<VkSubpassDependency, 2> dependencies = {
        VkSubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0,
        },
        VkSubpassDependency{
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dependencyFlags = 0,
        }};

    const VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<uint32_t>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };
    return CheckVk(vkCreateRenderPass(m_context->device,
                                      &renderPassInfo,
                                      nullptr,
                                      &m_context->offscreenRenderPass),
                   m_lastError,
                   "vkCreateRenderPass(offscreen)");
  }

  void VulkanRenderBackend::DestroyOffscreenRenderPass()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;
    if (m_context->offscreenRenderPass != VK_NULL_HANDLE)
    {
      vkDestroyRenderPass(m_context->device, m_context->offscreenRenderPass, nullptr);
      m_context->offscreenRenderPass = VK_NULL_HANDLE;
    }
  }

  bool VulkanRenderBackend::CreateOffscreenRenderTargetResources(const std::string &targetKey,
                                                                 uint32_t width,
                                                                 uint32_t height,
                                                                 uint64_t previousGeneration)
  {
    if (!EnsureOffscreenRenderPass())
      return false;

    Context::OffscreenRenderTarget target;
    target.width = width;
    target.height = height;
    target.generation = previousGeneration + 1;

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = kOffscreenTargetFormat,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!CheckVk(vkCreateImage(m_context->device, &imageInfo, nullptr, &target.image),
                 m_lastError,
                 "vkCreateImage(offscreen)"))
    {
      return false;
    }

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(m_context->device, target.image, &imageRequirements);
    const uint32_t memoryTypeIndex =
        FindMemoryTypeIndex(m_context->physicalDevice,
                            imageRequirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryTypeIndex == kInvalidQueueFamily)
    {
      m_lastError = "No Vulkan device-local memory type available for offscreen render target.";
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    const VkMemoryAllocateInfo memoryInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = imageRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };
    if (!CheckVk(vkAllocateMemory(m_context->device, &memoryInfo, nullptr, &target.memory),
                 m_lastError,
                 "vkAllocateMemory(offscreen)"))
    {
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }
    if (!CheckVk(vkBindImageMemory(m_context->device, target.image, target.memory, 0),
                 m_lastError,
                 "vkBindImageMemory(offscreen)"))
    {
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    const VkImageViewCreateInfo imageViewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = target.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = kOffscreenTargetFormat,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    if (!CheckVk(vkCreateImageView(m_context->device, &imageViewInfo, nullptr, &target.imageView),
                 m_lastError,
                 "vkCreateImageView(offscreen)"))
    {
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    if (!CheckVk(vkCreateSampler(m_context->device, &samplerInfo, nullptr, &target.sampler),
                 m_lastError,
                 "vkCreateSampler(offscreen)"))
    {
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    const VkImageView framebufferAttachment = target.imageView;
    const VkFramebufferCreateInfo framebufferInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = m_context->offscreenRenderPass,
        .attachmentCount = 1,
        .pAttachments = &framebufferAttachment,
        .width = width,
        .height = height,
        .layers = 1,
    };
    if (!CheckVk(vkCreateFramebuffer(m_context->device, &framebufferInfo, nullptr, &target.framebuffer),
                 m_lastError,
                 "vkCreateFramebuffer(offscreen)"))
    {
      vkDestroySampler(m_context->device, target.sampler, nullptr);
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    const VkCommandBufferAllocateInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = m_context->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (!CheckVk(vkAllocateCommandBuffers(m_context->device, &commandBufferInfo, &commandBuffer),
                 m_lastError,
                 "vkAllocateCommandBuffers(offscreen)"))
    {
      vkDestroyFramebuffer(m_context->device, target.framebuffer, nullptr);
      vkDestroySampler(m_context->device, target.sampler, nullptr);
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    if (!CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                 m_lastError,
                 "vkBeginCommandBuffer(offscreen)"))
    {
      vkFreeCommandBuffers(m_context->device, m_context->commandPool, 1, &commandBuffer);
      vkDestroyFramebuffer(m_context->device, target.framebuffer, nullptr);
      vkDestroySampler(m_context->device, target.sampler, nullptr);
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    const VkClearValue clearValue{
        .color = {{0.14f, 0.14f, 0.14f, 1.0f}},
    };
    const VkRenderPassBeginInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = m_context->offscreenRenderPass,
        .framebuffer = target.framebuffer,
        .renderArea = {{0, 0}, {width, height}},
        .clearValueCount = 1,
        .pClearValues = &clearValue,
    };
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(commandBuffer);

    if (!CheckVk(vkEndCommandBuffer(commandBuffer), m_lastError, "vkEndCommandBuffer(offscreen)"))
    {
      vkFreeCommandBuffers(m_context->device, m_context->commandPool, 1, &commandBuffer);
      vkDestroyFramebuffer(m_context->device, target.framebuffer, nullptr);
      vkDestroySampler(m_context->device, target.sampler, nullptr);
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    VkFence fence = VK_NULL_HANDLE;
    const VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    if (!CheckVk(vkCreateFence(m_context->device, &fenceInfo, nullptr, &fence),
                 m_lastError,
                 "vkCreateFence(offscreen)"))
    {
      vkFreeCommandBuffers(m_context->device, m_context->commandPool, 1, &commandBuffer);
      vkDestroyFramebuffer(m_context->device, target.framebuffer, nullptr);
      vkDestroySampler(m_context->device, target.sampler, nullptr);
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }

    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    if (!CheckVk(vkQueueSubmit(m_context->graphicsQueue, 1, &submitInfo, fence),
                 m_lastError,
                 "vkQueueSubmit(offscreen)"))
    {
      vkDestroyFence(m_context->device, fence, nullptr);
      vkFreeCommandBuffers(m_context->device, m_context->commandPool, 1, &commandBuffer);
      vkDestroyFramebuffer(m_context->device, target.framebuffer, nullptr);
      vkDestroySampler(m_context->device, target.sampler, nullptr);
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }
    if (!CheckVk(vkWaitForFences(m_context->device, 1, &fence, VK_TRUE, UINT64_MAX),
                 m_lastError,
                 "vkWaitForFences(offscreen)"))
    {
      vkDestroyFence(m_context->device, fence, nullptr);
      vkFreeCommandBuffers(m_context->device, m_context->commandPool, 1, &commandBuffer);
      vkDestroyFramebuffer(m_context->device, target.framebuffer, nullptr);
      vkDestroySampler(m_context->device, target.sampler, nullptr);
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
      vkFreeMemory(m_context->device, target.memory, nullptr);
      vkDestroyImage(m_context->device, target.image, nullptr);
      return false;
    }
    vkDestroyFence(m_context->device, fence, nullptr);
    vkFreeCommandBuffers(m_context->device, m_context->commandPool, 1, &commandBuffer);

    target.readyForSampling = true;
    target.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_context->offscreenTargets[targetKey] = target;
    return true;
  }

  void VulkanRenderBackend::DestroyOffscreenRenderTargetResources(const std::string &targetKey)
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    const auto it = m_context->offscreenTargets.find(targetKey);
    if (it == m_context->offscreenTargets.end())
      return;

    Context::OffscreenRenderTarget &target = it->second;

    if (target.imguiDescriptorSet != VK_NULL_HANDLE && IsImGuiVulkanTextureApiReady())
    {
      ImGui_ImplVulkan_RemoveTexture(target.imguiDescriptorSet);
      target.imguiDescriptorSet = VK_NULL_HANDLE;
    }

    if (target.framebuffer != VK_NULL_HANDLE)
      vkDestroyFramebuffer(m_context->device, target.framebuffer, nullptr);
    if (target.sampler != VK_NULL_HANDLE)
      vkDestroySampler(m_context->device, target.sampler, nullptr);
    if (target.imageView != VK_NULL_HANDLE)
      vkDestroyImageView(m_context->device, target.imageView, nullptr);
    if (target.image != VK_NULL_HANDLE)
      vkDestroyImage(m_context->device, target.image, nullptr);
    if (target.memory != VK_NULL_HANDLE)
      vkFreeMemory(m_context->device, target.memory, nullptr);

    m_context->offscreenTargets.erase(it);
  }

  bool VulkanRenderBackend::TryRegisterOffscreenTargetImGuiDescriptor(const std::string &targetKey,
                                                                      RenderTargetHandle *outHandle,
                                                                      bool needsYFlip)
  {
    if (!m_context || !outHandle)
      return false;

    const auto it = m_context->offscreenTargets.find(targetKey);
    if (it == m_context->offscreenTargets.end())
      return false;

    Context::OffscreenRenderTarget &target = it->second;
    if (!target.readyForSampling || target.imageView == VK_NULL_HANDLE || target.sampler == VK_NULL_HANDLE)
    {
      m_lastError = "Vulkan offscreen target is not ready for sampling: " + targetKey;
      return false;
    }

    if (target.imguiDescriptorSet == VK_NULL_HANDLE)
    {
      if (!IsImGuiVulkanTextureApiReady())
      {
        m_lastError = "ImGui Vulkan texture registration is not ready for offscreen target: " + targetKey;
        return false;
      }
      target.imguiDescriptorSet =
          ImGui_ImplVulkan_AddTexture(target.sampler, target.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      if (target.imguiDescriptorSet == VK_NULL_HANDLE)
      {
        m_lastError = "Failed to register Vulkan offscreen target with ImGui texture API: " + targetKey;
        return false;
      }
    }

    *outHandle = RenderTargetHandle::VulkanDescriptorSet(reinterpret_cast<void *>(target.imguiDescriptorSet),
                                                         needsYFlip,
                                                         target.width,
                                                         target.height,
                                                         target.generation);
    return true;
  }

  bool VulkanRenderBackend::Initialize(void *nativeWindowHandle)
  {
    m_lastError.clear();
    if (!nativeWindowHandle)
    {
      m_lastError = "Vulkan backend requires a native GLFW window handle.";
      return false;
    }

    if (!glfwVulkanSupported())
    {
      m_lastError = "GLFW reports that Vulkan is not supported on this system.";
      return false;
    }

    if (!CheckVk(volkInitialize(), m_lastError, "volkInitialize"))
      return false;

    m_context = std::make_unique<Context>();
    m_context->window = static_cast<GLFWwindow *>(nativeWindowHandle);

    uint32_t extensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!glfwExtensions || extensionCount == 0)
    {
      m_lastError = "GLFW did not provide required Vulkan instance extensions.";
      Shutdown();
      return false;
    }

    const VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "MonolithEngine",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "MonolithEngine",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    std::vector<const char *> instanceExtensions(glfwExtensions, glfwExtensions + extensionCount);
    const VkInstanceCreateInfo instanceInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
        .ppEnabledExtensionNames = instanceExtensions.data(),
    };
    if (!CheckVk(vkCreateInstance(&instanceInfo, nullptr, &m_context->instance), m_lastError,
                 "vkCreateInstance"))
    {
      Shutdown();
      return false;
    }

    volkLoadInstance(m_context->instance);

    if (!CheckVk(glfwCreateWindowSurface(m_context->instance, m_context->window, nullptr,
                                         &m_context->surface),
                 m_lastError,
                 "glfwCreateWindowSurface"))
    {
      Shutdown();
      return false;
    }

    uint32_t physicalDeviceCount = 0;
    if (!CheckVk(vkEnumeratePhysicalDevices(m_context->instance, &physicalDeviceCount, nullptr),
                 m_lastError,
                 "vkEnumeratePhysicalDevices(count)"))
    {
      Shutdown();
      return false;
    }
    if (physicalDeviceCount == 0)
    {
      m_lastError = "No Vulkan physical devices were found.";
      Shutdown();
      return false;
    }

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    if (!CheckVk(vkEnumeratePhysicalDevices(m_context->instance, &physicalDeviceCount,
                                            physicalDevices.data()),
                 m_lastError,
                 "vkEnumeratePhysicalDevices"))
    {
      Shutdown();
      return false;
    }

    const std::array<const char *, 1> requiredDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    for (VkPhysicalDevice device : physicalDevices)
    {
      uint32_t queueFamilyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
      std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

      std::optional<uint32_t> graphicsFamily;
      std::optional<uint32_t> presentFamily;
      for (uint32_t i = 0; i < queueFamilyCount; ++i)
      {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
          graphicsFamily = i;

        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_context->surface, &presentSupported);
        if (presentSupported == VK_TRUE)
          presentFamily = i;
      }

      if (!graphicsFamily || !presentFamily)
        continue;

      uint32_t extensionCountAvailable = 0;
      vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCountAvailable, nullptr);
      std::vector<VkExtensionProperties> extensions(extensionCountAvailable);
      vkEnumerateDeviceExtensionProperties(device,
                                           nullptr,
                                           &extensionCountAvailable,
                                           extensions.data());
      std::set<std::string> missingExtensions(requiredDeviceExtensions.begin(),
                                              requiredDeviceExtensions.end());
      for (const auto &extension : extensions)
        missingExtensions.erase(extension.extensionName);
      if (!missingExtensions.empty())
        continue;

      uint32_t surfaceFormatCount = 0;
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_context->surface, &surfaceFormatCount, nullptr);
      uint32_t presentModeCount = 0;
      vkGetPhysicalDeviceSurfacePresentModesKHR(device,
                                                m_context->surface,
                                                &presentModeCount,
                                                nullptr);
      if (surfaceFormatCount == 0 || presentModeCount == 0)
        continue;

      m_context->physicalDevice = device;
      m_context->graphicsQueueFamily = *graphicsFamily;
      m_context->presentQueueFamily = *presentFamily;
      break;
    }

    if (m_context->physicalDevice == VK_NULL_HANDLE)
    {
      m_lastError = "No Vulkan physical device supports graphics, present, and swapchain requirements.";
      Shutdown();
      return false;
    }

    const float queuePriority = 1.0f;
    std::set<uint32_t> uniqueQueueFamilies = {m_context->graphicsQueueFamily,
                                              m_context->presentQueueFamily};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueQueueFamilies.size());
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
      queueInfos.push_back(VkDeviceQueueCreateInfo{
          .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .queueFamilyIndex = queueFamily,
          .queueCount = 1,
          .pQueuePriorities = &queuePriority,
      });
    }

    const VkDeviceCreateInfo deviceInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size()),
        .pQueueCreateInfos = queueInfos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
        .pEnabledFeatures = nullptr,
    };
    if (!CheckVk(vkCreateDevice(m_context->physicalDevice, &deviceInfo, nullptr, &m_context->device),
                 m_lastError,
                 "vkCreateDevice"))
    {
      Shutdown();
      return false;
    }

    volkLoadDevice(m_context->device);
    vkGetDeviceQueue(m_context->device, m_context->graphicsQueueFamily, 0, &m_context->graphicsQueue);
    vkGetDeviceQueue(m_context->device, m_context->presentQueueFamily, 0, &m_context->presentQueue);

    const VkCommandPoolCreateInfo commandPoolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_context->graphicsQueueFamily,
    };
    if (!CheckVk(vkCreateCommandPool(m_context->device, &commandPoolInfo, nullptr,
                                     &m_context->commandPool),
                 m_lastError,
                 "vkCreateCommandPool"))
    {
      Shutdown();
      return false;
    }

    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                              .pNext = nullptr,
                                              .flags = 0};
    if (!CheckVk(vkCreateSemaphore(m_context->device, &semaphoreInfo, nullptr,
                                   &m_context->imageAvailableSemaphore),
                 m_lastError,
                 "vkCreateSemaphore(imageAvailable)"))
    {
      Shutdown();
      return false;
    }
    if (!CheckVk(vkCreateSemaphore(m_context->device, &semaphoreInfo, nullptr,
                                   &m_context->renderFinishedSemaphore),
                 m_lastError,
                 "vkCreateSemaphore(renderFinished)"))
    {
      Shutdown();
      return false;
    }

    const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                      .pNext = nullptr,
                                      .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    if (!CheckVk(vkCreateFence(m_context->device, &fenceInfo, nullptr, &m_context->inFlightFence),
                 m_lastError,
                 "vkCreateFence"))
    {
      Shutdown();
      return false;
    }

    if (!CreateOpaqueMaterialBindingScaffold())
    {
      Shutdown();
      return false;
    }

    const VkDescriptorSetLayout setLayouts[] = {m_context->opaqueMaterialSetLayout};
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = setLayouts,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };
    if (!CheckVk(vkCreatePipelineLayout(m_context->device,
                                        &pipelineLayoutInfo,
                                        nullptr,
                                        &m_context->opaquePipelineLayout),
                 m_lastError,
                 "vkCreatePipelineLayout"))
    {
      Shutdown();
      return false;
    }

    if (!CreateOpaquePipelineCreationScaffold())
    {
      Shutdown();
      return false;
    }

    if (!CreateOpaqueDrawIndexBuffer())
    {
      Shutdown();
      return false;
    }

    if (!CreateOpaqueShaderPipelineScaffold())
    {
      Shutdown();
      return false;
    }

    if (!RecreateSwapchain())
    {
      Shutdown();
      return false;
    }

    return true;
  }

  void VulkanRenderBackend::Shutdown()
  {
    if (!m_context)
      return;

    if (m_context->device != VK_NULL_HANDLE)
      vkDeviceWaitIdle(m_context->device);

    DestroyAllOffscreenRenderTargets();
    DestroyOffscreenRenderPass();

    DestroyOpaqueGraphicsPipelineScaffold();
    DestroySwapchain();

    DestroyOpaqueShaderPipelineScaffold();
    DestroyOpaqueDrawIndexBuffer();
    DestroyOpaqueMeshGpuBuffers();
    DestroyOpaqueMaterialBindingScaffold();
    DestroyOpaquePipelineCreationScaffold();

    if (m_context->opaquePipelineLayout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(m_context->device, m_context->opaquePipelineLayout, nullptr);

    if (m_context->inFlightFence != VK_NULL_HANDLE)
      vkDestroyFence(m_context->device, m_context->inFlightFence, nullptr);
    if (m_context->renderFinishedSemaphore != VK_NULL_HANDLE)
      vkDestroySemaphore(m_context->device, m_context->renderFinishedSemaphore, nullptr);
    if (m_context->imageAvailableSemaphore != VK_NULL_HANDLE)
      vkDestroySemaphore(m_context->device, m_context->imageAvailableSemaphore, nullptr);
    if (m_context->commandPool != VK_NULL_HANDLE)
      vkDestroyCommandPool(m_context->device, m_context->commandPool, nullptr);
    if (m_context->device != VK_NULL_HANDLE)
      vkDestroyDevice(m_context->device, nullptr);
    if (m_context->surface != VK_NULL_HANDLE)
      vkDestroySurfaceKHR(m_context->instance, m_context->surface, nullptr);
    if (m_context->instance != VK_NULL_HANDLE)
      vkDestroyInstance(m_context->instance, nullptr);

    m_context.reset();
    m_frameActive = false;
    m_passActive = false;
    m_sceneTextureCatalog = {};
    m_giHistoryCatalog = {};
    m_lastTemporalHistoryState = {};
    m_hasTemporalHistoryState = false;
  }

  void VulkanRenderBackend::DestroySwapchain()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    DestroyOpaqueRasterScaffold();
    DestroyReadbackBuffers();
    DestroyDepthResources();

    if (!m_context->commandBuffers.empty())
    {
      vkFreeCommandBuffers(m_context->device,
                           m_context->commandPool,
                           static_cast<uint32_t>(m_context->commandBuffers.size()),
                           m_context->commandBuffers.data());
      m_context->commandBuffers.clear();
    }

    for (VkImageView imageView : m_context->swapchainImageViews)
      vkDestroyImageView(m_context->device, imageView, nullptr);
    m_context->swapchainImageViews.clear();
    m_context->swapchainImages.clear();
    m_context->imageWasPresented.clear();

    if (m_context->swapchain != VK_NULL_HANDLE)
    {
      vkDestroySwapchainKHR(m_context->device, m_context->swapchain, nullptr);
      m_context->swapchain = VK_NULL_HANDLE;
    }
  }

  bool VulkanRenderBackend::CreateDepthResources()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE || m_context->physicalDevice == VK_NULL_HANDLE)
      return false;

    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    if (!FindSupportedDepthFormat(m_context->physicalDevice, false, &depthFormat))
    {
      m_lastError = "Failed to find a Vulkan depth format with depth-stencil attachment support.";
      return false;
    }
    const bool depthTransferSrcSupported =
        HasFormatFeature(m_context->physicalDevice, depthFormat, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
    VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (depthTransferSrcSupported)
      depthUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    const VkImageCreateInfo depthImageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthFormat,
        .extent = {m_context->swapchainExtent.width, m_context->swapchainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = depthUsage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!CheckVk(vkCreateImage(m_context->device, &depthImageInfo, nullptr, &m_context->depthImage),
                 m_lastError,
                 "vkCreateImage(depth)"))
    {
      return false;
    }

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(m_context->device, m_context->depthImage, &memoryRequirements);
    const uint32_t memoryTypeIndex =
        FindMemoryTypeIndex(m_context->physicalDevice,
                            memoryRequirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryTypeIndex == kInvalidQueueFamily)
    {
      m_lastError = "Failed to find Vulkan device-local memory for depth image allocation.";
      return false;
    }

    const VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };
    if (!CheckVk(vkAllocateMemory(m_context->device, &allocInfo, nullptr, &m_context->depthImageMemory),
                 m_lastError,
                 "vkAllocateMemory(depth)"))
    {
      return false;
    }

    if (!CheckVk(vkBindImageMemory(m_context->device, m_context->depthImage, m_context->depthImageMemory, 0),
                 m_lastError,
                 "vkBindImageMemory(depth)"))
    {
      return false;
    }

    const VkImageAspectFlags depthAspect =
        depthFormat == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT
                                            : static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT);
    const VkImageViewCreateInfo depthViewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = m_context->depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depthFormat,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {depthAspect, 0, 1, 0, 1},
    };
    if (!CheckVk(vkCreateImageView(m_context->device, &depthViewInfo, nullptr, &m_context->depthImageView),
                 m_lastError,
                 "vkCreateImageView(depth)"))
    {
      return false;
    }

    m_context->depthFormat = depthFormat;
    return true;
  }

  void VulkanRenderBackend::DestroyDepthResources()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    if (m_context->depthImageView != VK_NULL_HANDLE)
    {
      vkDestroyImageView(m_context->device, m_context->depthImageView, nullptr);
      m_context->depthImageView = VK_NULL_HANDLE;
    }
    if (m_context->depthImage != VK_NULL_HANDLE)
    {
      vkDestroyImage(m_context->device, m_context->depthImage, nullptr);
      m_context->depthImage = VK_NULL_HANDLE;
    }
    if (m_context->depthImageMemory != VK_NULL_HANDLE)
    {
      vkFreeMemory(m_context->device, m_context->depthImageMemory, nullptr);
      m_context->depthImageMemory = VK_NULL_HANDLE;
    }
    m_context->depthFormat = VK_FORMAT_UNDEFINED;
  }

  bool VulkanRenderBackend::EnsureReadbackBuffers()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return false;

    DestroyReadbackBuffers();

    const VkDeviceSize colorBytes =
        static_cast<VkDeviceSize>(m_context->swapchainExtent.width) * m_context->swapchainExtent.height *
        4u;
    if (m_context->supportsColorReadback)
    {
      if (!CreateHostVisibleBuffer(m_context->physicalDevice,
                                   m_context->device,
                                   colorBytes,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   &m_context->colorReadbackBuffer,
                                   &m_context->colorReadbackMemory,
                                   m_lastError,
                                   "vkCreateBuffer(colorReadback)"))
      {
        m_context->supportsColorReadback = false;
      }
      else
      {
        m_context->colorReadbackSize = colorBytes;
      }
    }

    if (m_context->supportsDepthReadback)
    {
      const VkDeviceSize depthBytes =
          static_cast<VkDeviceSize>(m_context->swapchainExtent.width) * m_context->swapchainExtent.height *
          sizeof(float);
      if (!CreateHostVisibleBuffer(m_context->physicalDevice,
                                   m_context->device,
                                   depthBytes,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   &m_context->depthReadbackBuffer,
                                   &m_context->depthReadbackMemory,
                                   m_lastError,
                                   "vkCreateBuffer(depthReadback)"))
      {
        m_context->supportsDepthReadback = false;
      }
      else
      {
        m_context->depthReadbackSize = depthBytes;
      }
    }

    m_context->hasColorReadbackData = false;
    m_context->hasDepthReadbackData = false;
    return true;
  }

  void VulkanRenderBackend::DestroyReadbackBuffers()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    if (m_context->colorReadbackBuffer != VK_NULL_HANDLE)
    {
      vkDestroyBuffer(m_context->device, m_context->colorReadbackBuffer, nullptr);
      m_context->colorReadbackBuffer = VK_NULL_HANDLE;
    }
    if (m_context->colorReadbackMemory != VK_NULL_HANDLE)
    {
      vkFreeMemory(m_context->device, m_context->colorReadbackMemory, nullptr);
      m_context->colorReadbackMemory = VK_NULL_HANDLE;
    }
    if (m_context->depthReadbackBuffer != VK_NULL_HANDLE)
    {
      vkDestroyBuffer(m_context->device, m_context->depthReadbackBuffer, nullptr);
      m_context->depthReadbackBuffer = VK_NULL_HANDLE;
    }
    if (m_context->depthReadbackMemory != VK_NULL_HANDLE)
    {
      vkFreeMemory(m_context->device, m_context->depthReadbackMemory, nullptr);
      m_context->depthReadbackMemory = VK_NULL_HANDLE;
    }

    m_context->colorReadbackSize = 0;
    m_context->depthReadbackSize = 0;
    m_context->hasColorReadbackData = false;
    m_context->hasDepthReadbackData = false;
  }

  bool VulkanRenderBackend::CreateOpaqueRasterScaffold()
  {
    const VkAttachmentDescription depthAttachment{
        .flags = 0,
        .format = m_context->depthFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentDescription colorAttachment{
        .flags = 0,
        .format = m_context->swapchainFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    const std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    const VkAttachmentReference colorAttachmentRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthAttachmentRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = &depthAttachmentRef,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };
    const VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0,
    };
    const VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    if (!CheckVk(vkCreateRenderPass(m_context->device,
                                    &renderPassInfo,
                                    nullptr,
                                    &m_context->opaqueRenderPass),
                 m_lastError,
                 "vkCreateRenderPass"))
    {
      return false;
    }

    m_context->opaqueFramebuffers.resize(m_context->swapchainImageViews.size());
    for (size_t i = 0; i < m_context->swapchainImageViews.size(); ++i)
    {
      const std::array<VkImageView, 2> framebufferAttachments = {
          m_context->swapchainImageViews[i],
          m_context->depthImageView,
      };
      const VkFramebufferCreateInfo framebufferInfo{
          .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .renderPass = m_context->opaqueRenderPass,
          .attachmentCount = static_cast<uint32_t>(framebufferAttachments.size()),
          .pAttachments = framebufferAttachments.data(),
          .width = m_context->swapchainExtent.width,
          .height = m_context->swapchainExtent.height,
          .layers = 1,
      };
      if (!CheckVk(vkCreateFramebuffer(m_context->device,
                                       &framebufferInfo,
                                       nullptr,
                                       &m_context->opaqueFramebuffers[i]),
                   m_lastError,
                   "vkCreateFramebuffer"))
      {
        return false;
      }
    }

    return true;
  }

  void VulkanRenderBackend::DestroyOpaqueRasterScaffold()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    for (VkFramebuffer framebuffer : m_context->opaqueFramebuffers)
    {
      if (framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(m_context->device, framebuffer, nullptr);
    }
    m_context->opaqueFramebuffers.clear();

    if (m_context->opaqueRenderPass != VK_NULL_HANDLE)
    {
      vkDestroyRenderPass(m_context->device, m_context->opaqueRenderPass, nullptr);
      m_context->opaqueRenderPass = VK_NULL_HANDLE;
    }
  }

  bool VulkanRenderBackend::CreateOpaquePipelineCreationScaffold()
  {
    const VkPipelineCacheCreateInfo pipelineCacheInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .initialDataSize = 0,
        .pInitialData = nullptr,
    };
    return CheckVk(vkCreatePipelineCache(m_context->device,
                                         &pipelineCacheInfo,
                                         nullptr,
                                         &m_context->opaquePipelineCache),
                   m_lastError,
                   "vkCreatePipelineCache");
  }

  void VulkanRenderBackend::DestroyOpaquePipelineCreationScaffold()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    if (m_context->opaquePipelineCache != VK_NULL_HANDLE)
    {
      vkDestroyPipelineCache(m_context->device, m_context->opaquePipelineCache, nullptr);
      m_context->opaquePipelineCache = VK_NULL_HANDLE;
    }
  }

  bool VulkanRenderBackend::CreateOpaqueMaterialBindingScaffold()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return false;

    const VkDescriptorSetLayoutBinding materialBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr,
    };
    const VkDescriptorSetLayoutCreateInfo setLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = 1,
        .pBindings = &materialBinding,
    };
    if (!CheckVk(vkCreateDescriptorSetLayout(m_context->device,
                                             &setLayoutInfo,
                                             nullptr,
                                             &m_context->opaqueMaterialSetLayout),
                 m_lastError,
                 "vkCreateDescriptorSetLayout(opaqueMaterial)"))
    {
      return false;
    }

    const VkDescriptorPoolSize poolSize{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = kOpaqueMaterialDescriptorCapacity,
    };
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = kOpaqueMaterialDescriptorCapacity,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    if (!CheckVk(vkCreateDescriptorPool(m_context->device,
                                        &poolInfo,
                                        nullptr,
                                        &m_context->opaqueMaterialDescriptorPool),
                 m_lastError,
                 "vkCreateDescriptorPool(opaqueMaterial)"))
    {
      return false;
    }

    const VkDeviceSize materialBufferSize =
        static_cast<VkDeviceSize>(sizeof(OpaqueMaterialGpuData) * kOpaqueMaterialDescriptorCapacity);
    const VkBufferCreateInfo materialBufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = materialBufferSize,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    if (!CheckVk(vkCreateBuffer(m_context->device,
                                &materialBufferInfo,
                                nullptr,
                                &m_context->opaqueMaterialBuffer),
                 m_lastError,
                 "vkCreateBuffer(opaqueMaterial)"))
    {
      return false;
    }

    VkMemoryRequirements materialMemoryRequirements{};
    vkGetBufferMemoryRequirements(m_context->device,
                                  m_context->opaqueMaterialBuffer,
                                  &materialMemoryRequirements);
    const uint32_t memoryTypeIndex =
        FindMemoryTypeIndex(m_context->physicalDevice,
                            materialMemoryRequirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryTypeIndex == kInvalidQueueFamily)
    {
      m_lastError = "No host-visible Vulkan memory type supports opaque material uniform buffer.";
      return false;
    }

    const VkMemoryAllocateInfo materialMemoryAlloc{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = materialMemoryRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };
    if (!CheckVk(vkAllocateMemory(m_context->device,
                                  &materialMemoryAlloc,
                                  nullptr,
                                  &m_context->opaqueMaterialBufferMemory),
                 m_lastError,
                 "vkAllocateMemory(opaqueMaterial)"))
    {
      return false;
    }

    if (!CheckVk(vkBindBufferMemory(m_context->device,
                                    m_context->opaqueMaterialBuffer,
                                    m_context->opaqueMaterialBufferMemory,
                                    0),
                 m_lastError,
                 "vkBindBufferMemory(opaqueMaterial)"))
    {
      return false;
    }

    if (!CheckVk(vkMapMemory(m_context->device,
                             m_context->opaqueMaterialBufferMemory,
                             0,
                             materialBufferSize,
                             0,
                             &m_context->opaqueMaterialBufferMapped),
                 m_lastError,
                 "vkMapMemory(opaqueMaterial)"))
    {
      return false;
    }

    std::vector<VkDescriptorSetLayout> setLayouts(kOpaqueMaterialDescriptorCapacity,
                                                  m_context->opaqueMaterialSetLayout);
    m_context->opaqueMaterialDescriptorSets.resize(kOpaqueMaterialDescriptorCapacity);
    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = m_context->opaqueMaterialDescriptorPool,
        .descriptorSetCount = kOpaqueMaterialDescriptorCapacity,
        .pSetLayouts = setLayouts.data(),
    };
    if (!CheckVk(vkAllocateDescriptorSets(m_context->device,
                                          &allocateInfo,
                                          m_context->opaqueMaterialDescriptorSets.data()),
                 m_lastError,
                 "vkAllocateDescriptorSets(opaqueMaterial)"))
    {
      return false;
    }

    for (uint32_t i = 0; i < kOpaqueMaterialDescriptorCapacity; ++i)
    {
      const VkDescriptorBufferInfo bufferInfo{
          .buffer = m_context->opaqueMaterialBuffer,
          .offset = static_cast<VkDeviceSize>(i * sizeof(OpaqueMaterialGpuData)),
          .range = sizeof(OpaqueMaterialGpuData),
      };
      const VkWriteDescriptorSet write{
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .pNext = nullptr,
          .dstSet = m_context->opaqueMaterialDescriptorSets[i],
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pImageInfo = nullptr,
          .pBufferInfo = &bufferInfo,
          .pTexelBufferView = nullptr,
      };
      vkUpdateDescriptorSets(m_context->device, 1, &write, 0, nullptr);
    }

    return true;
  }

  void VulkanRenderBackend::DestroyOpaqueMaterialBindingScaffold()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    m_context->opaqueMaterialDescriptorSets.clear();
    if (m_context->opaqueMaterialBufferMapped)
    {
      vkUnmapMemory(m_context->device, m_context->opaqueMaterialBufferMemory);
      m_context->opaqueMaterialBufferMapped = nullptr;
    }
    if (m_context->opaqueMaterialBuffer != VK_NULL_HANDLE)
    {
      vkDestroyBuffer(m_context->device, m_context->opaqueMaterialBuffer, nullptr);
      m_context->opaqueMaterialBuffer = VK_NULL_HANDLE;
    }
    if (m_context->opaqueMaterialBufferMemory != VK_NULL_HANDLE)
    {
      vkFreeMemory(m_context->device, m_context->opaqueMaterialBufferMemory, nullptr);
      m_context->opaqueMaterialBufferMemory = VK_NULL_HANDLE;
    }
    if (m_context->opaqueMaterialDescriptorPool != VK_NULL_HANDLE)
    {
      vkDestroyDescriptorPool(m_context->device, m_context->opaqueMaterialDescriptorPool, nullptr);
      m_context->opaqueMaterialDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_context->opaqueMaterialSetLayout != VK_NULL_HANDLE)
    {
      vkDestroyDescriptorSetLayout(m_context->device, m_context->opaqueMaterialSetLayout, nullptr);
      m_context->opaqueMaterialSetLayout = VK_NULL_HANDLE;
    }
  }

  bool VulkanRenderBackend::CreateOpaqueDrawIndexBuffer()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return false;

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = sizeof(kFallbackOpaqueIndices),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    if (!CheckVk(vkCreateBuffer(m_context->device,
                                &bufferInfo,
                                nullptr,
                                &m_context->opaqueFallbackIndexBuffer),
                 m_lastError,
                 "vkCreateBuffer(opaqueIndex)"))
    {
      return false;
    }

    VkMemoryRequirements indexMemoryRequirements{};
    vkGetBufferMemoryRequirements(m_context->device,
                                  m_context->opaqueFallbackIndexBuffer,
                                  &indexMemoryRequirements);
    const uint32_t memoryTypeIndex =
        FindMemoryTypeIndex(m_context->physicalDevice,
                            indexMemoryRequirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryTypeIndex == kInvalidQueueFamily)
    {
      m_lastError = "No host-visible Vulkan memory type supports opaque fallback index buffer.";
      return false;
    }

    const VkMemoryAllocateInfo memoryAllocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = indexMemoryRequirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };
    if (!CheckVk(vkAllocateMemory(m_context->device,
                                  &memoryAllocInfo,
                                  nullptr,
                                  &m_context->opaqueFallbackIndexBufferMemory),
                 m_lastError,
                 "vkAllocateMemory(opaqueIndex)"))
    {
      return false;
    }
    if (!CheckVk(vkBindBufferMemory(m_context->device,
                                    m_context->opaqueFallbackIndexBuffer,
                                    m_context->opaqueFallbackIndexBufferMemory,
                                    0),
                 m_lastError,
                 "vkBindBufferMemory(opaqueIndex)"))
    {
      return false;
    }

    void *mapped = nullptr;
    if (!CheckVk(vkMapMemory(m_context->device,
                             m_context->opaqueFallbackIndexBufferMemory,
                             0,
                             sizeof(kFallbackOpaqueIndices),
                             0,
                             &mapped),
                 m_lastError,
                 "vkMapMemory(opaqueIndex)"))
    {
      return false;
    }
    std::memcpy(mapped, kFallbackOpaqueIndices.data(), sizeof(kFallbackOpaqueIndices));
    vkUnmapMemory(m_context->device, m_context->opaqueFallbackIndexBufferMemory);
    return true;
  }

  void VulkanRenderBackend::DestroyOpaqueDrawIndexBuffer()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    if (m_context->opaqueFallbackIndexBuffer != VK_NULL_HANDLE)
    {
      vkDestroyBuffer(m_context->device, m_context->opaqueFallbackIndexBuffer, nullptr);
      m_context->opaqueFallbackIndexBuffer = VK_NULL_HANDLE;
    }
    if (m_context->opaqueFallbackIndexBufferMemory != VK_NULL_HANDLE)
    {
      vkFreeMemory(m_context->device, m_context->opaqueFallbackIndexBufferMemory, nullptr);
      m_context->opaqueFallbackIndexBufferMemory = VK_NULL_HANDLE;
    }
  }

  bool VulkanRenderBackend::EnsureOpaqueMeshGpuBuffers(const Mesh &mesh)
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return false;

    if (m_context->opaqueMeshGpuBuffers.find(&mesh) != m_context->opaqueMeshGpuBuffers.end())
      return true;

    const std::vector<Vertex> &vertices = mesh.GetVertices();
    const std::vector<uint32_t> &indices = mesh.GetIndices();
    if (vertices.empty() || indices.empty())
      return false;

    Context::MeshGpuBuffers gpuBuffers;
    const VkDeviceSize vertexBufferSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(Vertex));
    const VkDeviceSize indexBufferSize = static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t));

    if (!CreateHostVisibleBuffer(m_context->physicalDevice,
                                 m_context->device,
                                 vertexBufferSize,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 &gpuBuffers.vertexBuffer,
                                 &gpuBuffers.vertexMemory,
                                 m_lastError,
                                 "CreateHostVisibleBuffer(vertex)"))
    {
      return false;
    }

    if (!CreateHostVisibleBuffer(m_context->physicalDevice,
                                 m_context->device,
                                 indexBufferSize,
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 &gpuBuffers.indexBuffer,
                                 &gpuBuffers.indexMemory,
                                 m_lastError,
                                 "CreateHostVisibleBuffer(index)"))
    {
      vkDestroyBuffer(m_context->device, gpuBuffers.vertexBuffer, nullptr);
      vkFreeMemory(m_context->device, gpuBuffers.vertexMemory, nullptr);
      return false;
    }

    void *vertexMapped = nullptr;
    if (!CheckVk(vkMapMemory(m_context->device,
                             gpuBuffers.vertexMemory,
                             0,
                             vertexBufferSize,
                             0,
                             &vertexMapped),
                 m_lastError,
                 "vkMapMemory(meshVertex)"))
    {
      vkDestroyBuffer(m_context->device, gpuBuffers.indexBuffer, nullptr);
      vkFreeMemory(m_context->device, gpuBuffers.indexMemory, nullptr);
      vkDestroyBuffer(m_context->device, gpuBuffers.vertexBuffer, nullptr);
      vkFreeMemory(m_context->device, gpuBuffers.vertexMemory, nullptr);
      return false;
    }
    std::memcpy(vertexMapped, vertices.data(), static_cast<size_t>(vertexBufferSize));
    vkUnmapMemory(m_context->device, gpuBuffers.vertexMemory);

    void *indexMapped = nullptr;
    if (!CheckVk(vkMapMemory(m_context->device,
                             gpuBuffers.indexMemory,
                             0,
                             indexBufferSize,
                             0,
                             &indexMapped),
                 m_lastError,
                 "vkMapMemory(meshIndex)"))
    {
      vkDestroyBuffer(m_context->device, gpuBuffers.indexBuffer, nullptr);
      vkFreeMemory(m_context->device, gpuBuffers.indexMemory, nullptr);
      vkDestroyBuffer(m_context->device, gpuBuffers.vertexBuffer, nullptr);
      vkFreeMemory(m_context->device, gpuBuffers.vertexMemory, nullptr);
      return false;
    }
    std::memcpy(indexMapped, indices.data(), static_cast<size_t>(indexBufferSize));
    vkUnmapMemory(m_context->device, gpuBuffers.indexMemory);

    gpuBuffers.indexCount = static_cast<uint32_t>(indices.size());
    m_context->opaqueMeshGpuBuffers.emplace(&mesh, gpuBuffers);
    return true;
  }

  void VulkanRenderBackend::DestroyOpaqueMeshGpuBuffers()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    for (auto &entry : m_context->opaqueMeshGpuBuffers)
    {
      Context::MeshGpuBuffers &gpuBuffers = entry.second;
      if (gpuBuffers.indexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_context->device, gpuBuffers.indexBuffer, nullptr);
      if (gpuBuffers.indexMemory != VK_NULL_HANDLE)
        vkFreeMemory(m_context->device, gpuBuffers.indexMemory, nullptr);
      if (gpuBuffers.vertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_context->device, gpuBuffers.vertexBuffer, nullptr);
      if (gpuBuffers.vertexMemory != VK_NULL_HANDLE)
        vkFreeMemory(m_context->device, gpuBuffers.vertexMemory, nullptr);
    }
    m_context->opaqueMeshGpuBuffers.clear();
  }

  bool VulkanRenderBackend::CreateOpaqueShaderPipelineScaffold()
  {
    const auto tryLoadShaderModule = [&](const std::array<std::string, 2> &candidatePaths,
                                         VkShaderModule *outModule,
                                         const char *context)
    {
      for (const std::string &path : candidatePaths)
      {
        const std::vector<char> bytes = ReadBinaryFile(path);
        if (bytes.empty())
          continue;
        if (TryCreateShaderModule(m_context->device, bytes, outModule, m_lastError, context))
          return true;
        return false;
      }
      return false;
    };

    m_context->opaqueVertexShaderModule = VK_NULL_HANDLE;
    m_context->opaqueFragmentShaderModule = VK_NULL_HANDLE;
    m_context->opaqueShaderStages = {};

    const bool vertexShaderLoaded = tryLoadShaderModule(
        {"assets/shaders/vulkan/opaque_scene.vert.spv", "renderer/shaders/vulkan/opaque_scene.vert.spv"},
        &m_context->opaqueVertexShaderModule,
        "vkCreateShaderModule(vertex)");
    const bool fragmentShaderLoaded = tryLoadShaderModule(
        {"assets/shaders/vulkan/opaque_scene.frag.spv", "renderer/shaders/vulkan/opaque_scene.frag.spv"},
        &m_context->opaqueFragmentShaderModule,
        "vkCreateShaderModule(fragment)");

    m_context->opaqueShaderStages[0] = VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = m_context->opaqueVertexShaderModule,
        .pName = m_context->opaqueShaderEntryNames[0].c_str(),
        .pSpecializationInfo = nullptr,
    };
    m_context->opaqueShaderStages[1] = VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = m_context->opaqueFragmentShaderModule,
        .pName = m_context->opaqueShaderEntryNames[1].c_str(),
        .pSpecializationInfo = nullptr,
    };

    if ((!vertexShaderLoaded || !fragmentShaderLoaded) && m_lastError.empty())
    {
      m_lastError =
          "Vulkan opaque shader modules are not available yet. Expected precompiled SPIR-V files at "
          "assets/shaders/vulkan/opaque_scene.{vert,frag}.spv or "
          "renderer/shaders/vulkan/opaque_scene.{vert,frag}.spv";
    }

    m_context->opaqueShaderPipelineScaffoldReady =
        m_context->opaquePipelineLayout != VK_NULL_HANDLE &&
        m_context->opaquePipelineCache != VK_NULL_HANDLE &&
        m_context->opaqueShaderStages[0].pName != nullptr &&
        m_context->opaqueShaderStages[1].pName != nullptr;
    return m_context->opaqueShaderPipelineScaffoldReady;
  }

  void VulkanRenderBackend::DestroyOpaqueShaderPipelineScaffold()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    if (m_context->opaqueVertexShaderModule != VK_NULL_HANDLE)
    {
      vkDestroyShaderModule(m_context->device, m_context->opaqueVertexShaderModule, nullptr);
      m_context->opaqueVertexShaderModule = VK_NULL_HANDLE;
    }
    if (m_context->opaqueFragmentShaderModule != VK_NULL_HANDLE)
    {
      vkDestroyShaderModule(m_context->device, m_context->opaqueFragmentShaderModule, nullptr);
      m_context->opaqueFragmentShaderModule = VK_NULL_HANDLE;
    }

    m_context->opaqueShaderStages = {};
    m_context->opaqueShaderPipelineScaffoldReady = false;
  }

  bool VulkanRenderBackend::CreateOpaqueGraphicsPipelineScaffold()
  {
    const VkPipeline previousPrimaryPipeline = m_context->opaqueGraphicsPipeline;
    for (auto &entry : m_context->opaquePipelineCacheByKey)
    {
      const VkPipeline cachedPipeline = entry.second;
      if (cachedPipeline != VK_NULL_HANDLE && cachedPipeline != previousPrimaryPipeline)
        vkDestroyPipeline(m_context->device, cachedPipeline, nullptr);
    }
    if (previousPrimaryPipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(m_context->device, previousPrimaryPipeline, nullptr);
    m_context->opaqueGraphicsPipeline = VK_NULL_HANDLE;
    m_context->opaquePipelineCacheByKey.clear();

    m_context->opaqueGraphicsPipelineScaffoldReady =
        m_context->opaqueShaderPipelineScaffoldReady && m_context->opaqueRenderPass != VK_NULL_HANDLE;
    m_context->opaqueGraphicsPipelineExecutable = false;
    if (!m_context->opaqueGraphicsPipelineScaffoldReady)
    {
      m_lastError = "Opaque graphics pipeline scaffold prerequisites are incomplete.";
      return false;
    }

    if (m_context->opaqueShaderStages[0].module == VK_NULL_HANDLE ||
        m_context->opaqueShaderStages[1].module == VK_NULL_HANDLE)
    {
      m_lastError =
          "Vulkan opaque graphics pipeline cannot execute draws because shader modules are unavailable.";
      return true;
    }

    void *pipelineHandle = nullptr;
    if (!GetOrCreateOpaquePipeline({}, &pipelineHandle))
      return false;

    m_context->opaqueGraphicsPipeline = reinterpret_cast<VkPipeline>(pipelineHandle);
    m_context->opaqueGraphicsPipelineExecutable = m_context->opaqueGraphicsPipeline != VK_NULL_HANDLE;
    return m_context->opaqueGraphicsPipelineExecutable;
  }

  void VulkanRenderBackend::DestroyOpaqueGraphicsPipelineScaffold()
  {
    if (!m_context || m_context->device == VK_NULL_HANDLE)
      return;

    const VkPipeline primaryPipeline = m_context->opaqueGraphicsPipeline;
    for (auto &entry : m_context->opaquePipelineCacheByKey)
    {
      const VkPipeline pipeline = entry.second;
      if (pipeline != VK_NULL_HANDLE && pipeline != primaryPipeline)
        vkDestroyPipeline(m_context->device, pipeline, nullptr);
    }
    if (primaryPipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(m_context->device, primaryPipeline, nullptr);
    m_context->opaqueGraphicsPipeline = VK_NULL_HANDLE;
    m_context->opaquePipelineCacheByKey.clear();

    m_context->opaqueGraphicsPipelineExecutable = false;
    m_context->opaqueGraphicsPipelineScaffoldReady = false;
  }

  bool VulkanRenderBackend::GetOrCreateOpaquePipeline(const OpaquePipelineKey &key, void **outPipelineHandle)
  {
    if (!outPipelineHandle || !m_context)
      return false;
    *outPipelineHandle = nullptr;

    const uint8_t packedKey = PackOpaquePipelineKey(key);
    const auto it = m_context->opaquePipelineCacheByKey.find(packedKey);
    if (it != m_context->opaquePipelineCacheByKey.end() && it->second != VK_NULL_HANDLE)
    {
      *outPipelineHandle = reinterpret_cast<void *>(it->second);
      return true;
    }

    if (m_context->opaqueShaderStages[0].module == VK_NULL_HANDLE ||
        m_context->opaqueShaderStages[1].module == VK_NULL_HANDLE)
    {
      m_lastError = "Cannot build opaque Vulkan pipeline for key: shader modules are unavailable.";
      return false;
    }

    const VkVertexInputBindingDescription vertexBinding{
        .binding = 0,
        .stride = static_cast<uint32_t>(sizeof(Vertex)),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const std::array<VkVertexInputAttributeDescription, 3> vertexAttributes = {
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = static_cast<uint32_t>(offsetof(Vertex, position)),
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = static_cast<uint32_t>(offsetof(Vertex, normal)),
        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = static_cast<uint32_t>(offsetof(Vertex, uv)),
        }};
    const VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBinding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };
    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr,
    };
    const VkPipelineRasterizationStateCreateInfo rasterizationState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = key.usesAlbedoMap ? static_cast<VkCullModeFlags>(VK_CULL_MODE_BACK_BIT)
                                      : static_cast<VkCullModeFlags>(VK_CULL_MODE_NONE),
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisampleState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    const VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo colorBlendState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
    };
    const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                         VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    const VkPipelineDepthStencilStateCreateInfo depthStencilState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = key.depthTestEnabled ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = key.writesDepth ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };
    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stageCount = static_cast<uint32_t>(m_context->opaqueShaderStages.size()),
        .pStages = m_context->opaqueShaderStages.data(),
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pTessellationState = nullptr,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &depthStencilState,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = &dynamicState,
        .layout = m_context->opaquePipelineLayout,
        .renderPass = m_context->opaqueRenderPass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    VkPipeline newPipeline = VK_NULL_HANDLE;
    if (!CheckVk(vkCreateGraphicsPipelines(m_context->device,
                                           m_context->opaquePipelineCache,
                                           1,
                                           &pipelineInfo,
                                           nullptr,
                                           &newPipeline),
                 m_lastError,
                 "vkCreateGraphicsPipelines(opaque-keyed)"))
    {
      return false;
    }

    m_context->opaquePipelineCacheByKey.emplace(packedKey, newPipeline);
    if (m_context->opaqueGraphicsPipeline == VK_NULL_HANDLE)
      m_context->opaqueGraphicsPipeline = newPipeline;
    *outPipelineHandle = reinterpret_cast<void *>(newPipeline);
    return true;
  }

  bool VulkanRenderBackend::RecreateSwapchain()
  {
    DestroyOpaqueGraphicsPipelineScaffold();
    DestroySwapchain();

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    if (!CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_context->physicalDevice,
                                                           m_context->surface,
                                                           &surfaceCapabilities),
                 m_lastError,
                 "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
    {
      return false;
    }

    uint32_t surfaceFormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_context->physicalDevice,
                                         m_context->surface,
                                         &surfaceFormatCount,
                                         nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_context->physicalDevice,
                                         m_context->surface,
                                         &surfaceFormatCount,
                                         surfaceFormats.data());
    VkSurfaceFormatKHR chosenFormat = surfaceFormats.front();
    for (const auto &candidate : surfaceFormats)
    {
      if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM &&
          candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      {
        chosenFormat = candidate;
        break;
      }
    }
    m_context->swapchainFormat = chosenFormat.format;

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_context->physicalDevice,
                                              m_context->surface,
                                              &presentModeCount,
                                              nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_context->physicalDevice,
                                              m_context->surface,
                                              &presentModeCount,
                                              presentModes.data());
    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR presentMode : presentModes)
    {
      if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
      {
        chosenPresentMode = presentMode;
        break;
      }
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_context->window, &framebufferWidth, &framebufferHeight);
    framebufferWidth = std::max(framebufferWidth, 1);
    framebufferHeight = std::max(framebufferHeight, 1);

    if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
      m_context->swapchainExtent = surfaceCapabilities.currentExtent;
    }
    else
    {
      m_context->swapchainExtent.width = std::clamp(static_cast<uint32_t>(framebufferWidth),
                                                    surfaceCapabilities.minImageExtent.width,
                                                    surfaceCapabilities.maxImageExtent.width);
      m_context->swapchainExtent.height = std::clamp(static_cast<uint32_t>(framebufferHeight),
                                                     surfaceCapabilities.minImageExtent.height,
                                                     surfaceCapabilities.maxImageExtent.height);
    }

    uint32_t imageCount = std::max(surfaceCapabilities.minImageCount, 2u);
    if (surfaceCapabilities.maxImageCount > 0)
      imageCount = std::min(imageCount, surfaceCapabilities.maxImageCount);

    std::array<uint32_t, 2> queueFamilyIndices = {m_context->graphicsQueueFamily,
                                                  m_context->presentQueueFamily};
    const bool usesSeparatePresentQueue =
        m_context->graphicsQueueFamily != m_context->presentQueueFamily;

    const bool surfaceSupportsTransferSrc =
        (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkImageUsageFlags swapchainUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (surfaceSupportsTransferSrc)
      swapchainUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    const VkSwapchainCreateInfoKHR swapchainInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = m_context->surface,
        .minImageCount = imageCount,
        .imageFormat = chosenFormat.format,
        .imageColorSpace = chosenFormat.colorSpace,
        .imageExtent = m_context->swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = swapchainUsage,
        .imageSharingMode = usesSeparatePresentQueue ? VK_SHARING_MODE_CONCURRENT
                                                     : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = usesSeparatePresentQueue ? 2u : 0u,
        .pQueueFamilyIndices = usesSeparatePresentQueue ? queueFamilyIndices.data() : nullptr,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = chosenPresentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    if (!CheckVk(vkCreateSwapchainKHR(m_context->device, &swapchainInfo, nullptr,
                                      &m_context->swapchain),
                 m_lastError,
                 "vkCreateSwapchainKHR"))
    {
      return false;
    }

    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(m_context->device, m_context->swapchain, &swapchainImageCount, nullptr);
    m_context->swapchainImages.resize(swapchainImageCount);
    vkGetSwapchainImagesKHR(m_context->device,
                            m_context->swapchain,
                            &swapchainImageCount,
                            m_context->swapchainImages.data());
    m_context->swapchainImageViews.resize(swapchainImageCount);
    m_context->imageWasPresented.assign(swapchainImageCount, false);

    for (size_t i = 0; i < m_context->swapchainImages.size(); ++i)
    {
      const VkImageViewCreateInfo imageViewInfo{
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .image = m_context->swapchainImages[i],
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = m_context->swapchainFormat,
          .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY},
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      if (!CheckVk(vkCreateImageView(m_context->device,
                                     &imageViewInfo,
                                     nullptr,
                                     &m_context->swapchainImageViews[i]),
                   m_lastError,
                   "vkCreateImageView"))
      {
        return false;
      }
    }

    m_context->commandBuffers.resize(swapchainImageCount);
    const VkCommandBufferAllocateInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = m_context->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = swapchainImageCount,
    };
    if (!CheckVk(vkAllocateCommandBuffers(m_context->device,
                                          &commandBufferInfo,
                                          m_context->commandBuffers.data()),
                 m_lastError,
                 "vkAllocateCommandBuffers"))
    {
      return false;
    }

    if (!CreateDepthResources())
      return false;

    m_context->supportsColorReadback = (swapchainUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    m_context->supportsDepthReadback =
        m_context->depthFormat == VK_FORMAT_D32_SFLOAT &&
        HasFormatFeature(m_context->physicalDevice,
                         m_context->depthFormat,
                         VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
    if (!EnsureReadbackBuffers())
      return false;

    if (!CreateOpaqueRasterScaffold())
      return false;

    if (m_context->opaqueShaderPipelineScaffoldReady && !CreateOpaqueGraphicsPipelineScaffold())
      return false;

    return true;
  }

  bool VulkanRenderBackend::RecordFrameCommands(const RenderFrameConfig &frame)
  {
    VkCommandBuffer commandBuffer = m_context->commandBuffers[m_context->activeImageIndex];
    if (!CheckVk(vkResetCommandBuffer(commandBuffer, 0), m_lastError, "vkResetCommandBuffer"))
      return false;

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };
    if (!CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), m_lastError,
                 "vkBeginCommandBuffer"))
    {
      return false;
    }

    const bool hasQueuedOpaqueDraws = !m_pendingOpaqueDraws.empty();
    const bool hasQueuedOverlayRender = m_pendingOverlayRenderCallback != nullptr;
    const bool hasExecutableOpaquePipeline = HasOpaqueDrawExecutionReady();
    if (hasQueuedOpaqueDraws && !hasExecutableOpaquePipeline)
    {
      m_lastError =
          "Opaque scene draw submissions are queued, but Vulkan draw execution is not ready: "
          "shader modules and graphics pipeline are still scaffold-only. Pending draws: " +
          std::to_string(m_pendingOpaqueDraws.size());
    }

    bool colorLayoutIsPresent = false;
    bool colorLayoutIsTransferSrc = false;
    bool colorLayoutIsTransferDst = false;
    if (hasQueuedOpaqueDraws || hasQueuedOverlayRender)
    {
      const VkImageMemoryBarrier toColorAttachment{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = nullptr,
          .srcAccessMask = 0,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .oldLayout = m_context->imageWasPresented[m_context->activeImageIndex]
                           ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                           : VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image = m_context->swapchainImages[m_context->activeImageIndex],
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           1,
                           &toColorAttachment);

      const std::array<VkClearValue, 2> clearValues = {
          VkClearValue{.color = {{frame.clearColor.x, frame.clearColor.y, frame.clearColor.z, frame.clearColor.w}}},
          VkClearValue{.depthStencil = {1.0f, 0u}},
      };
      const VkRenderPassBeginInfo renderPassBeginInfo{
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .pNext = nullptr,
          .renderPass = m_context->opaqueRenderPass,
          .framebuffer = m_context->opaqueFramebuffers[m_context->activeImageIndex],
          .renderArea = {{0, 0}, m_context->swapchainExtent},
          .clearValueCount = static_cast<uint32_t>(clearValues.size()),
          .pClearValues = clearValues.data(),
      };
      vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

      const VkViewport viewport{
          .x = 0.0f,
          .y = 0.0f,
          .width = static_cast<float>(m_context->swapchainExtent.width),
          .height = static_cast<float>(m_context->swapchainExtent.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      };
      const VkRect2D scissor{{0, 0}, m_context->swapchainExtent};
      vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
      vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
      if (hasQueuedOpaqueDraws && hasExecutableOpaquePipeline)
      {
        for (size_t drawIndex = 0; drawIndex < m_pendingOpaqueDraws.size(); ++drawIndex)
        {
          const PendingOpaqueDraw &draw = m_pendingOpaqueDraws[drawIndex];
          if (!draw.mesh || draw.indexCount <= 0)
            continue;

          if (!EnsureOpaqueMeshGpuBuffers(*draw.mesh))
          {
            if (m_lastError.empty())
            {
              m_lastError = "Vulkan opaque draw execution skipped because mesh GPU buffers are unavailable.";
            }
            continue;
          }

          const auto meshGpuIt = m_context->opaqueMeshGpuBuffers.find(draw.mesh);
          if (meshGpuIt == m_context->opaqueMeshGpuBuffers.end())
            continue;
          const Context::MeshGpuBuffers &meshGpu = meshGpuIt->second;
          if (meshGpu.indexBuffer == VK_NULL_HANDLE || meshGpu.vertexBuffer == VK_NULL_HANDLE ||
              meshGpu.indexCount == 0)
            continue;

          void *pipelineHandle = nullptr;
          if (!GetOrCreateOpaquePipeline(draw.pipelineKey, &pipelineHandle))
            continue;
          VkPipeline pipeline = reinterpret_cast<VkPipeline>(pipelineHandle);

          if (pipeline == VK_NULL_HANDLE)
            continue;

          const VkBuffer vertexBuffers[] = {meshGpu.vertexBuffer};
          const VkDeviceSize vertexOffsets[] = {0};
          vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
          vkCmdBindIndexBuffer(commandBuffer, meshGpu.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

          const uint32_t materialSlot =
              static_cast<uint32_t>(drawIndex % m_context->opaqueMaterialDescriptorSets.size());
          auto *materialStorage = reinterpret_cast<OpaqueMaterialGpuData *>(m_context->opaqueMaterialBufferMapped);
          OpaqueMaterialGpuData &materialGpu = materialStorage[materialSlot];
          materialGpu.baseColor[0] = draw.material.baseColor.x;
          materialGpu.baseColor[1] = draw.material.baseColor.y;
          materialGpu.baseColor[2] = draw.material.baseColor.z;
          materialGpu.baseColor[3] = draw.material.baseColor.w;
          materialGpu.roughness = draw.material.roughness;
          materialGpu.metallic = draw.material.metallic;
          materialGpu.uvScale = draw.material.uvScale;

          vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
          vkCmdBindDescriptorSets(commandBuffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_context->opaquePipelineLayout,
                                  0,
                                  1,
                                  &m_context->opaqueMaterialDescriptorSets[materialSlot],
                                  0,
                                  nullptr);

          const uint32_t submittedIndexCount = static_cast<uint32_t>(draw.indexCount);
          const uint32_t drawIndexCount = std::min(submittedIndexCount, meshGpu.indexCount);
          if (drawIndexCount == 0)
            continue;
          vkCmdDrawIndexed(commandBuffer, drawIndexCount, 1u, 0u, 0, 0u);
          ++m_executedOpaqueIndexedDraws;
        }
      }

      if (hasQueuedOverlayRender)
      {
        m_pendingOverlayRenderCallback(m_pendingOverlayRenderUserData,
                                       reinterpret_cast<void *>(commandBuffer));
      }

      vkCmdEndRenderPass(commandBuffer);
      colorLayoutIsPresent = true;

      auto viewportTargetIt = m_context->offscreenTargets.find(kEditorViewportTargetKey);
      if (viewportTargetIt != m_context->offscreenTargets.end())
      {
        Context::OffscreenRenderTarget &viewportTarget = viewportTargetIt->second;
        if (viewportTarget.readyForSampling && viewportTarget.image != VK_NULL_HANDLE)
        {
          const VkImageMemoryBarrier sourceToTransfer{
              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
              .pNext = nullptr,
              .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
              .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
              .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = m_context->swapchainImages[m_context->activeImageIndex],
              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
          };
          vkCmdPipelineBarrier(commandBuffer,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &sourceToTransfer);

          const VkImageLayout destinationOldLayout =
              viewportTarget.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED
                                                                        : viewportTarget.currentLayout;
          const VkImageMemoryBarrier targetToTransfer{
              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
              .pNext = nullptr,
              .srcAccessMask = destinationOldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                   ? static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT)
                                   : 0u,
              .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
              .oldLayout = destinationOldLayout,
              .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = viewportTarget.image,
              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
          };
          vkCmdPipelineBarrier(commandBuffer,
                               destinationOldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                   ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                   : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &targetToTransfer);

          VkImageBlit blitRegion{};
          blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
          blitRegion.srcOffsets[0] = {0, 0, 0};
          blitRegion.srcOffsets[1] = {static_cast<int32_t>(m_context->swapchainExtent.width),
                                      static_cast<int32_t>(m_context->swapchainExtent.height),
                                      1};
          blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
          blitRegion.dstOffsets[0] = {0, 0, 0};
          blitRegion.dstOffsets[1] = {static_cast<int32_t>(viewportTarget.width),
                                      static_cast<int32_t>(viewportTarget.height),
                                      1};
          vkCmdBlitImage(commandBuffer,
                         m_context->swapchainImages[m_context->activeImageIndex],
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         viewportTarget.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1,
                         &blitRegion,
                         VK_FILTER_LINEAR);

          const VkImageMemoryBarrier targetToSample{
              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
              .pNext = nullptr,
              .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
              .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = viewportTarget.image,
              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
          };
          vkCmdPipelineBarrier(commandBuffer,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &targetToSample);
          viewportTarget.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          colorLayoutIsPresent = false;
          colorLayoutIsTransferSrc = true;
        }
      }

      if (m_context->supportsDepthReadback && m_context->depthReadbackBuffer != VK_NULL_HANDLE &&
          m_context->depthFormat == VK_FORMAT_D32_SFLOAT)
      {
        const VkImageMemoryBarrier depthToTransfer{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_context->depthImage,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        };
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &depthToTransfer);

        const VkBufferImageCopy depthCopyRegion{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {m_context->swapchainExtent.width, m_context->swapchainExtent.height, 1},
        };
        vkCmdCopyImageToBuffer(commandBuffer,
                               m_context->depthImage,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_context->depthReadbackBuffer,
                               1,
                               &depthCopyRegion);

        const VkImageMemoryBarrier depthToAttachment{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_context->depthImage,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        };
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &depthToAttachment);
        m_context->hasDepthReadbackData = true;
      }
    }
    else if (frame.clearColorBuffer)
    {
      const VkImageMemoryBarrier toTransfer{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = nullptr,
          .srcAccessMask = 0,
          .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .oldLayout = m_context->imageWasPresented[m_context->activeImageIndex]
                           ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                           : VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image = m_context->swapchainImages[m_context->activeImageIndex],
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           1,
                           &toTransfer);

      const VkClearColorValue clearColor = {{frame.clearColor.x,
                                             frame.clearColor.y,
                                             frame.clearColor.z,
                                             frame.clearColor.w}};
      const VkImageSubresourceRange clearRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdClearColorImage(commandBuffer,
                           m_context->swapchainImages[m_context->activeImageIndex],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           &clearColor,
                           1,
                           &clearRange);
      colorLayoutIsTransferDst = true;
    }

    if (m_context->supportsColorReadback && m_context->colorReadbackBuffer != VK_NULL_HANDLE &&
        (colorLayoutIsPresent || colorLayoutIsTransferSrc || colorLayoutIsTransferDst))
    {
      if (!colorLayoutIsTransferSrc)
      {
        const VkImageMemoryBarrier toTransferSource{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = colorLayoutIsPresent
                                 ? static_cast<VkAccessFlags>(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
                                 : static_cast<VkAccessFlags>(VK_ACCESS_TRANSFER_WRITE_BIT),
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = colorLayoutIsPresent ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                              : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_context->swapchainImages[m_context->activeImageIndex],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCmdPipelineBarrier(commandBuffer,
                             colorLayoutIsPresent ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                                  : VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &toTransferSource);
      }

      const VkBufferImageCopy colorCopyRegion{
          .bufferOffset = 0,
          .bufferRowLength = 0,
          .bufferImageHeight = 0,
          .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
          .imageOffset = {0, 0, 0},
          .imageExtent = {m_context->swapchainExtent.width, m_context->swapchainExtent.height, 1},
      };
      vkCmdCopyImageToBuffer(commandBuffer,
                             m_context->swapchainImages[m_context->activeImageIndex],
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             m_context->colorReadbackBuffer,
                             1,
                             &colorCopyRegion);

      const VkImageMemoryBarrier toPresent{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = nullptr,
          .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .dstAccessMask = 0,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image = m_context->swapchainImages[m_context->activeImageIndex],
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           1,
                           &toPresent);
      m_context->hasColorReadbackData = true;
    }
    else if (colorLayoutIsTransferSrc)
    {
      const VkImageMemoryBarrier toPresent{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = nullptr,
          .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .dstAccessMask = 0,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image = m_context->swapchainImages[m_context->activeImageIndex],
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           1,
                           &toPresent);
    }
    else if (colorLayoutIsTransferDst)
    {
      const VkImageMemoryBarrier toPresent{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = nullptr,
          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask = 0,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image = m_context->swapchainImages[m_context->activeImageIndex],
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           1,
                           &toPresent);
    }

    if (!CheckVk(vkEndCommandBuffer(commandBuffer), m_lastError, "vkEndCommandBuffer"))
      return false;

    m_context->frameCommandsRecorded = true;
    return true;
  }

  void VulkanRenderBackend::BeginFrame(const RenderFrameConfig &frame)
  {
    MONOLITH_ASSERT(IsInitialized(), "VulkanRenderBackend::BeginFrame called before initialization");
    MONOLITH_ASSERT(!m_frameActive, "VulkanRenderBackend::BeginFrame called while a frame is active");
    if (!IsInitialized() || m_frameActive)
      return;

    m_lastError.clear();

    if (!CheckVk(vkWaitForFences(m_context->device, 1, &m_context->inFlightFence, VK_TRUE, UINT64_MAX),
                 m_lastError,
                 "vkWaitForFences"))
    {
      return;
    }

    VkResult acquireResult = vkAcquireNextImageKHR(m_context->device,
                                                   m_context->swapchain,
                                                   UINT64_MAX,
                                                   m_context->imageAvailableSemaphore,
                                                   VK_NULL_HANDLE,
                                                   &m_context->activeImageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
    {
      MONOLITH_ASSERT(RecreateSwapchain(), m_lastError.c_str());
      acquireResult = vkAcquireNextImageKHR(m_context->device,
                                            m_context->swapchain,
                                            UINT64_MAX,
                                            m_context->imageAvailableSemaphore,
                                            VK_NULL_HANDLE,
                                             &m_context->activeImageIndex);
    }
    if (acquireResult != VK_SUCCESS)
    {
      m_lastError = "vkAcquireNextImageKHR failed during BeginFrame: " +
                    std::string(VkResultName(acquireResult));
      return;
    }

    if (!CheckVk(vkResetFences(m_context->device, 1, &m_context->inFlightFence),
                 m_lastError,
                 "vkResetFences"))
    {
      return;
    }

    m_activeFrame = frame;
    const TemporalHistoryState currentTemporalHistoryState = BuildTemporalHistoryState(frame.temporal);
    if (m_hasTemporalHistoryState)
    {
      const GiHistoryResetReason resetReason =
          DetermineGiHistoryResetReason(m_lastTemporalHistoryState, currentTemporalHistoryState);
      if (resetReason != GiHistoryResetReason::None &&
          m_giHistoryCatalog.Has(GiHistorySemantic::DiffuseIrradiance))
      {
        InvalidateGiHistory(resetReason, nullptr);
      }
    }
    m_lastTemporalHistoryState = currentTemporalHistoryState;
    m_hasTemporalHistoryState = true;
    m_lastSsrPassContract = {};
    m_lastSsgiPassContract = {};
    m_lastTemporalGiResolvePassContract = {};
    m_lastLightingCompositePassContract = {};
    m_lastSceneTracingRepresentationContract = {};
    m_lastCachedHitLightingRepresentationContract = {};
    m_hasSsrPassContract = false;
    m_hasSsgiPassContract = false;
    m_hasTemporalGiResolvePassContract = false;
    m_hasLightingCompositePassContract = false;
    m_hasSceneTracingRepresentationContract = false;
    m_hasCachedHitLightingRepresentationContract = false;
    m_activeView = {};
    if (m_pendingOpaqueDraws.capacity() < 256)
      m_pendingOpaqueDraws.reserve(256);
    m_pendingOpaqueDraws.clear();
    m_context->frameCommandsRecorded = false;
    m_drawCalls = 0;
    m_executedOpaqueIndexedDraws = 0;
    m_frameActive = true;
  }

  void VulkanRenderBackend::EndFrame()
  {
    MONOLITH_ASSERT(m_frameActive, "VulkanRenderBackend::EndFrame called without an active frame");
    if (!m_frameActive)
      return;

    if (m_passActive)
      EndPass();

    if (!m_context->frameCommandsRecorded)
    {
      MONOLITH_ASSERT(RecordFrameCommands(m_activeFrame), m_lastError.c_str());
    }

    if (m_context->frameCommandsRecorded)
    {
      const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
      const VkSubmitInfo submitInfo{
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .pNext = nullptr,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &m_context->imageAvailableSemaphore,
          .pWaitDstStageMask = waitStages,
          .commandBufferCount = 1,
          .pCommandBuffers = &m_context->commandBuffers[m_context->activeImageIndex],
          .signalSemaphoreCount = 1,
          .pSignalSemaphores = &m_context->renderFinishedSemaphore,
      };
      if (!CheckVk(vkQueueSubmit(m_context->graphicsQueue,
                                 1,
                                 &submitInfo,
                                 m_context->inFlightFence),
                   m_lastError,
                   "vkQueueSubmit"))
      {
        m_frameActive = false;
        return;
      }

      const VkPresentInfoKHR presentInfo{
          .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
          .pNext = nullptr,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &m_context->renderFinishedSemaphore,
          .swapchainCount = 1,
          .pSwapchains = &m_context->swapchain,
          .pImageIndices = &m_context->activeImageIndex,
          .pResults = nullptr,
      };
      const VkResult presentResult = vkQueuePresentKHR(m_context->presentQueue, &presentInfo);
      if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
      {
        MONOLITH_ASSERT(RecreateSwapchain(), m_lastError.c_str());
      }
      else if (presentResult != VK_SUCCESS)
      {
        m_lastError = "vkQueuePresentKHR failed during EndFrame: " +
                      std::string(VkResultName(presentResult));
        m_frameActive = false;
        return;
      }

      m_context->imageWasPresented[m_context->activeImageIndex] = true;
      m_context->frameCommandsRecorded = false;
    }

    m_frameActive = false;
    m_pendingOpaqueDraws.clear();
    m_pendingOverlayRenderCallback = nullptr;
    m_pendingOverlayRenderUserData = nullptr;
    ExecuteScreenSpaceReflectionPass();
    ExecuteScreenSpaceGlobalIlluminationPass();
    ExecuteTemporalGiResolvePass();
    ExecuteLightingCompositePass();
    if (m_giHistoryCatalog.Has(GiHistorySemantic::DiffuseIrradiance))
    {
      m_giHistoryCatalog.ownerState = m_lastTemporalHistoryState;
      m_giHistoryCatalog.validForTemporalReuse = m_lastTemporalHistoryState.temporalEnabled;
    }
  }

  void VulkanRenderBackend::BeginPass(const RenderPassConfig &pass)
  {
    MONOLITH_ASSERT(m_frameActive, "VulkanRenderBackend::BeginPass called without an active frame");
    MONOLITH_ASSERT(!m_passActive, "VulkanRenderBackend::BeginPass called while a pass is active");
    if (!m_frameActive || m_passActive)
      return;

    m_activePassId = pass.id;
    m_activeView = pass.view;
    m_passActive = true;
  }

  void VulkanRenderBackend::EndPass()
  {
    MONOLITH_ASSERT(m_passActive, "VulkanRenderBackend::EndPass called without an active pass");
    if (!m_passActive)
      return;
    m_passActive = false;
  }

  void VulkanRenderBackend::DrawMesh(const MeshDrawCommand &command)
  {
    if (!m_passActive || !IsSupportedVulkanScenePass(m_activePassId) || !command.mesh || !command.material)
      return;

    m_pendingOpaqueDraws.push_back(
        PendingOpaqueDraw{command.mesh,
                          ResolveIndexCount(*command.mesh),
                          command.modelMatrix,
                          TranslateMaterialState(*command.material),
                          {}});
    m_pendingOpaqueDraws.back().pipelineKey = BuildOpaquePipelineKey(m_pendingOpaqueDraws.back().material);
    ++m_drawCalls;
  }

  void VulkanRenderBackend::DrawSkinnedMesh(const SkinnedMeshDrawCommand &) {}

  void VulkanRenderBackend::DrawWireframe(const WireframeDrawCommand &) {}

#else

  struct VulkanRenderBackend::Context
  {
  };

  VulkanRenderBackend::VulkanRenderBackend(void *)
  {
    m_lastError = "Vulkan backend was compiled without Vulkan dependency support.";
  }

  VulkanRenderBackend::~VulkanRenderBackend() = default;

  RenderBackendCapabilities VulkanRenderBackend::GetCapabilities() const
  {
    return GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
  }

  bool VulkanRenderBackend::IsInitialized() const
  {
    return false;
  }

  bool VulkanRenderBackend::HasOpaqueRasterScaffold() const { return false; }
  bool VulkanRenderBackend::HasOpaquePipelineCreationScaffold() const { return false; }
  bool VulkanRenderBackend::HasOpaqueShaderPipelineScaffold() const { return false; }
  bool VulkanRenderBackend::HasOpaqueGraphicsPipelineScaffold() const { return false; }
  bool VulkanRenderBackend::HasOpaqueDrawExecutionReady() const { return false; }

  bool VulkanRenderBackend::TryGetImGuiVulkanInitData(void **,
                                                      void **,
                                                      void **,
                                                      uint32_t *,
                                                      void **,
                                                      void **,
                                                      uint32_t *) const
  {
    return false;
  }

  void *VulkanRenderBackend::GetActiveCommandBufferHandle() const
  {
    return nullptr;
  }

  void VulkanRenderBackend::QueueOverlayRenderCallback(OverlayRenderCallback, void *) {}
  bool VulkanRenderBackend::ReadbackColorBgr8(int, int, std::vector<uint8_t> &, std::string *)
  {
    return false;
  }
  bool VulkanRenderBackend::ReadbackDepth32F(int, int, std::vector<float> &, std::string *)
  {
    return false;
  }
  bool VulkanRenderBackend::EnsureEditorViewportRenderTarget(uint32_t, uint32_t, std::string *)
  {
    return false;
  }
  bool VulkanRenderBackend::TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *,
                                                                   bool,
                                                                   std::string *)
  {
    return false;
  }
  bool VulkanRenderBackend::EnsureSceneTextureResources(uint32_t, uint32_t, std::string *) { return false; }
  bool VulkanRenderBackend::TryGetSceneTextureCatalog(SceneTextureCatalog *, std::string *) const
  {
    return false;
  }
  bool VulkanRenderBackend::EnsureGiHistoryResources(uint32_t, uint32_t, std::string *) { return false; }
  bool VulkanRenderBackend::TryGetGiHistoryCatalog(GiHistoryCatalog *, std::string *) const
  {
    return false;
  }
  bool VulkanRenderBackend::TryGetScreenSpaceReflectionPassContract(ScreenSpaceReflectionPassContract *,
                                                                    std::string *) const
  {
    return false;
  }
  bool VulkanRenderBackend::TryGetScreenSpaceGlobalIlluminationPassContract(
      ScreenSpaceGlobalIlluminationPassContract *,
      std::string *) const
  {
    return false;
  }
  bool VulkanRenderBackend::TryGetTemporalGiResolvePassContract(
      TemporalGiResolvePassContract *,
      std::string *) const
  {
    return false;
  }
  bool VulkanRenderBackend::TryGetLightingCompositePassContract(
      LightingCompositePassContract *,
      std::string *) const
  {
    return false;
  }
  bool VulkanRenderBackend::TryGetSceneTracingRepresentationContract(
      SceneTracingRepresentationContract *,
      std::string *) const
  {
    return false;
  }
  bool VulkanRenderBackend::TryGetCachedHitLightingRepresentationContract(
      CachedHitLightingRepresentationContract *,
      std::string *) const
  {
    return false;
  }
  bool VulkanRenderBackend::InvalidateGiHistory(GiHistoryResetReason, std::string *) { return false; }
  bool VulkanRenderBackend::EnsureOffscreenRenderTarget(const std::string &, uint32_t, uint32_t) { return false; }
  bool VulkanRenderBackend::TryGetOffscreenRenderTargetHandle(const std::string &,
                                                              RenderTargetHandle *,
                                                              bool)
  {
    return false;
  }
  bool VulkanRenderBackend::TryGetOffscreenRenderTargetMetadata(const std::string &,
                                                                OffscreenTargetMetadata *) const
  {
    return false;
  }
  void VulkanRenderBackend::DestroyOffscreenRenderTarget(const std::string &) {}
  void VulkanRenderBackend::DestroyAllOffscreenRenderTargets() {}
  bool VulkanRenderBackend::BuildOffscreenResourceHandle(const std::string &, BackendResourceHandle *) const
  {
    return false;
  }

  bool VulkanRenderBackend::Initialize(void *)
  {
    return false;
  }

  void VulkanRenderBackend::Shutdown() {}
  bool VulkanRenderBackend::RecreateSwapchain() { return false; }
  void VulkanRenderBackend::DestroySwapchain() {}
  bool VulkanRenderBackend::CreateOpaqueRasterScaffold() { return false; }
  void VulkanRenderBackend::DestroyOpaqueRasterScaffold() {}
  bool VulkanRenderBackend::CreateOpaquePipelineCreationScaffold() { return false; }
  void VulkanRenderBackend::DestroyOpaquePipelineCreationScaffold() {}
  bool VulkanRenderBackend::CreateDepthResources() { return false; }
  void VulkanRenderBackend::DestroyDepthResources() {}
  bool VulkanRenderBackend::EnsureReadbackBuffers() { return false; }
  void VulkanRenderBackend::DestroyReadbackBuffers() {}
  bool VulkanRenderBackend::CreateOpaqueMaterialBindingScaffold() { return false; }
  void VulkanRenderBackend::DestroyOpaqueMaterialBindingScaffold() {}
  bool VulkanRenderBackend::CreateOpaqueDrawIndexBuffer() { return false; }
  void VulkanRenderBackend::DestroyOpaqueDrawIndexBuffer() {}
  bool VulkanRenderBackend::EnsureOpaqueMeshGpuBuffers(const Mesh &) { return false; }
  void VulkanRenderBackend::DestroyOpaqueMeshGpuBuffers() {}
  bool VulkanRenderBackend::CreateOpaqueShaderPipelineScaffold() { return false; }
  void VulkanRenderBackend::DestroyOpaqueShaderPipelineScaffold() {}
  bool VulkanRenderBackend::GetOrCreateOpaquePipeline(const OpaquePipelineKey &, void **) { return false; }
  bool VulkanRenderBackend::CreateOpaqueGraphicsPipelineScaffold() { return false; }
  void VulkanRenderBackend::DestroyOpaqueGraphicsPipelineScaffold() {}
  bool VulkanRenderBackend::RecordFrameCommands(const RenderFrameConfig &) { return false; }
  void VulkanRenderBackend::ExecuteScreenSpaceReflectionPass() {}
  void VulkanRenderBackend::ExecuteScreenSpaceGlobalIlluminationPass() {}
  void VulkanRenderBackend::ExecuteTemporalGiResolvePass() {}
  void VulkanRenderBackend::ExecuteLightingCompositePass() {}
  bool VulkanRenderBackend::EnsureOffscreenRenderPass() { return false; }
  void VulkanRenderBackend::DestroyOffscreenRenderPass() {}
  bool VulkanRenderBackend::CreateOffscreenRenderTargetResources(const std::string &,
                                                                 uint32_t,
                                                                 uint32_t,
                                                                 uint64_t)
  {
    return false;
  }
  void VulkanRenderBackend::DestroyOffscreenRenderTargetResources(const std::string &) {}
  bool VulkanRenderBackend::TryRegisterOffscreenTargetImGuiDescriptor(const std::string &,
                                                                      RenderTargetHandle *,
                                                                      bool)
  {
    return false;
  }
  void VulkanRenderBackend::BeginFrame(const RenderFrameConfig &) {}
  void VulkanRenderBackend::EndFrame() {}
  void VulkanRenderBackend::BeginPass(const RenderPassConfig &) {}
  void VulkanRenderBackend::EndPass() {}
  void VulkanRenderBackend::DrawMesh(const MeshDrawCommand &) {}
  void VulkanRenderBackend::DrawSkinnedMesh(const SkinnedMeshDrawCommand &) {}
  void VulkanRenderBackend::DrawWireframe(const WireframeDrawCommand &) {}

#endif

} // namespace Monolith
