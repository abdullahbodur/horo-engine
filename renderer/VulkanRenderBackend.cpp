#include "renderer/VulkanRenderBackend.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/Assert.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"

#if defined(MONOLITH_HAS_VULKAN)
#include <volk.h>
#include <GLFW/glfw3.h>
#endif

namespace Monolith {

#if defined(MONOLITH_HAS_VULKAN)

namespace {

constexpr uint32_t kInvalidQueueFamily = std::numeric_limits<uint32_t>::max();

const char* VkResultName(VkResult result) {
  switch (result) {
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

bool CheckVk(VkResult result, std::string& outError, const char* context) {
  if (result == VK_SUCCESS)
    return true;
  outError = std::string(context) + " failed: " + VkResultName(result);
  return false;
}

}  // namespace

struct VulkanRenderBackend::Context {
  GLFWwindow* window = nullptr;
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
  VkRenderPass opaqueRenderPass = VK_NULL_HANDLE;
  VkPipelineLayout opaquePipelineLayout = VK_NULL_HANDLE;
  VkPipelineCache opaquePipelineCache = VK_NULL_HANDLE;
  std::vector<VkImage> swapchainImages;
  std::vector<VkImageView> swapchainImageViews;
  std::vector<VkFramebuffer> opaqueFramebuffers;
  std::vector<VkCommandBuffer> commandBuffers;
  std::vector<bool> imageWasPresented;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
  VkFence inFlightFence = VK_NULL_HANDLE;
  uint32_t activeImageIndex = 0;
  bool frameCommandsRecorded = false;
};

namespace {

constexpr bool IsSupportedVulkanScenePass(const RenderPassId passId) {
  return passId == RenderPassId::OpaqueScene || passId == RenderPassId::CompatibilityScene;
}

}  // namespace

VulkanRenderBackend::TranslatedMaterialState VulkanRenderBackend::TranslateMaterialState(
    const Material& material) {
  TranslatedMaterialState translated;
  translated.baseColor = material.color;
  translated.roughness = material.roughness;
  translated.metallic = material.metallic;
  translated.uvScale = material.uvScale;
  translated.usesAlbedoMap = material.albedoMap && material.albedoMap->IsValid();
  translated.usesCustomShader = material.shader && material.shader->IsValid();
  return translated;
}

int VulkanRenderBackend::ResolveIndexCount(const Mesh& mesh) {
  return mesh.GetIndexCount();
}

VulkanRenderBackend::OpaquePipelineKey VulkanRenderBackend::BuildOpaquePipelineKey(
    const TranslatedMaterialState& materialState) {
  OpaquePipelineKey key;
  key.usesAlbedoMap = materialState.usesAlbedoMap;
  key.usesCustomShader = materialState.usesCustomShader;
  key.writesDepth = true;
  key.depthTestEnabled = true;
  return key;
}

VulkanRenderBackend::VulkanRenderBackend(void* nativeWindowHandle) {
  Initialize(nativeWindowHandle);
}

VulkanRenderBackend::~VulkanRenderBackend() {
  Shutdown();
}

RenderBackendCapabilities VulkanRenderBackend::GetCapabilities() const {
  RenderBackendCapabilities caps = GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
  caps.supportsDebugDraw = false;
  caps.supportsDebugLabels = false;
  caps.supportsOffscreenTargets = false;
  caps.supportsNativeTextureHandles = false;
  caps.supportsReadback = false;
  caps.supportsDepthReadback = false;
  caps.supportsDebugHud = false;
  caps.supportsComputePasses = false;
  caps.supportsGpuTimestamps = false;
  caps.supportsBindlessResources = false;
  return caps;
}

bool VulkanRenderBackend::IsInitialized() const {
  return m_context && m_context->device != VK_NULL_HANDLE && m_context->swapchain != VK_NULL_HANDLE;
}

bool VulkanRenderBackend::HasOpaqueRasterScaffold() const {
  return m_context && m_context->opaqueRenderPass != VK_NULL_HANDLE &&
         m_context->opaquePipelineLayout != VK_NULL_HANDLE &&
         m_context->opaqueFramebuffers.size() == m_context->swapchainImageViews.size() &&
         !m_context->opaqueFramebuffers.empty();
}

bool VulkanRenderBackend::HasOpaquePipelineCreationScaffold() const {
  return HasOpaqueRasterScaffold() && m_context && m_context->opaquePipelineCache != VK_NULL_HANDLE;
}

bool VulkanRenderBackend::Initialize(void* nativeWindowHandle) {
  m_lastError.clear();
  if (!nativeWindowHandle) {
    m_lastError = "Vulkan backend requires a native GLFW window handle.";
    return false;
  }

  if (!glfwVulkanSupported()) {
    m_lastError = "GLFW reports that Vulkan is not supported on this system.";
    return false;
  }

  if (!CheckVk(volkInitialize(), m_lastError, "volkInitialize"))
    return false;

  m_context = std::make_unique<Context>();
  m_context->window = static_cast<GLFWwindow*>(nativeWindowHandle);

  uint32_t extensionCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
  if (!glfwExtensions || extensionCount == 0) {
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

  std::vector<const char*> instanceExtensions(glfwExtensions, glfwExtensions + extensionCount);
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
               "vkCreateInstance")) {
    Shutdown();
    return false;
  }

  volkLoadInstance(m_context->instance);

  if (!CheckVk(glfwCreateWindowSurface(m_context->instance, m_context->window, nullptr,
                                       &m_context->surface),
               m_lastError,
               "glfwCreateWindowSurface")) {
    Shutdown();
    return false;
  }

  uint32_t physicalDeviceCount = 0;
  if (!CheckVk(vkEnumeratePhysicalDevices(m_context->instance, &physicalDeviceCount, nullptr),
               m_lastError,
               "vkEnumeratePhysicalDevices(count)")) {
    Shutdown();
    return false;
  }
  if (physicalDeviceCount == 0) {
    m_lastError = "No Vulkan physical devices were found.";
    Shutdown();
    return false;
  }

  std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
  if (!CheckVk(vkEnumeratePhysicalDevices(m_context->instance, &physicalDeviceCount,
                                          physicalDevices.data()),
               m_lastError,
               "vkEnumeratePhysicalDevices")) {
    Shutdown();
    return false;
  }

  const std::array<const char*, 1> requiredDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  for (VkPhysicalDevice device : physicalDevices) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
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
    for (const auto& extension : extensions)
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

  if (m_context->physicalDevice == VK_NULL_HANDLE) {
    m_lastError = "No Vulkan physical device supports graphics, present, and swapchain requirements.";
    Shutdown();
    return false;
  }

  const float queuePriority = 1.0f;
  std::set<uint32_t> uniqueQueueFamilies = {m_context->graphicsQueueFamily,
                                            m_context->presentQueueFamily};
  std::vector<VkDeviceQueueCreateInfo> queueInfos;
  queueInfos.reserve(uniqueQueueFamilies.size());
  for (uint32_t queueFamily : uniqueQueueFamilies) {
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
               "vkCreateDevice")) {
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
               "vkCreateCommandPool")) {
    Shutdown();
    return false;
  }

