#include "editor/EditorImGuiBackend.h"

#include <algorithm>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "renderer/Renderer.h"
#include "renderer/VulkanRenderBackend.h"

#if defined(HORO_HAS_VULKAN)
#include <imgui_impl_vulkan.h>
#endif

namespace Horo::Editor {
#if defined(HORO_HAS_VULKAN)
    namespace {
        struct VulkanImGuiState {
            VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
            VkRenderPass renderPass = VK_NULL_HANDLE;
            bool initialized = false;
            uint32_t minImageCount = 0;
        };

        VulkanImGuiState g_vulkanImGuiState;

        VkDescriptorPool CreateVulkanImGuiDescriptorPool(VkDevice device) {
            const VkDescriptorPoolSize poolSizes[] = {
                {VK_DESCRIPTOR_TYPE_SAMPLER, 64},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64},
                {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 64},
                {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 64},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 64},
                {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64},
            };

            const VkDescriptorPoolCreateInfo poolInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets = 512,
                .poolSizeCount = static_cast<uint32_t>(std::size(poolSizes)),
                .pPoolSizes = poolSizes,
            };

            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
                return VK_NULL_HANDLE;
            return pool;
        }

        void DestroyVulkanImGuiDescriptorPool(VkDevice device) {
            if (device == VK_NULL_HANDLE ||
                g_vulkanImGuiState.descriptorPool == VK_NULL_HANDLE)
                return;
            vkDestroyDescriptorPool(device, g_vulkanImGuiState.descriptorPool, nullptr);
            g_vulkanImGuiState.descriptorPool = VK_NULL_HANDLE;
        }

        void RenderVulkanImGuiDrawDataCallback(void *userData,
                                               void *commandBufferHandle) {
            if (userData == nullptr || commandBufferHandle == nullptr)
                return;

            ImGui_ImplVulkan_RenderDrawData(
                reinterpret_cast<ImDrawData *>(userData),
                reinterpret_cast<VkCommandBuffer>(commandBufferHandle));
        }

        bool TryBuildVulkanImGuiInitInfo(VulkanRenderBackend *backend,
                                         ImGui_ImplVulkan_InitInfo *outInitInfo,
                                         uint32_t *outMinImageCount,
                                         VkRenderPass *outRenderPass) {
            if (!backend || !outInitInfo || !outMinImageCount || !outRenderPass)
                return false;

            void *instanceHandle = nullptr;
            void *physicalDeviceHandle = nullptr;
            void *deviceHandle = nullptr;
            void *queueHandle = nullptr;
            void *renderPassHandle = nullptr;
            uint32_t queueFamily = 0;
            uint32_t imageCount = 0;
            if (!backend->TryGetImGuiVulkanInitData(
                &instanceHandle, &physicalDeviceHandle, &deviceHandle, &queueFamily,
                &queueHandle, &renderPassHandle, &imageCount)) {
                return false;
            }

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.Instance = reinterpret_cast<VkInstance>(instanceHandle);
            initInfo.PhysicalDevice =
                    reinterpret_cast<VkPhysicalDevice>(physicalDeviceHandle);
            initInfo.Device = reinterpret_cast<VkDevice>(deviceHandle);
            initInfo.QueueFamily = queueFamily;
            initInfo.Queue = reinterpret_cast<VkQueue>(queueHandle);
            initInfo.DescriptorPool = g_vulkanImGuiState.descriptorPool;
            initInfo.RenderPass = reinterpret_cast<VkRenderPass>(renderPassHandle);
            initInfo.MinImageCount = std::max(2u, imageCount);
            initInfo.ImageCount = std::max(2u, imageCount);
            initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            initInfo.Subpass = 0;
            initInfo.UseDynamicRendering = false;

            *outInitInfo = initInfo;
            *outMinImageCount = initInfo.MinImageCount;
            *outRenderPass = initInfo.RenderPass;
            return true;
        }
    } // namespace
