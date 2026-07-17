#include "EditorGuiRendererMetal.h"

#include <imgui.h>
#include <imgui_impl_metal.h>
#include <imgui_impl_sdl3.h>

#import <Metal/Metal.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] Error MakeGuiRendererError(const char *code, std::string message)
{
    return Error{.code = ErrorCode{code},
                 .domain = ErrorDomainId{"horo.editor.gui.metal"},
                 .severity = ErrorSeverity::Error,
                 .message = std::move(message)};
}
} // namespace

struct EditorGuiRendererMetal::Impl
{
    Impl(SDL_Window &borrowedWindow, Render::MetalEditorGraphicsBridge &borrowedGraphicsBridge) noexcept
        : window(&borrowedWindow), graphicsBridge(&borrowedGraphicsBridge)
    {
    }

    SDL_Window *window{nullptr};
    Render::MetalEditorGraphicsBridge *graphicsBridge{nullptr};
    __strong id<MTLDevice> device{nil};
    std::vector<__strong id<MTLTexture>> textures;
    bool platformInitialized{false};
    bool rendererInitialized{false};
};

/** @copydoc EditorGuiRendererMetal::EditorGuiRendererMetal */
EditorGuiRendererMetal::EditorGuiRendererMetal(SDL_Window &window,
                                               Render::MetalEditorGraphicsBridge &graphicsBridge) noexcept
    : impl_(std::make_unique<Impl>(window, graphicsBridge))
{
}

/** @copydoc EditorGuiRendererMetal::~EditorGuiRendererMetal */
EditorGuiRendererMetal::~EditorGuiRendererMetal()
{
    Shutdown();
}

/** @copydoc EditorGuiRendererMetal::Initialize */
Result<void> EditorGuiRendererMetal::Initialize()
{
    if (impl_->platformInitialized || impl_->rendererInitialized)
    {
        return Result<void>::Failure(
            MakeGuiRendererError("editor.gui.metal.invalid_state", "Metal GUI renderer is already initialized."));
    }
    impl_->device = (__bridge id<MTLDevice>)impl_->graphicsBridge->Device();
    if (impl_->device == nil)
    {
        return Result<void>::Failure(
            MakeGuiRendererError("editor.gui.metal.device_unavailable", "Metal GUI renderer device is unavailable."));
    }

    impl_->platformInitialized = ImGui_ImplSDL3_InitForMetal(impl_->window);
    impl_->rendererInitialized = impl_->platformInitialized && ImGui_ImplMetal_Init(impl_->device);
    if (!impl_->rendererInitialized)
    {
        Shutdown();
        return Result<void>::Failure(MakeGuiRendererError("editor.gui.metal.initialization_failed",
                                                          "Failed to initialize Dear ImGui SDL3/Metal bridges."));
    }
    return Result<void>::Success();
}

/** @copydoc EditorGuiRendererMetal::BeginFrame */
Result<void> EditorGuiRendererMetal::BeginFrame()
{
    if (!impl_->rendererInitialized)
    {
        return Result<void>::Failure(
            MakeGuiRendererError("editor.gui.metal.not_initialized", "Metal GUI renderer is not initialized."));
    }
    MTLRenderPassDescriptor *pass =
        (__bridge MTLRenderPassDescriptor *)impl_->graphicsBridge->CurrentRenderPassDescriptor();
    if (pass == nil)
    {
        return Result<void>::Failure(MakeGuiRendererError("editor.gui.metal.no_active_frame",
                                                          "Metal GUI frame requires an active renderer frame."));
    }
    ImGui_ImplMetal_NewFrame(pass);
    ImGui_ImplSDL3_NewFrame();
    return Result<void>::Success();
}

/** @copydoc EditorGuiRendererMetal::RenderDrawData */
Result<void> EditorGuiRendererMetal::RenderDrawData()
{
    if (!impl_->rendererInitialized)
    {
        return Result<void>::Failure(
            MakeGuiRendererError("editor.gui.metal.not_initialized", "Metal GUI renderer is not initialized."));
    }
    id<MTLCommandBuffer> commandBuffer = (__bridge id<MTLCommandBuffer>)impl_->graphicsBridge->CurrentCommandBuffer();
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)impl_->graphicsBridge->CurrentRenderEncoder();
    if (commandBuffer == nil || encoder == nil)
    {
        return Result<void>::Failure(MakeGuiRendererError("editor.gui.metal.no_primary_encoder",
                                                          "Metal GUI rendering requires an active primary encoder."));
    }
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, encoder);
    return Result<void>::Success();
}

/** @copydoc EditorGuiRendererMetal::CreateTexture */
Result<std::uintptr_t> EditorGuiRendererMetal::CreateTexture(const EditorRgba8ImageView &image)
{
    if (!impl_->rendererInitialized || !image.IsValid())
    {
        return Result<std::uintptr_t>::Failure(
            MakeGuiRendererError("editor.gui.metal.invalid_texture", "Metal GUI texture upload is invalid."));
    }
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                          width:image.width
                                                                                         height:image.height
                                                                                      mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [impl_->device newTextureWithDescriptor:descriptor];
    if (texture == nil)
    {
        return Result<std::uintptr_t>::Failure(
            MakeGuiRendererError("editor.gui.metal.texture_creation_failed", "Failed to create Metal GUI texture."));
    }
    [texture replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
               mipmapLevel:0
                 withBytes:image.pixels.data()
               bytesPerRow:static_cast<NSUInteger>(image.width) * 4U];
    impl_->textures.push_back(texture);
    return Result<std::uintptr_t>::Success(reinterpret_cast<std::uintptr_t>((__bridge void *)texture));
}

/** @copydoc EditorGuiRendererMetal::DestroyTexture */
void EditorGuiRendererMetal::DestroyTexture(const std::uintptr_t textureId) noexcept
{
    const void *target = reinterpret_cast<const void *>(textureId);
    const auto found = std::find_if(impl_->textures.begin(), impl_->textures.end(), [target](id<MTLTexture> texture) {
        return (__bridge const void *)texture == target;
    });
    if (found != impl_->textures.end())
    {
        impl_->textures.erase(found);
    }
}

/** @copydoc EditorGuiRendererMetal::Shutdown */
void EditorGuiRendererMetal::Shutdown() noexcept
{
    impl_->graphicsBridge->WaitUntilIdle();
    impl_->textures.clear();
    if (impl_->rendererInitialized)
    {
        ImGui_ImplMetal_Shutdown();
        impl_->rendererInitialized = false;
    }
    if (impl_->platformInitialized)
    {
        ImGui_ImplSDL3_Shutdown();
        impl_->platformInitialized = false;
    }
    impl_->device = nil;
}
} // namespace Horo::Editor