  const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                             .pNext = nullptr,
                                             .flags = 0};
  if (!CheckVk(vkCreateSemaphore(m_context->device, &semaphoreInfo, nullptr,
                                 &m_context->imageAvailableSemaphore),
               m_lastError,
               "vkCreateSemaphore(imageAvailable)")) {
    Shutdown();
    return false;
  }
  if (!CheckVk(vkCreateSemaphore(m_context->device, &semaphoreInfo, nullptr,
                                 &m_context->renderFinishedSemaphore),
               m_lastError,
               "vkCreateSemaphore(renderFinished)")) {
    Shutdown();
    return false;
  }

  const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                    .pNext = nullptr,
                                    .flags = VK_FENCE_CREATE_SIGNALED_BIT};
  if (!CheckVk(vkCreateFence(m_context->device, &fenceInfo, nullptr, &m_context->inFlightFence),
               m_lastError,
               "vkCreateFence")) {
    Shutdown();
    return false;
  }

  const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .setLayoutCount = 0,
      .pSetLayouts = nullptr,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr,
  };
  if (!CheckVk(vkCreatePipelineLayout(m_context->device,
                                      &pipelineLayoutInfo,
                                      nullptr,
                                      &m_context->opaquePipelineLayout),
               m_lastError,
               "vkCreatePipelineLayout")) {
    Shutdown();
    return false;
  }

  if (!CreateOpaquePipelineCreationScaffold()) {
    Shutdown();
    return false;
  }

  if (!RecreateSwapchain()) {
    Shutdown();
    return false;
  }

  return true;
}

