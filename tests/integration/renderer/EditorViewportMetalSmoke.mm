#include "Horo/Runtime/Render/RenderFrontend.h"
#include "Horo/Runtime/Scene/PrimitiveMesh.h"
#include "editor/renderer/metal/EditorViewportRendererMetal.h"
#include "editor/renderer/metal/SdlMetalPresentationPort.h"
#include "runtime/renderer/modules/metal/MetalBackendModule.h"

#include <SDL3/SDL.h>

#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
using namespace Horo;
using namespace Horo::Editor;
using namespace Horo::Render;

void Check(const bool condition)
{
    if (!condition)
    {
        std::abort();
    }
}
} // namespace

int main()
{
    Check(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window =
        SDL_CreateWindow("Horo Metal viewport smoke", 256, 256, SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    Check(window != nullptr);

    SdlMetalPresentationPort presentationPort{*window};
    MetalEditorGraphicsBridge graphicsBridge;
    RenderBackendRegistry registry;
    Check(RegisterMetalRenderBackend(registry, presentationPort, graphicsBridge).HasValue());
    Check(registry.Seal().HasValue());
    auto frontendResult = RenderFrontend::Create(registry, RenderBackendId{"metal"},
                                                 RenderBackendConfig{.requirePresentation = true,
                                                                     .enableValidation = true,
                                                                     .maxFramesInFlight = 2,
                                                                     .presentMode = PresentMode::Immediate});
    if (frontendResult.HasError())
    {
        std::fprintf(stderr, "Metal frontend creation failed: %s\n", frontendResult.ErrorValue().message.c_str());
    }
    Check(frontendResult.HasValue());
    std::unique_ptr<RenderFrontend> frontend = std::move(frontendResult).Value();

    EditorViewportRendererMetal viewport{graphicsBridge};
    Check(viewport.Initialize().HasValue());
    Runtime::PrimitiveMeshCache meshCache;
    constexpr std::array primitiveTypes{Runtime::PrimitiveMeshType::Box,     Runtime::PrimitiveMeshType::Sphere,
                                        Runtime::PrimitiveMeshType::Capsule, Runtime::PrimitiveMeshType::Cylinder,
                                        Runtime::PrimitiveMeshType::Cone,    Runtime::PrimitiveMeshType::Plane,
                                        Runtime::PrimitiveMeshType::Quad};
    std::vector<Runtime::PrimitiveMeshLease> meshLeases;
    std::vector<EditorViewportMeshResourceView> meshResources;
    std::vector<EditorViewportInstance> viewportInstances;
    for (std::size_t index = 0; index < primitiveTypes.size(); ++index)
    {
        auto acquiredMesh = meshCache.Acquire(Runtime::PrimitiveMeshDescriptor::Defaults(primitiveTypes[index]));
        Check(acquiredMesh.HasValue());
        Runtime::PrimitiveMeshLease meshLease = std::move(acquiredMesh).Value();
        const Render::MeshData &mesh = meshLease.Data();
        const Render::RenderMeshHandle meshHandle{meshLease.Id(), 1};
        meshResources.push_back({meshHandle, mesh.vertices, mesh.indices, mesh.localBounds});
        constexpr std::array positions{Math::Vec2{0, 0},       Math::Vec2{-1.0F, 0.7F},  Math::Vec2{0, 0.9F},
                                       Math::Vec2{1.0F, 0.7F}, Math::Vec2{-1.0F, -0.7F}, Math::Vec2{0, -0.9F},
                                       Math::Vec2{1.0F, -0.7F}};
        const float scale = primitiveTypes[index] == Runtime::PrimitiveMeshType::Plane ? 0.08F
                            : index == 0                                               ? 0.65F
                                                                                       : 0.45F;
        viewportInstances.push_back(
            {meshHandle,
             Math::Transform{.translation = {positions[index].x, positions[index].y, 0}, .scale = {scale, scale, scale}}
                 .ToMatrix(),
             mesh.localBounds, Render::CoreDefaultMaterial,
             {.tint = {0.12F, 0.72F, 1.0F}, .tintStrength = index == 0 ? 0.65F : 0.0F}});
        meshLeases.push_back(std::move(meshLease));
    }
    const EditorViewportSceneView viewportScene{
        .camera = {}, .meshResources = meshResources, .instances = viewportInstances};
    Check(frontend->AttachStaticMeshPassExecutor(viewport).HasValue());
    auto viewportTargetResult = frontend->CreateOffscreenTarget({128, 128});
    Check(viewportTargetResult.HasValue());
    const RenderTargetHandle viewportTarget = viewportTargetResult.Value();
    Check(frontend->Resize(FramebufferExtent{256, 256}).HasValue());
    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 1, .outputExtent = {256, 256}});
    Check(begun.HasValue());
    RenderFrameScope frame = std::move(begun).Value();

    viewport.RequestExtent(EditorViewportExtent{128, 128});

    const std::array passes{
        RenderPassDescriptor{
            .id = RenderPassId{1},
            .kind = RenderPassKind::Graphics,
            .staticMesh = StaticMeshPassDescriptor{
                .target = viewportTarget,
                .extent = {128, 128},
                .scene = RenderSceneView{ToRenderCamera(viewportScene.camera), viewportScene.meshResources,
                                         viewportScene.instances},
            },
        },
        RenderPassDescriptor{
            .id = RenderPassId{2},
            .kind = RenderPassKind::Graphics,
            .primaryOutput =
                PrimaryOutputAttachment{
                    .loadOperation = AttachmentLoadOperation::Clear,
                    .storeOperation = AttachmentStoreOperation::Store,
                    .clearColor = ClearColor{0.02F, 0.03F, 0.05F, 1.0F},
                },
        },
    };
    Check(frame.Execute(passes).HasValue());
    Check(viewport.IsReady());
    const EditorViewportTextureView textureView = viewport.TextureView();
    Check(textureView.IsValid());
    Check(textureView.u0 == 0.0F && textureView.v0 == 0.0F);
    Check(textureView.u1 == 1.0F && textureView.v1 == 1.0F);
    Check(frame.Present().HasValue());
    frontend->DetachStaticMeshPassExecutor(viewport);
    Check(frontend->ReleaseOffscreenTarget(viewportTarget).HasValue());
    graphicsBridge.WaitUntilIdle();

    id<MTLTexture> texture = (__bridge id<MTLTexture>)(reinterpret_cast<void *>(textureView.textureId));
    Check(texture != nil);
    Check(texture.width == 128 && texture.height == 128);
    const std::size_t bytesPerRow = texture.width * 4U;
    const std::size_t byteCount = bytesPerRow * texture.height;
    id<MTLDevice> device = (__bridge id<MTLDevice>)graphicsBridge.Device();
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)graphicsBridge.CommandQueue();
    id<MTLBuffer> readback = [device newBufferWithLength:byteCount options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> readbackCommands = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [readbackCommands blitCommandEncoder];
    [blit copyFromTexture:texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(texture.width, texture.height, 1)
                        toBuffer:readback
               destinationOffset:0
          destinationBytesPerRow:bytesPerRow
        destinationBytesPerImage:byteCount];
    [blit endEncoding];
    [readbackCommands commit];
    [readbackCommands waitUntilCompleted];
    std::vector<std::uint8_t> rgba(byteCount);
    std::copy_n(static_cast<const std::uint8_t *>(readback.contents), byteCount, rgba.data());

    std::uint8_t minimum = 255;
    std::uint8_t maximum = 0;
    for (std::size_t index = 0; index < rgba.size(); index += 4)
    {
        minimum = std::min(minimum, std::min(rgba[index], std::min(rgba[index + 1], rgba[index + 2])));
        maximum = std::max(maximum, std::max(rgba[index], std::max(rgba[index + 1], rgba[index + 2])));
    }
    Check(maximum > minimum + 32);
    const std::size_t center = (static_cast<std::size_t>(texture.height / 2) * texture.width + texture.width / 2) * 4;
    Check(rgba[center] > 150 && rgba[center + 1] > 130 && rgba[center + 2] < 150);

    viewport.Shutdown();
    frontend.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
