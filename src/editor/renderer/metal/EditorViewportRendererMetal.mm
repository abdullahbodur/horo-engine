#include "EditorViewportRendererMetal.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>

namespace Horo::Editor
{
namespace
{
constexpr std::uint32_t maxViewportDimension = 8192;

struct MetalSelectionStyle
{
    Math::Vec3 color;
    float strength;
};
static_assert(sizeof(MetalSelectionStyle) == sizeof(float) * 4);

[[nodiscard]] Error MakeViewportError(const char *code, std::string message)
{
    return Error{.code = ErrorCode{code},
                 .domain = ErrorDomainId{"horo.editor.viewport.metal"},
                 .severity = ErrorSeverity::Error,
                 .message = std::move(message)};
}

[[nodiscard]] std::string ErrorMessage(NSString *prefix, NSError *error)
{
    std::string message{prefix.UTF8String};
    if (error != nil)
    {
        message += ": ";
        message += error.localizedDescription.UTF8String;
    }
    return message;
}

} // namespace

struct EditorViewportRendererMetal::Impl
{
    struct GpuMesh
    {
        __strong id<MTLBuffer> vertexBuffer{nil};
        __strong id<MTLBuffer> indexBuffer{nil};
        NSUInteger indexCount{0};
        std::uint32_t generation{0};
    };
    explicit Impl(Render::MetalEditorGraphicsBridge &borrowedGraphicsBridge) noexcept
        : graphicsBridge(&borrowedGraphicsBridge)
    {
    }

    Render::MetalEditorGraphicsBridge *graphicsBridge{nullptr};
    __strong id<MTLDevice> device{nil};
    __strong id<MTLLibrary> library{nil};
    __strong id<MTLRenderPipelineState> pipeline{nil};
    __strong id<MTLDepthStencilState> depthState{nil};
    std::unordered_map<std::uint64_t, GpuMesh> meshes;
    __strong id<MTLTexture> colorTexture{nil};
    __strong id<MTLTexture> depthTexture{nil};
    std::array<__strong id<MTLTexture>, 3> retiredColorTextures{};
    std::size_t nextRetiredTexture{0};
    EditorViewportExtent requestedExtent{};
    EditorViewportExtent allocatedExtent{};
    Render::RenderTargetHandle targetHandle{};
    bool initialized{false};
};

/** @copydoc EditorViewportRendererMetal::EditorViewportRendererMetal */
EditorViewportRendererMetal::EditorViewportRendererMetal(Render::MetalEditorGraphicsBridge &graphicsBridge) noexcept
    : impl_(std::make_unique<Impl>(graphicsBridge))
{
}

/** @copydoc EditorViewportRendererMetal::~EditorViewportRendererMetal */
EditorViewportRendererMetal::~EditorViewportRendererMetal()
{
    Shutdown();
}

/** @copydoc EditorViewportRendererMetal::Initialize */
Result<void> EditorViewportRendererMetal::Initialize()
{
    if (impl_->initialized)
    {
        return Result<void>::Failure(MakeViewportError("editor.viewport.already_initialized",
                                                       "Editor viewport renderer is already initialized."));
    }
    impl_->device = (__bridge id<MTLDevice>)impl_->graphicsBridge->Device();
    if (impl_->device == nil)
    {
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.metal_device_unavailable", "Metal presentation device is unavailable."));
    }

    static NSString *shaderSource = @R"metal(
#include <metal_stdlib>
using namespace metal;
struct Vertex { packed_float3 position; packed_float3 normal; packed_float2 uv; };
struct SelectionStyle { packed_float3 color; float strength; };
struct VertexOut { float4 position [[position]]; float3 color; };
vertex VertexOut viewport_vertex(uint vertexId [[vertex_id]],
                                 const device Vertex* vertices [[buffer(0)]],
                                 constant float4x4& mvp [[buffer(1)]])
{
    VertexOut output;
    output.position = mvp * float4(vertices[vertexId].position, 1.0);
    float light = 0.55 + 0.45 * max(dot(normalize(float3(vertices[vertexId].normal)),
                                      normalize(float3(0.4, 0.8, 0.3))), 0.0);
    output.color = float3(0.62, 0.67, 0.74) * light;
    return output;
}
fragment float4 viewport_fragment(VertexOut input [[stage_in]],
                                  constant SelectionStyle& selection [[buffer(0)]])
{
    return float4(mix(input.color, float3(selection.color), selection.strength), 1.0);
}
)metal";

    NSError *error = nil;
    impl_->library = [impl_->device newLibraryWithSource:shaderSource options:nil error:&error];
    if (impl_->library == nil)
    {
        Shutdown();
        return Result<void>::Failure(MakeViewportError(
            "editor.viewport.shader_compile_failed", ErrorMessage(@"Metal viewport shader compilation failed", error)));
    }