void VulkanRenderBackend::Shutdown() {
  if (!m_context)
    return;

  if (m_context->device != VK_NULL_HANDLE)
    vkDeviceWaitIdle(m_context->device);

  DestroySwapchain();

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
}

void VulkanRenderBackend::DestroySwapchain() {
  if (!m_context || m_context->device == VK_NULL_HANDLE)
    return;

  DestroyOpaqueRasterScaffold();

  if (!m_context->commandBuffers.empty()) {
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

  if (m_context->swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(m_context->device, m_context->swapchain, nullptr);
    m_context->swapchain = VK_NULL_HANDLE;
  }
}

bool VulkanRenderBackend::CreateOpaqueRasterScaffold() {
  const VkAttachmentDescription colorAttachment{
      .flags = 0,
      .format = m_context->swapchainFormat,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  const VkAttachmentReference colorAttachmentRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
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
  const VkSubpassDependency dependency{
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dependencyFlags = 0,
  };
  const VkRenderPassCreateInfo renderPassInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .attachmentCount = 1,
      .pAttachments = &colorAttachment,
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
               "vkCreateRenderPass")) {
    return false;
  }

  m_context->opaqueFramebuffers.resize(m_context->swapchainImageViews.size());
  for (size_t i = 0; i < m_context->swapchainImageViews.size(); ++i) {
    const VkImageView attachment = m_context->swapchainImageViews[i];
    const VkFramebufferCreateInfo framebufferInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = m_context->opaqueRenderPass,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .width = m_context->swapchainExtent.width,
        .height = m_context->swapchainExtent.height,
        .layers = 1,
    };
    if (!CheckVk(vkCreateFramebuffer(m_context->device,
                                     &framebufferInfo,
                                     nullptr,
                                     &m_context->opaqueFramebuffers[i]),
                 m_lastError,
                 "vkCreateFramebuffer")) {
      return false;
    }
  }

  return true;
}

void VulkanRenderBackend::DestroyOpaqueRasterScaffold() {
  if (!m_context || m_context->device == VK_NULL_HANDLE)
    return;

  for (VkFramebuffer framebuffer : m_context->opaqueFramebuffers) {
    if (framebuffer != VK_NULL_HANDLE)
      vkDestroyFramebuffer(m_context->device, framebuffer, nullptr);
  }
  m_context->opaqueFramebuffers.clear();

  if (m_context->opaqueRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(m_context->device, m_context->opaqueRenderPass, nullptr);
    m_context->opaqueRenderPass = VK_NULL_HANDLE;
  }
}