#endif // HORO_HAS_VULKAN

    bool IsSupportedEditorImGuiBackend(RenderBackendId backendId) {
        using enum RenderBackendId;
        switch (backendId) {
            case Auto:
            case OpenGL:
                return true;
            case Vulkan:
#if defined(HORO_HAS_VULKAN)
                return true;
#else
                return false;
#endif
            case Null:
                return false;
        }

        return false;
    }

    bool InitEditorImGuiBackend(GLFWwindow *window, RenderBackendId backendId) {
        if (!window)
            return false;

        using enum RenderBackendId;
        switch (backendId) {
            case Auto:
            case OpenGL:
                ImGui_ImplGlfw_InitForOpenGL(window, true);
                ImGui_ImplOpenGL3_Init("#version 410");
                return true;
            case Vulkan: {
#if defined(HORO_HAS_VULKAN)
                auto *backend =
                        dynamic_cast<VulkanRenderBackend *>(Renderer::GetBackendForInterop());
                if (!backend)
                    return false;

                ImGui_ImplVulkan_InitInfo initInfo{};
                uint32_t minImageCount = 0;
                VkRenderPass renderPass = VK_NULL_HANDLE;
                if (!TryBuildVulkanImGuiInitInfo(backend, &initInfo, &minImageCount,
                                                 &renderPass))
                    return false;

                const VkDevice device = initInfo.Device;
                if (g_vulkanImGuiState.descriptorPool == VK_NULL_HANDLE) {
                    g_vulkanImGuiState.descriptorPool =
                            CreateVulkanImGuiDescriptorPool(device);
                }
                if (g_vulkanImGuiState.descriptorPool == VK_NULL_HANDLE)
                    return false;

                if (!TryBuildVulkanImGuiInitInfo(backend, &initInfo, &minImageCount,
                                                 &renderPass))
                    return false;

                if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
                    DestroyVulkanImGuiDescriptorPool(device);
                    return false;
                }

                if (!ImGui_ImplVulkan_Init(&initInfo)) {
                    ImGui_ImplGlfw_Shutdown();
                    DestroyVulkanImGuiDescriptorPool(device);
                    return false;
                }

                g_vulkanImGuiState.initialized = true;
                g_vulkanImGuiState.minImageCount = minImageCount;
                g_vulkanImGuiState.renderPass = renderPass;
                return true;
#else
                return false;
#endif
            }
            case Null:
                return false;
        }

        return false;
    }

    void ShutdownEditorImGuiBackend(RenderBackendId backendId) {
        using enum RenderBackendId;
        switch (backendId) {
            case Auto:
            case OpenGL:
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                return;
            case Vulkan: {
#if defined(HORO_HAS_VULKAN)
                if (!g_vulkanImGuiState.initialized)
                    return;

                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                g_vulkanImGuiState.initialized = false;
                g_vulkanImGuiState.minImageCount = 0;
                g_vulkanImGuiState.renderPass = VK_NULL_HANDLE;

                auto *backend =
                        dynamic_cast<VulkanRenderBackend *>(Renderer::GetBackendForInterop());
                if (backend) {
                    void *deviceHandle = nullptr;
                    if (backend->TryGetImGuiVulkanInitData(nullptr, nullptr, &deviceHandle,
                                                           nullptr, nullptr, nullptr,
                                                           nullptr)) {
                        DestroyVulkanImGuiDescriptorPool(
                            reinterpret_cast<VkDevice>(deviceHandle));
                    }
                }
#endif
                return;
            }
            case Null:
                return;
        }
    }

    void BeginEditorImGuiFrame(RenderBackendId backendId) {
        using enum RenderBackendId;
        switch (backendId) {
            case Auto:
            case OpenGL:
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                return;
            case Vulkan:
#if defined(HORO_HAS_VULKAN)
                if (!g_vulkanImGuiState.initialized)
                    return;

                if (auto *backend = dynamic_cast<VulkanRenderBackend *>(
                    Renderer::GetBackendForInterop())) {
                    ImGui_ImplVulkan_InitInfo initInfo{};
                    uint32_t minImageCount = 0;
                    VkRenderPass renderPass = VK_NULL_HANDLE;
                    if (!TryBuildVulkanImGuiInitInfo(backend, &initInfo, &minImageCount,
                                                     &renderPass))
                        return;

                    if (renderPass != g_vulkanImGuiState.renderPass) {
                        ImGui_ImplVulkan_Shutdown();
                        if (!ImGui_ImplVulkan_Init(&initInfo)) {
                            g_vulkanImGuiState.initialized = false;
                            g_vulkanImGuiState.minImageCount = 0;
                            g_vulkanImGuiState.renderPass = VK_NULL_HANDLE;
                            return;
                        }
                        g_vulkanImGuiState.renderPass = renderPass;
                        g_vulkanImGuiState.minImageCount = minImageCount;
                    } else if (minImageCount != g_vulkanImGuiState.minImageCount) {
                        ImGui_ImplVulkan_SetMinImageCount(minImageCount);
                        g_vulkanImGuiState.minImageCount = minImageCount;
                    }
                }

                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplGlfw_NewFrame();
#endif
                return;
            case Null:
                return;
        }
    }

    void RenderEditorImGuiDrawData(RenderBackendId backendId,
                                   ImDrawData *drawData) {
        if (!drawData)
            return;

        using enum RenderBackendId;
        switch (backendId) {
            case Auto:
            case OpenGL:
                ImGui_ImplOpenGL3_RenderDrawData(drawData);
                return;
            case Vulkan:
#if defined(HORO_HAS_VULKAN)
                if (!g_vulkanImGuiState.initialized)
                    return;
                if (!Renderer::IsFrameActive())
                    return;
                if (auto *backend = dynamic_cast<VulkanRenderBackend *>(
                    Renderer::GetBackendForInterop())) {
                    backend->QueueOverlayRenderCallback(&RenderVulkanImGuiDrawDataCallback,
                                                        drawData);
                }
#endif
                return;
            case Null:
                return;
        }
    }
} // namespace Horo::Editor