    id<MTLFunction> vertexFunction = [impl_->library newFunctionWithName:@"viewport_vertex"];
    id<MTLFunction> fragmentFunction = [impl_->library newFunctionWithName:@"viewport_fragment"];
    MTLRenderPipelineDescriptor *pipelineDescriptor = [MTLRenderPipelineDescriptor new];
    pipelineDescriptor.label = @"Horo Editor Viewport";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    impl_->pipeline = [impl_->device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (impl_->pipeline == nil)
    {
        Shutdown();
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.pipeline_creation_failed",
                              ErrorMessage(@"Metal viewport pipeline creation failed", error)));
    }

    MTLDepthStencilDescriptor *depthDescriptor = [MTLDepthStencilDescriptor new];
    depthDescriptor.depthCompareFunction = MTLCompareFunctionLess;
    depthDescriptor.depthWriteEnabled = YES;
    impl_->depthState = [impl_->device newDepthStencilStateWithDescriptor:depthDescriptor];

    if (impl_->depthState == nil)
    {
        Shutdown();
        return Result<void>::Failure(MakeViewportError("editor.viewport.resource_creation_failed",
                                                       "Failed to create Metal viewport geometry or depth state."));
    }

    impl_->initialized = true;
    return Result<void>::Success();
}

/** @copydoc EditorViewportRendererMetal::Shutdown */
void EditorViewportRendererMetal::Shutdown() noexcept
{
    impl_->graphicsBridge->WaitUntilIdle();
    impl_->depthTexture = nil;
    impl_->colorTexture = nil;
    impl_->retiredColorTextures = {};
    impl_->nextRetiredTexture = 0;
    impl_->meshes.clear();
    impl_->depthState = nil;
    impl_->pipeline = nil;
    impl_->library = nil;
    impl_->device = nil;
    impl_->requestedExtent = {};
    impl_->allocatedExtent = {};
    impl_->targetHandle = {};
    impl_->initialized = false;
}

/** @copydoc EditorViewportRendererMetal::RequestExtent */
void EditorViewportRendererMetal::RequestExtent(const EditorViewportExtent extent) noexcept
{
    impl_->requestedExtent.width = std::min(extent.width, maxViewportDimension);
    impl_->requestedExtent.height = std::min(extent.height, maxViewportDimension);
}

/** @copydoc EditorViewportRendererMetal::RequestedExtent */
EditorViewportExtent EditorViewportRendererMetal::RequestedExtent() const noexcept
{
    return impl_->requestedExtent;
}