bool VulkanRenderBackend::CreateOpaquePipelineCreationScaffold() {
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

void VulkanRenderBackend::DestroyOpaquePipelineCreationScaffold() {
  if (!m_context || m_context->device == VK_NULL_HANDLE)
    return;

  if (m_context->opaquePipelineCache != VK_NULL_HANDLE) {
    vkDestroyPipelineCache(m_context->device, m_context->opaquePipelineCache, nullptr);
    m_context->opaquePipelineCache = VK_NULL_HANDLE;
  }
}

bool VulkanRenderBackend::RecreateSwapchain() {
  DestroySwapchain();

  VkSurfaceCapabilitiesKHR surfaceCapabilities{};
  if (!CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_context->physicalDevice,
                                                         m_context->surface,
                                                         &surfaceCapabilities),
               m_lastError,
               "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
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
  for (const auto& candidate : surfaceFormats) {
    if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM &&
        candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
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
  for (VkPresentModeKHR presentMode : presentModes) {
    if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      chosenPresentMode = presentMode;
      break;
    }
  }

  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(m_context->window, &framebufferWidth, &framebufferHeight);
  framebufferWidth = std::max(framebufferWidth, 1);
  framebufferHeight = std::max(framebufferHeight, 1);

  if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    m_context->swapchainExtent = surfaceCapabilities.currentExtent;
  } else {
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
      .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
               "vkCreateSwapchainKHR")) {
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

  for (size_t i = 0; i < m_context->swapchainImages.size(); ++i) {
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
                 "vkCreateImageView")) {
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
               "vkAllocateCommandBuffers")) {
    return false;
  }

  if (!CreateOpaqueRasterScaffold())
    return false;

  return true;
}

bool VulkanRenderBackend::RecordFrameCommands(const RenderFrameConfig& frame) {
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
               "vkBeginCommandBuffer")) {
    return false;
  }

  if (frame.clearColorBuffer) {
    if (!m_pendingOpaqueDraws.empty()) {
      // Placeholder for the first opaque-scene submission slice: keep the current
      // clear/present bootstrap, but make frame recording aware that opaque scene
      // work was queued through the backend seam.
      m_lastError.clear();
    }

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

void VulkanRenderBackend::BeginFrame(const RenderFrameConfig& frame) {
  MONOLITH_ASSERT(IsInitialized(), "VulkanRenderBackend::BeginFrame called before initialization");
  MONOLITH_ASSERT(!m_frameActive, "VulkanRenderBackend::BeginFrame called while a frame is active");
  if (!IsInitialized() || m_frameActive)
    return;

  if (!CheckVk(vkWaitForFences(m_context->device, 1, &m_context->inFlightFence, VK_TRUE, UINT64_MAX),
               m_lastError,
               "vkWaitForFences")) {
    return;
  }

  VkResult acquireResult = vkAcquireNextImageKHR(m_context->device,
                                                 m_context->swapchain,
                                                 UINT64_MAX,
                                                 m_context->imageAvailableSemaphore,
                                                 VK_NULL_HANDLE,
                                                 &m_context->activeImageIndex);
  if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
    MONOLITH_ASSERT(RecreateSwapchain(), m_lastError.c_str());
    acquireResult = vkAcquireNextImageKHR(m_context->device,
                                          m_context->swapchain,
                                          UINT64_MAX,
                                          m_context->imageAvailableSemaphore,
                                          VK_NULL_HANDLE,
                                          &m_context->activeImageIndex);
  }
  MONOLITH_ASSERT(acquireResult == VK_SUCCESS, "vkAcquireNextImageKHR failed for Vulkan backend");

  MONOLITH_ASSERT(CheckVk(vkResetFences(m_context->device, 1, &m_context->inFlightFence),
                          m_lastError,
                          "vkResetFences"),
                  m_lastError.c_str());

  m_activeFrame = frame;
  m_activeView = {};
  m_pendingOpaqueDraws.clear();
  m_context->frameCommandsRecorded = false;
  m_drawCalls = 0;
  m_frameActive = true;
}

void VulkanRenderBackend::EndFrame() {
  MONOLITH_ASSERT(m_frameActive, "VulkanRenderBackend::EndFrame called without an active frame");
  if (!m_frameActive)
    return;

  if (m_passActive)
    EndPass();

  if (!m_context->frameCommandsRecorded) {
    MONOLITH_ASSERT(RecordFrameCommands(m_activeFrame), m_lastError.c_str());
  }

  if (m_context->frameCommandsRecorded) {
    const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
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
    MONOLITH_ASSERT(CheckVk(vkQueueSubmit(m_context->graphicsQueue,
                                          1,
                                          &submitInfo,
                                          m_context->inFlightFence),
                            m_lastError,
                            "vkQueueSubmit"),
                    m_lastError.c_str());

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
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
      MONOLITH_ASSERT(RecreateSwapchain(), m_lastError.c_str());
    } else {
      MONOLITH_ASSERT(presentResult == VK_SUCCESS, "vkQueuePresentKHR failed for Vulkan backend");
    }

    m_context->imageWasPresented[m_context->activeImageIndex] = true;
    m_context->frameCommandsRecorded = false;
  }

  m_frameActive = false;
  m_pendingOpaqueDraws.clear();
}

void VulkanRenderBackend::BeginPass(const RenderPassConfig& pass) {
  MONOLITH_ASSERT(m_frameActive, "VulkanRenderBackend::BeginPass called without an active frame");
  MONOLITH_ASSERT(!m_passActive, "VulkanRenderBackend::BeginPass called while a pass is active");
  if (!m_frameActive || m_passActive)
    return;

  m_activePassId = pass.id;
  m_activeView = pass.view;
  m_passActive = true;
}

void VulkanRenderBackend::EndPass() {
  MONOLITH_ASSERT(m_passActive, "VulkanRenderBackend::EndPass called without an active pass");
  if (!m_passActive)
    return;
  m_passActive = false;
}

void VulkanRenderBackend::DrawMesh(const MeshDrawCommand& command) {
  if (!m_passActive || !IsSupportedVulkanScenePass(m_activePassId) || !command.mesh || !command.material)
    return;

  m_pendingOpaqueDraws.push_back(
      PendingOpaqueDraw{ResolveIndexCount(*command.mesh),
                        command.modelMatrix,
                        TranslateMaterialState(*command.material),
                        {}});
  m_pendingOpaqueDraws.back().pipelineKey = BuildOpaquePipelineKey(m_pendingOpaqueDraws.back().material);
  ++m_drawCalls;
}

void VulkanRenderBackend::DrawSkinnedMesh(const SkinnedMeshDrawCommand&) {}

void VulkanRenderBackend::DrawWireframe(const WireframeDrawCommand&) {}

#else

struct VulkanRenderBackend::Context {};

VulkanRenderBackend::VulkanRenderBackend(void*) {
  m_lastError = "Vulkan backend was compiled without Vulkan dependency support.";
}

VulkanRenderBackend::~VulkanRenderBackend() = default;

RenderBackendCapabilities VulkanRenderBackend::GetCapabilities() const {
  return GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
}

bool VulkanRenderBackend::IsInitialized() const {
  return false;
}

bool VulkanRenderBackend::Initialize(void*) {
  return false;
}

void VulkanRenderBackend::Shutdown() {}
bool VulkanRenderBackend::RecreateSwapchain() { return false; }
void VulkanRenderBackend::DestroySwapchain() {}
bool VulkanRenderBackend::RecordFrameCommands(const RenderFrameConfig&) { return false; }
void VulkanRenderBackend::BeginFrame(const RenderFrameConfig&) {}
void VulkanRenderBackend::EndFrame() {}
void VulkanRenderBackend::BeginPass(const RenderPassConfig&) {}
void VulkanRenderBackend::EndPass() {}
void VulkanRenderBackend::DrawMesh(const MeshDrawCommand&) {}
void VulkanRenderBackend::DrawSkinnedMesh(const SkinnedMeshDrawCommand&) {}
void VulkanRenderBackend::DrawWireframe(const WireframeDrawCommand&) {}

#endif

}  // namespace Monolith