/** @copydoc EditorViewportRendererMetal::ExecuteStaticMeshPass */
Result<void> EditorViewportRendererMetal::ExecuteStaticMeshPass(const Render::StaticMeshPassDescriptor &descriptor)
{
    if (!impl_->initialized)
    {
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.not_initialized", "Viewport renderer is not initialized."));
    }
    const EditorViewportExtent requestedExtent = std::exchange(impl_->requestedExtent, {});
    if (!requestedExtent.IsValid())
    {
        return Result<void>::Success();
    }
    if (!descriptor.IsValid() || descriptor.extent.width != requestedExtent.width ||
        descriptor.extent.height != requestedExtent.height)
    {
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.invalid_scene", "Editor viewport scene data is invalid."));
    }
    if (impl_->targetHandle.IsValid() && impl_->targetHandle != descriptor.target)
    {
        return Result<void>::Failure(
            MakeViewportError("editor.viewport.stale_target", "Viewport pass references a stale render target."));
    }
    impl_->targetHandle = descriptor.target;

    if (requestedExtent.width != impl_->allocatedExtent.width ||
        requestedExtent.height != impl_->allocatedExtent.height)
    {
        MTLTextureDescriptor *colorDescriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:requestedExtent.width
                                                              height:requestedExtent.height
                                                           mipmapped:NO];
        colorDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDescriptor.storageMode = MTLStorageModePrivate;
        id<MTLTexture> colorTexture = [impl_->device newTextureWithDescriptor:colorDescriptor];

        MTLTextureDescriptor *depthDescriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:requestedExtent.width
                                                              height:requestedExtent.height
                                                           mipmapped:NO];
        depthDescriptor.usage = MTLTextureUsageRenderTarget;
        depthDescriptor.storageMode = MTLStorageModePrivate;
        id<MTLTexture> depthTexture = [impl_->device newTextureWithDescriptor:depthDescriptor];
        if (colorTexture == nil || depthTexture == nil)
        {
            return Result<void>::Failure(MakeViewportError("editor.viewport.target_creation_failed",
                                                           "Failed to create Metal viewport render targets."));
        }
        impl_->retiredColorTextures[impl_->nextRetiredTexture] = impl_->colorTexture;
        impl_->nextRetiredTexture = (impl_->nextRetiredTexture + 1U) % impl_->retiredColorTextures.size();
        impl_->colorTexture = colorTexture;
        impl_->depthTexture = depthTexture;
        impl_->allocatedExtent = requestedExtent;
    }

    id<MTLCommandBuffer> commandBuffer = (__bridge id<MTLCommandBuffer>)impl_->graphicsBridge->CurrentCommandBuffer();
    if (commandBuffer == nil)
    {
        return Result<void>::Failure(MakeViewportError("editor.viewport.no_active_frame",
                                                       "Metal viewport rendering requires an active renderer frame."));
    }

    for (const Render::RenderMeshResourceView &resource : descriptor.scene.meshResources)
    {
        if (auto existing = impl_->meshes.find(resource.handle.id.value); existing != impl_->meshes.end())
        {
            if (existing->second.generation == resource.handle.generation)
                continue;
            id<MTLBuffer> retiredVertex = existing->second.vertexBuffer;
            id<MTLBuffer> retiredIndex = existing->second.indexBuffer;
            [commandBuffer addCompletedHandler:^(__unused id<MTLCommandBuffer> completed) {
              (void)retiredVertex;
              (void)retiredIndex;
            }];
            impl_->meshes.erase(existing);
        }
        Impl::GpuMesh mesh;
        mesh.generation = resource.handle.generation;
        mesh.vertexBuffer = [impl_->device newBufferWithBytes:resource.vertices.data()
                                                       length:resource.vertices.size_bytes()
                                                      options:MTLResourceStorageModeShared];
        mesh.indexBuffer = [impl_->device newBufferWithBytes:resource.indices.data()
                                                      length:resource.indices.size_bytes()
                                                     options:MTLResourceStorageModeShared];
        mesh.indexCount = resource.indices.size();
        if (mesh.vertexBuffer == nil || mesh.indexBuffer == nil)
            return Result<void>::Failure(MakeViewportError("editor.viewport.resource_creation_failed",
                                                           "Failed to upload a Metal viewport mesh resource."));
        impl_->meshes.emplace(resource.handle.id.value, std::move(mesh));
    }
    for (auto mesh = impl_->meshes.begin(); mesh != impl_->meshes.end();)
    {
        const bool present =
            std::ranges::any_of(descriptor.scene.meshResources, [&](const Render::RenderMeshResourceView &resource) {
                return resource.handle.id.value == mesh->first &&
                       resource.handle.generation == mesh->second.generation;
            });
        if (!present)
        {
            id<MTLBuffer> retiredVertex = mesh->second.vertexBuffer;
            id<MTLBuffer> retiredIndex = mesh->second.indexBuffer;
            [commandBuffer addCompletedHandler:^(__unused id<MTLCommandBuffer> completed) {
              (void)retiredVertex;
              (void)retiredIndex;
            }];
            mesh = impl_->meshes.erase(mesh);
        }
        else
            ++mesh;
    }

    const float aspect =
        static_cast<float>(impl_->allocatedExtent.width) / static_cast<float>(impl_->allocatedExtent.height);

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = impl_->colorTexture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(
        descriptor.clearColor.red, descriptor.clearColor.green, descriptor.clearColor.blue,
        descriptor.clearColor.alpha);
    pass.depthAttachment.texture = impl_->depthTexture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (encoder == nil)
    {
        return Result<void>::Failure(MakeViewportError("editor.viewport.encoder_creation_failed",
                                                       "Failed to create the Metal viewport render encoder."));
    }
    [encoder pushDebugGroup:@"Horo Editor Viewport"];
    [encoder setRenderPipelineState:impl_->pipeline];
    [encoder setDepthStencilState:impl_->depthState];
    [encoder setCullMode:MTLCullModeNone];
    for (const Render::RenderStaticMeshInstance &instance : descriptor.scene.instances)
    {
        const auto mesh = impl_->meshes.find(instance.mesh.id.value);
        if (mesh == impl_->meshes.end())
        {
            [encoder endEncoding];
            return Result<void>::Failure(MakeViewportError("editor.viewport.stale_mesh_resource",
                                                           "Viewport instance references a stale mesh resource."));
        }
        [encoder setVertexBuffer:mesh->second.vertexBuffer offset:0 atIndex:0];
        const Result<Math::Mat4> mvp = BuildRenderMvp(descriptor.scene.camera, instance.localToWorld, aspect,
                                                     Math::ClipDepthRange::ZeroToOne);
        if (mvp.HasError())
        {
            [encoder endEncoding];
            return Result<void>::Failure(mvp.ErrorValue());
        }
        const MetalSelectionStyle selectionStyle{instance.presentation.tint, instance.presentation.tintStrength};
        [encoder setVertexBytes:mvp.Value().values.data() length:sizeof(mvp.Value().values) atIndex:1];
        [encoder setFragmentBytes:&selectionStyle length:sizeof(selectionStyle) atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:mesh->second.indexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:mesh->second.indexBuffer
                     indexBufferOffset:0];
    }
    [encoder popDebugGroup];
    [encoder endEncoding];
    return Result<void>::Success();
}

/** @copydoc EditorViewportRendererMetal::TextureView */
EditorViewportTextureView EditorViewportRendererMetal::TextureView() const noexcept
{
    return EditorViewportTextureView{
        .textureId = reinterpret_cast<std::uintptr_t>((__bridge void *)impl_->colorTexture),
        .u0 = 0.0F,
        .v0 = 0.0F,
        .u1 = 1.0F,
        .v1 = 1.0F,
    };
}

/** @copydoc EditorViewportRendererMetal::IsReady */
bool EditorViewportRendererMetal::IsReady() const noexcept
{
    return impl_->initialized && impl_->colorTexture != nil && impl_->allocatedExtent.IsValid();
}
} // namespace Horo::Editor
