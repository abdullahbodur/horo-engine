#include "EditorViewportRendererMetal.h"

#include "editor/renderer/grid/EditorViewportGridGeometry.h"
#include "editor/screens/workspace/panels/viewport/visualizers/light/LightVisualizerGeometry.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

namespace Horo::Editor {
    namespace {
        constexpr std::uint32_t maxViewportDimension = 8192;

        struct MetalSelectionStyle {
            Math::Vec3 color;
            float strength;
        };

        static_assert(sizeof(MetalSelectionStyle) == sizeof(float) * 4);

        struct MetalLight {
            Math::Vec4 positionKind;
            Math::Vec4 directionRange;
            Math::Vec4 colorIntensity;
            Math::Vec4 cone;
        };

        static_assert(sizeof(MetalLight) == sizeof(float) * 16);

        struct MetalSceneLighting {
            std::array<MetalLight, Render::MaximumForwardLights> lights{};
            Math::Vec3 cameraPosition{};
            std::uint32_t lightCount{0};
            std::uint32_t shadowEnabled{0};
            std::uint32_t shadowLightIndex{0};
            Math::Vec2 padding{};
        };

        static_assert(sizeof(MetalSceneLighting) == sizeof(MetalLight) * Render::MaximumForwardLights + sizeof(float) * 8);

        struct MetalObjectTransforms {
            Math::Mat4 mvp;
            Math::Mat4 model;
            Math::Mat4 shadowMvp;
        };

        struct MetalGridStyle {
            Math::Vec3 color;
            float padding{0.0F};
        };

        static_assert(sizeof(MetalGridStyle) == sizeof(float) * 4);

        [[nodiscard]] Error MakeViewportError(const char *code, std::string message) {
            return Error{.code = ErrorCode{code},
                         .domain = ErrorDomainId{"horo.editor.viewport.metal"},
                         .severity = ErrorSeverity::Error,
                         .message = std::move(message)};
        }

        [[nodiscard]] std::string ErrorMessage(NSString *prefix, NSError *error) {
            std::string message{prefix.UTF8String};
            if (error != nil) {
                message += ": ";
                message += error.localizedDescription.UTF8String;
            }
            return message;
        }

    }  // namespace

    struct EditorViewportRendererMetal::Impl {
        struct GpuMesh {
            __strong id<MTLBuffer> vertexBuffer{nil};
            __strong id<MTLBuffer> indexBuffer{nil};
            NSUInteger indexCount{0};
            std::uint32_t generation{0};
        };

        explicit Impl(Render::MetalEditorGraphicsBridge &borrowedGraphicsBridge) noexcept : graphicsBridge(&borrowedGraphicsBridge) {}

        Render::MetalEditorGraphicsBridge *graphicsBridge{nullptr};
        __strong id<MTLDevice> device{nil};
        __strong id<MTLLibrary> library{nil};
        __strong id<MTLRenderPipelineState> pipeline{nil};
        __strong id<MTLRenderPipelineState> gridPipeline{nil};
        __strong id<MTLRenderPipelineState> shadowPipeline{nil};
        __strong id<MTLDepthStencilState> depthState{nil};
        __strong id<MTLDepthStencilState> gridDepthState{nil};
        __strong id<MTLSamplerState> shadowSampler{nil};
        __strong id<MTLBuffer> gridVertexBuffer{nil};
        std::unordered_map<std::uint64_t, GpuMesh> meshes;
        __strong id<MTLTexture> colorTexture{nil};
        __strong id<MTLTexture> depthTexture{nil};
        __strong id<MTLTexture> shadowDepthTexture{nil};
        std::array<__strong id<MTLTexture>, 3> retiredColorTextures{};
        std::size_t nextRetiredTexture{0};
        EditorViewportExtent requestedExtent{};
        EditorViewportExtent allocatedExtent{};
        EditorViewportGridOptions gridOptions{};
        EditorViewportLightVisualizerOptions lightVisualizerOptions{};
        Render::RenderTargetHandle targetHandle{};
        bool initialized{false};
    };

    /** @copydoc EditorViewportRendererMetal::EditorViewportRendererMetal */
    EditorViewportRendererMetal::EditorViewportRendererMetal(Render::MetalEditorGraphicsBridge &graphicsBridge) noexcept
        : impl_(std::make_unique<Impl>(graphicsBridge)) {}

    /** @copydoc EditorViewportRendererMetal::~EditorViewportRendererMetal */
    EditorViewportRendererMetal::~EditorViewportRendererMetal() {
        Shutdown();
    }

    /** @copydoc EditorViewportRendererMetal::Initialize */
    Result<void> EditorViewportRendererMetal::Initialize() {
        if (impl_->initialized) {
            return Result<void>::Failure(
                MakeViewportError("editor.viewport.already_initialized", "Editor viewport renderer is already initialized."));
        }
        impl_->device = (__bridge id<MTLDevice>)impl_->graphicsBridge->Device();
        if (impl_->device == nil) {
            return Result<void>::Failure(
                MakeViewportError("editor.viewport.metal_device_unavailable", "Metal presentation device is unavailable."));
        }

        static NSString *shaderSource = @R"metal(
#include <metal_stdlib>
using namespace metal;
struct Vertex { packed_float3 position; packed_float3 normal; packed_float2 uv; };
struct SelectionStyle { packed_float3 color; float strength; };
struct Light { float4 positionKind; float4 directionRange; float4 colorIntensity; float4 cone; };
struct SceneLighting {
    Light lights[16];
    packed_float3 cameraPosition;
    uint lightCount;
    uint shadowEnabled;
    uint shadowLightIndex;
    float2 padding;
};
struct ObjectTransforms { float4x4 mvp; float4x4 model; float4x4 shadowMvp; };
struct VertexOut {
    float4 position [[position]];
    float3 worldPosition;
    float3 worldNormal;
    float4 shadowPosition;
};
struct GridVertexOut { float4 position [[position]]; };
vertex VertexOut viewport_vertex(uint vertexId [[vertex_id]],
                                 const device Vertex* vertices [[buffer(0)]],
                                 constant ObjectTransforms& transforms [[buffer(1)]])
{
    VertexOut output;
    float4 worldPosition = transforms.model * float4(vertices[vertexId].position, 1.0);
    float3x3 model3x3 = float3x3(transforms.model[0].xyz, transforms.model[1].xyz,
                                transforms.model[2].xyz);
    float3 inverseRow0 = cross(model3x3[1], model3x3[2]);
    float3 inverseRow1 = cross(model3x3[2], model3x3[0]);
    float3 inverseRow2 = cross(model3x3[0], model3x3[1]);
    float determinant = dot(model3x3[0], inverseRow0);
    float inverseDeterminant = abs(determinant) > 0.0000001 ? 1.0 / determinant : 1.0;
    float3x3 normalMatrix = float3x3(inverseRow0 * inverseDeterminant,
                                     inverseRow1 * inverseDeterminant,
                                     inverseRow2 * inverseDeterminant);
    output.position = transforms.mvp * float4(vertices[vertexId].position, 1.0);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(normalMatrix * float3(vertices[vertexId].normal));
    output.shadowPosition = transforms.shadowMvp * float4(vertices[vertexId].position, 1.0);
    return output;
}
vertex float4 viewport_shadow_vertex(uint vertexId [[vertex_id]],
                                     const device Vertex* vertices [[buffer(0)]],
                                     constant float4x4& shadowMvp [[buffer(1)]])
{
    return shadowMvp * float4(vertices[vertexId].position, 1.0);
}
vertex GridVertexOut viewport_grid_vertex(uint vertexId [[vertex_id]],
                                          const device packed_float3* vertices [[buffer(0)]],
                                          constant float4x4& viewProjection [[buffer(1)]])
{
    GridVertexOut output;
    output.position = viewProjection * float4(float3(vertices[vertexId]), 1.0);
    return output;
}
fragment float4 viewport_fragment(VertexOut input [[stage_in]],
                                  constant SelectionStyle& selection [[buffer(0)]],
                                  constant SceneLighting& scene [[buffer(1)]],
                                  depth2d<float> shadowMap [[texture(0)]],
                                  sampler shadowSampler [[sampler(0)]])
{
    float3 normal = normalize(input.worldNormal);
    float3 viewDirection = normalize(float3(scene.cameraPosition) - input.worldPosition);
    float3 baseColor = float3(0.68, 0.70, 0.74);
    float3 lighting = baseColor * 0.08;
    for (uint index = 0; index < scene.lightCount; ++index)
    {
        constant Light& light = scene.lights[index];
        uint kind = uint(light.positionKind.w + 0.5);
        float3 lightDirection;
        float attenuation = 1.0;
        if (kind == 0)
        {
            lightDirection = normalize(-light.directionRange.xyz);
        }
        else
        {
            float3 toLight = light.positionKind.xyz - input.worldPosition;
            float distanceToLight = length(toLight);
            lightDirection = distanceToLight > 0.0001 ? toLight / distanceToLight : normal;
            float range = max(light.directionRange.w, 0.0001);
            float normalizedDistance = distanceToLight / range;
            float rangeFade = max(1.0 - normalizedDistance * normalizedDistance, 0.0);
            attenuation = rangeFade * rangeFade;
            if (kind == 2)
            {
                float coneCosine = dot(normalize(light.directionRange.xyz), -lightDirection);
                attenuation *= smoothstep(light.cone.y, light.cone.x, coneCosine);
            }
        }
        float diffuse = max(dot(normal, lightDirection), 0.0);
        float3 halfDirection = normalize(lightDirection + viewDirection);
        float specular = pow(max(dot(normal, halfDirection), 0.0), 48.0) * 0.18;
        float3 radiance = light.colorIntensity.rgb * light.colorIntensity.a * attenuation;
        float visibility = 1.0;
        if (scene.shadowEnabled != 0 && index == scene.shadowLightIndex)
        {
            float3 projected = input.shadowPosition.xyz / input.shadowPosition.w;
            float3 shadowCoordinate = float3(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5, projected.z);
            if (shadowCoordinate.x > 0.0 && shadowCoordinate.x < 1.0 &&
                shadowCoordinate.y > 0.0 && shadowCoordinate.y < 1.0 &&
                shadowCoordinate.z > 0.0 && shadowCoordinate.z < 1.0)
            {
                float2 texel = 1.0 / float2(shadowMap.get_width(), shadowMap.get_height());
                float bias = max(0.0025 * (1.0 - dot(normal, lightDirection)), 0.00045);
                visibility = 0.0;
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x)
                    {
                        float closestDepth = shadowMap.sample(shadowSampler,
                            shadowCoordinate.xy + float2(x, y) * texel);
                        visibility += shadowCoordinate.z - bias <= closestDepth ? 1.0 : 0.0;
                    }
                visibility /= 9.0;
            }
        }
        lighting += radiance * (baseColor * diffuse + specular) * visibility;
    }
    float3 mapped = lighting / (lighting + 1.0);
    float3 displayColor = pow(max(mapped, 0.0), float3(1.0 / 2.2));
    return float4(mix(displayColor, float3(selection.color), selection.strength), 1.0);
}
fragment float4 viewport_grid_fragment(GridVertexOut input [[stage_in]],
                                       constant SelectionStyle& style [[buffer(0)]])
{
    (void)input;
    return float4(style.color, 1.0);
}
)metal";

        NSError *error = nil;
        impl_->library = [impl_->device newLibraryWithSource:shaderSource options:nil error:&error];
        if (impl_->library == nil) {
            Shutdown();
            return Result<void>::Failure(MakeViewportError("editor.viewport.shader_compile_failed",
                                                           ErrorMessage(@"Metal viewport shader compilation failed", error)));
        }

        id<MTLFunction> vertexFunction = [impl_->library newFunctionWithName:@"viewport_vertex"];
        id<MTLFunction> fragmentFunction = [impl_->library newFunctionWithName:@"viewport_fragment"];
        id<MTLFunction> gridVertexFunction = [impl_->library newFunctionWithName:@"viewport_grid_vertex"];
        id<MTLFunction> gridFragmentFunction = [impl_->library newFunctionWithName:@"viewport_grid_fragment"];
        id<MTLFunction> shadowVertexFunction = [impl_->library newFunctionWithName:@"viewport_shadow_vertex"];
        MTLRenderPipelineDescriptor *pipelineDescriptor = [MTLRenderPipelineDescriptor new];
        pipelineDescriptor.label = @"Horo Editor Viewport";
        pipelineDescriptor.vertexFunction = vertexFunction;
        pipelineDescriptor.fragmentFunction = fragmentFunction;
        pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        impl_->pipeline = [impl_->device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
        if (impl_->pipeline == nil) {
            Shutdown();
            return Result<void>::Failure(MakeViewportError("editor.viewport.pipeline_creation_failed",
                                                           ErrorMessage(@"Metal viewport pipeline creation failed", error)));
        }

        MTLRenderPipelineDescriptor *gridPipelineDescriptor = [MTLRenderPipelineDescriptor new];
        gridPipelineDescriptor.label = @"Horo Editor Viewport Grid";
        gridPipelineDescriptor.vertexFunction = gridVertexFunction;
        gridPipelineDescriptor.fragmentFunction = gridFragmentFunction;
        gridPipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        gridPipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        impl_->gridPipeline = [impl_->device newRenderPipelineStateWithDescriptor:gridPipelineDescriptor error:&error];
        if (impl_->gridPipeline == nil) {
            Shutdown();
            return Result<void>::Failure(MakeViewportError("editor.viewport.grid_pipeline_creation_failed",
                                                           ErrorMessage(@"Metal viewport grid pipeline creation failed", error)));
        }

        MTLRenderPipelineDescriptor *shadowPipelineDescriptor = [MTLRenderPipelineDescriptor new];
        shadowPipelineDescriptor.label = @"Horo Editor Directional Shadow";
        shadowPipelineDescriptor.vertexFunction = shadowVertexFunction;
        shadowPipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        impl_->shadowPipeline = [impl_->device newRenderPipelineStateWithDescriptor:shadowPipelineDescriptor error:&error];
        if (impl_->shadowPipeline == nil) {
            Shutdown();
            return Result<void>::Failure(MakeViewportError("editor.viewport.shadow_pipeline_creation_failed",
                                                           ErrorMessage(@"Metal viewport shadow pipeline creation failed", error)));
        }

        MTLDepthStencilDescriptor *depthDescriptor = [MTLDepthStencilDescriptor new];
        depthDescriptor.depthCompareFunction = MTLCompareFunctionLess;
        depthDescriptor.depthWriteEnabled = YES;
        impl_->depthState = [impl_->device newDepthStencilStateWithDescriptor:depthDescriptor];

        MTLDepthStencilDescriptor *gridDepthDescriptor = [MTLDepthStencilDescriptor new];
        gridDepthDescriptor.depthCompareFunction = MTLCompareFunctionLess;
        gridDepthDescriptor.depthWriteEnabled = NO;
        impl_->gridDepthState = [impl_->device newDepthStencilStateWithDescriptor:gridDepthDescriptor];
        constexpr NSUInteger gridBufferSize = sizeof(Math::Vec3) * (ViewportGridGeometry::MaxRegularVertices + 4);
        impl_->gridVertexBuffer = [impl_->device newBufferWithLength:gridBufferSize options:MTLResourceStorageModeShared];

        MTLSamplerDescriptor *shadowSamplerDescriptor = [MTLSamplerDescriptor new];
        shadowSamplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
        shadowSamplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
        shadowSamplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToBorderColor;
        shadowSamplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToBorderColor;
        shadowSamplerDescriptor.borderColor = MTLSamplerBorderColorOpaqueWhite;
        impl_->shadowSampler = [impl_->device newSamplerStateWithDescriptor:shadowSamplerDescriptor];

        constexpr NSUInteger shadowMapResolution = EditorViewportDirectionalShadowMapResolution;
        MTLTextureDescriptor *shadowTextureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                                           width:shadowMapResolution
                                                                                                          height:shadowMapResolution
                                                                                                       mipmapped:NO];
        shadowTextureDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        shadowTextureDescriptor.storageMode = MTLStorageModePrivate;
        impl_->shadowDepthTexture = [impl_->device newTextureWithDescriptor:shadowTextureDescriptor];

        if (impl_->depthState == nil || impl_->gridDepthState == nil || impl_->gridVertexBuffer == nil || impl_->shadowSampler == nil ||
            impl_->shadowDepthTexture == nil) {
            Shutdown();
            return Result<void>::Failure(
                MakeViewportError("editor.viewport.resource_creation_failed", "Failed to create Metal viewport geometry or depth state."));
        }

        impl_->initialized = true;
        return Result<void>::Success();
    }

    /** @copydoc EditorViewportRendererMetal::Shutdown */
    void EditorViewportRendererMetal::Shutdown() noexcept {
        impl_->graphicsBridge->WaitUntilIdle();
        impl_->depthTexture = nil;
        impl_->shadowDepthTexture = nil;
        impl_->colorTexture = nil;
        impl_->retiredColorTextures = {};
        impl_->nextRetiredTexture = 0;
        impl_->meshes.clear();
        impl_->gridVertexBuffer = nil;
        impl_->gridDepthState = nil;
        impl_->depthState = nil;
        impl_->shadowSampler = nil;
        impl_->shadowPipeline = nil;
        impl_->gridPipeline = nil;
        impl_->pipeline = nil;
        impl_->library = nil;
        impl_->device = nil;
        impl_->requestedExtent = {};
        impl_->allocatedExtent = {};
        impl_->gridOptions = {};
        impl_->lightVisualizerOptions = {};
        impl_->targetHandle = {};
        impl_->initialized = false;
    }

    /** @copydoc EditorViewportRendererMetal::RequestExtent */
    void EditorViewportRendererMetal::RequestExtent(const EditorViewportExtent extent) noexcept {
        impl_->requestedExtent.width = std::min(extent.width, maxViewportDimension);
        impl_->requestedExtent.height = std::min(extent.height, maxViewportDimension);
    }

    /** @copydoc EditorViewportRendererMetal::RequestGrid */
    void EditorViewportRendererMetal::RequestGrid(const EditorViewportGridOptions &options) noexcept {
        impl_->gridOptions = options;
        if (!std::isfinite(impl_->gridOptions.targetMinorSpacingPixels) || impl_->gridOptions.targetMinorSpacingPixels <= 0.0F)
            impl_->gridOptions.targetMinorSpacingPixels = 48.0F;
    }

    /** @copydoc EditorViewportRendererMetal::RequestLightVisualizer */
    void EditorViewportRendererMetal::RequestLightVisualizer(const EditorViewportLightVisualizerOptions &options) noexcept {
        impl_->lightVisualizerOptions = std::move(options);
    }

    /** @copydoc EditorViewportRendererMetal::RequestedExtent */
    EditorViewportExtent EditorViewportRendererMetal::RequestedExtent() const noexcept {
        return impl_->requestedExtent;
    }

    /** @copydoc EditorViewportRendererMetal::ClipDepthRange */
    Math::ClipDepthRange EditorViewportRendererMetal::ClipDepthRange() const noexcept {
        return Math::ClipDepthRange::ZeroToOne;
    }

    /** @copydoc EditorViewportRendererMetal::ExecuteStaticMeshPass */
    Result<void> EditorViewportRendererMetal::ExecuteStaticMeshPass(const Render::StaticMeshPassDescriptor &descriptor) {
        if (!impl_->initialized) {
            return Result<void>::Failure(MakeViewportError("editor.viewport.not_initialized", "Viewport renderer is not initialized."));
        }
        const EditorViewportExtent requestedExtent = std::exchange(impl_->requestedExtent, {});
        if (!requestedExtent.IsValid()) {
            return Result<void>::Success();
        }
        if (!descriptor.IsValid() || descriptor.extent.width != requestedExtent.width ||
            descriptor.extent.height != requestedExtent.height) {
            return Result<void>::Failure(MakeViewportError("editor.viewport.invalid_scene", "Editor viewport scene data is invalid."));
        }
        if (impl_->targetHandle.IsValid() && impl_->targetHandle != descriptor.target) {
            return Result<void>::Failure(
                MakeViewportError("editor.viewport.stale_target", "Viewport pass references a stale render target."));
        }
        impl_->targetHandle = descriptor.target;

        if (requestedExtent.width != impl_->allocatedExtent.width || requestedExtent.height != impl_->allocatedExtent.height) {
            MTLTextureDescriptor *colorDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                                       width:requestedExtent.width
                                                                                                      height:requestedExtent.height
                                                                                                   mipmapped:NO];
            colorDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            colorDescriptor.storageMode = MTLStorageModePrivate;
            id<MTLTexture> colorTexture = [impl_->device newTextureWithDescriptor:colorDescriptor];

            MTLTextureDescriptor *depthDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                                       width:requestedExtent.width
                                                                                                      height:requestedExtent.height
                                                                                                   mipmapped:NO];
            depthDescriptor.usage = MTLTextureUsageRenderTarget;
            depthDescriptor.storageMode = MTLStorageModePrivate;
            id<MTLTexture> depthTexture = [impl_->device newTextureWithDescriptor:depthDescriptor];
            if (colorTexture == nil || depthTexture == nil) {
                return Result<void>::Failure(
                    MakeViewportError("editor.viewport.target_creation_failed", "Failed to create Metal viewport render targets."));
            }
            impl_->retiredColorTextures[impl_->nextRetiredTexture] = impl_->colorTexture;
            impl_->nextRetiredTexture = (impl_->nextRetiredTexture + 1U) % impl_->retiredColorTextures.size();
            impl_->colorTexture = colorTexture;
            impl_->depthTexture = depthTexture;
            impl_->allocatedExtent = requestedExtent;
        }

        id<MTLCommandBuffer> commandBuffer = (__bridge id<MTLCommandBuffer>)impl_->graphicsBridge->CurrentCommandBuffer();
        if (commandBuffer == nil) {
            return Result<void>::Failure(
                MakeViewportError("editor.viewport.no_active_frame", "Metal viewport rendering requires an active renderer frame."));
        }

        for (const Render::RenderMeshResourceView &resource : descriptor.scene.meshResources) {
            if (auto existing = impl_->meshes.find(resource.handle.id.value); existing != impl_->meshes.end()) {
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
                return Result<void>::Failure(
                    MakeViewportError("editor.viewport.resource_creation_failed", "Failed to upload a Metal viewport mesh resource."));
            impl_->meshes.emplace(resource.handle.id.value, std::move(mesh));
        }
        for (auto mesh = impl_->meshes.begin(); mesh != impl_->meshes.end();) {
            const bool present = std::ranges::any_of(descriptor.scene.meshResources, [&](const Render::RenderMeshResourceView &resource) {
                return resource.handle.id.value == mesh->first && resource.handle.generation == mesh->second.generation;
            });
            if (!present) {
                id<MTLBuffer> retiredVertex = mesh->second.vertexBuffer;
                id<MTLBuffer> retiredIndex = mesh->second.indexBuffer;
                [commandBuffer addCompletedHandler:^(__unused id<MTLCommandBuffer> completed) {
                  (void)retiredVertex;
                  (void)retiredIndex;
                }];
                mesh = impl_->meshes.erase(mesh);
            } else
                ++mesh;
        }

        const float aspect = static_cast<float>(impl_->allocatedExtent.width) / static_cast<float>(impl_->allocatedExtent.height);
        const Result<std::optional<EditorViewportDirectionalShadowView>> shadow =
            BuildEditorViewportDirectionalShadowView(descriptor.scene, Math::ClipDepthRange::ZeroToOne);
        if (shadow.HasError()) {
            return Result<void>::Failure(shadow.ErrorValue());
        }

        if (shadow.Value().has_value()) {
            MTLRenderPassDescriptor *shadowPass = [MTLRenderPassDescriptor renderPassDescriptor];
            shadowPass.depthAttachment.texture = impl_->shadowDepthTexture;
            shadowPass.depthAttachment.loadAction = MTLLoadActionClear;
            shadowPass.depthAttachment.storeAction = MTLStoreActionStore;
            shadowPass.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> shadowEncoder = [commandBuffer renderCommandEncoderWithDescriptor:shadowPass];
            if (shadowEncoder == nil) {
                return Result<void>::Failure(MakeViewportError("editor.viewport.shadow_encoder_creation_failed",
                                                               "Failed to create the Metal directional shadow render encoder."));
            }
            [shadowEncoder pushDebugGroup:@"Horo Editor Directional Shadow"];
            [shadowEncoder setRenderPipelineState:impl_->shadowPipeline];
            [shadowEncoder setDepthStencilState:impl_->depthState];
            [shadowEncoder setCullMode:MTLCullModeFront];
            [shadowEncoder setDepthBias:0.00045F slopeScale:2.5F clamp:0.01F];
            for (const Render::RenderStaticMeshInstance &instance : descriptor.scene.instances) {
                const auto mesh = impl_->meshes.find(instance.mesh.id.value);
                if (mesh == impl_->meshes.end()) {
                    [shadowEncoder endEncoding];
                    return Result<void>::Failure(
                        MakeViewportError("editor.viewport.stale_mesh_resource", "Shadow pass instance references a stale mesh resource."));
                }
                const Math::Mat4 shadowMvp = Math::Multiply(shadow.Value()->viewProjection, instance.localToWorld);
                [shadowEncoder setVertexBuffer:mesh->second.vertexBuffer offset:0 atIndex:0];
                [shadowEncoder setVertexBytes:&shadowMvp length:sizeof(shadowMvp) atIndex:1];
                [shadowEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                          indexCount:mesh->second.indexCount
                                           indexType:MTLIndexTypeUInt32
                                         indexBuffer:mesh->second.indexBuffer
                                   indexBufferOffset:0];
            }
            [shadowEncoder popDebugGroup];
            [shadowEncoder endEncoding];
        }

        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = impl_->colorTexture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(descriptor.clearColor.red, descriptor.clearColor.green,
                                                                descriptor.clearColor.blue, descriptor.clearColor.alpha);
        pass.depthAttachment.texture = impl_->depthTexture;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
        if (encoder == nil) {
            return Result<void>::Failure(
                MakeViewportError("editor.viewport.encoder_creation_failed", "Failed to create the Metal viewport render encoder."));
        }
        [encoder pushDebugGroup:@"Horo Editor Viewport"];
        [encoder setCullMode:MTLCullModeNone];
        if (impl_->gridOptions.visible) {
            ViewportGridGeometry grid;
            if (!BuildViewportGridGeometry(
                    ViewportGridGeometryRequest{
                        .camera = descriptor.scene.camera,
                        .aspect = aspect,
                        .viewportHeightPixels = static_cast<float>(impl_->allocatedExtent.height),
                        .targetMinorSpacingPixels = impl_->gridOptions.targetMinorSpacingPixels,
                        .targetLineWidthPixels = impl_->gridOptions.targetLineWidthPixels,
                    },
                    grid)) {
                [encoder endEncoding];
                return Result<void>::Failure(
                    MakeViewportError("editor.viewport.invalid_grid", "Failed to build finite viewport grid geometry."));
            }
            const Result<Math::Mat4> viewProjection =
                BuildRenderMvp(descriptor.scene.camera, Math::Mat4::Identity(), aspect, Math::ClipDepthRange::ZeroToOne);
            if (viewProjection.HasError()) {
                [encoder endEncoding];
                return Result<void>::Failure(viewProjection.ErrorValue());
            }

            std::byte *gridBytes = static_cast<std::byte *>(impl_->gridVertexBuffer.contents);
            NSUInteger gridOffset = 0;
            const auto encodeBatch = [&](const ViewportGridLineBatch batch) {
                if (batch.positions.empty())
                    return;
                const NSUInteger byteCount = static_cast<NSUInteger>(batch.positions.size_bytes());
                std::memcpy(gridBytes + gridOffset, batch.positions.data(), byteCount);
                [encoder setVertexBuffer:impl_->gridVertexBuffer offset:gridOffset atIndex:0];
                [encoder setVertexBytes:viewProjection.Value().values.data() length:sizeof(viewProjection.Value().values) atIndex:1];
                const MetalGridStyle style{.color = batch.color};
                [encoder setFragmentBytes:&style length:sizeof(style) atIndex:0];
                const MTLPrimitiveType primitive =
                    batch.topology == ViewportGridPrimitiveTopology::Triangles ? MTLPrimitiveTypeTriangle : MTLPrimitiveTypeLine;
                [encoder drawPrimitives:primitive vertexStart:0 vertexCount:static_cast<NSUInteger>(batch.positions.size())];
                gridOffset += byteCount;
            };
            [encoder setRenderPipelineState:impl_->gridPipeline];
            [encoder setDepthStencilState:impl_->gridDepthState];
            encodeBatch(grid.RegularLines());
            encodeBatch(grid.Axes());
        }
        [encoder setRenderPipelineState:impl_->pipeline];
        [encoder setDepthStencilState:impl_->depthState];
        MetalSceneLighting sceneLighting;
        sceneLighting.cameraPosition = descriptor.scene.camera.position;
        sceneLighting.lightCount = static_cast<std::uint32_t>(descriptor.scene.lights.size());
        sceneLighting.shadowEnabled = shadow.Value().has_value() ? 1U : 0U;
        sceneLighting.shadowLightIndex = shadow.Value().has_value() ? static_cast<std::uint32_t>(shadow.Value()->lightIndex) : 0U;
        for (std::size_t index = 0; index < descriptor.scene.lights.size(); ++index) {
            const Render::RenderLight &light = descriptor.scene.lights[index];
            sceneLighting.lights[index] = {
                .positionKind = {light.position.x, light.position.y, light.position.z, static_cast<float>(light.kind)},
                .directionRange = {light.direction.x, light.direction.y, light.direction.z, light.range},
                .colorIntensity = {light.color.x, light.color.y, light.color.z, light.intensity},
                .cone = {light.innerConeCosine, light.outerConeCosine, 0.0F, 0.0F},
            };
        }
        [encoder setFragmentBytes:&sceneLighting length:sizeof(sceneLighting) atIndex:1];
        [encoder setFragmentTexture:impl_->shadowDepthTexture atIndex:0];
        [encoder setFragmentSamplerState:impl_->shadowSampler atIndex:0];
        for (const Render::RenderStaticMeshInstance &instance : descriptor.scene.instances) {
            const auto mesh = impl_->meshes.find(instance.mesh.id.value);
            if (mesh == impl_->meshes.end()) {
                [encoder endEncoding];
                return Result<void>::Failure(
                    MakeViewportError("editor.viewport.stale_mesh_resource", "Viewport instance references a stale mesh resource."));
            }
            [encoder setVertexBuffer:mesh->second.vertexBuffer offset:0 atIndex:0];
            const Result<Math::Mat4> mvp =
                BuildRenderMvp(descriptor.scene.camera, instance.localToWorld, aspect, Math::ClipDepthRange::ZeroToOne);
            if (mvp.HasError()) {
                [encoder endEncoding];
                return Result<void>::Failure(mvp.ErrorValue());
            }
            const MetalObjectTransforms transforms{
                .mvp = mvp.Value(),
                .model = instance.localToWorld,
                .shadowMvp = shadow.Value().has_value() ? Math::Multiply(shadow.Value()->viewProjection, instance.localToWorld)
                                                        : Math::Mat4::Identity(),
            };
            const MetalSelectionStyle selectionStyle{instance.presentation.tint, instance.presentation.tintStrength};
            [encoder setVertexBytes:&transforms length:sizeof(transforms) atIndex:1];
            [encoder setFragmentBytes:&selectionStyle length:sizeof(selectionStyle) atIndex:0];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:mesh->second.indexCount
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:mesh->second.indexBuffer
                         indexBufferOffset:0];
        }
        if (impl_->lightVisualizerOptions.selectedLight.has_value()) {
            LightVisualizerGeometry geometry;
            if (!BuildLightVisualizerGeometry(
                    LightVisualizerGeometryRequest{
                        .camera = descriptor.scene.camera,
                        .light = *impl_->lightVisualizerOptions.selectedLight,
                    },
                    geometry)) {
                [encoder endEncoding];
                return Result<void>::Failure(
                    MakeViewportError("editor.viewport.invalid_light_visualizer", "Failed to build selected Light visualizer geometry."));
            }
            const Result<Math::Mat4> viewProjection =
                BuildRenderMvp(descriptor.scene.camera, Math::Mat4::Identity(), aspect, Math::ClipDepthRange::ZeroToOne);
            if (viewProjection.HasError()) {
                [encoder endEncoding];
                return Result<void>::Failure(viewProjection.ErrorValue());
            }
            std::memcpy(impl_->gridVertexBuffer.contents, geometry.Lines().data(), geometry.Lines().size_bytes());
            [encoder setRenderPipelineState:impl_->gridPipeline];
            [encoder setDepthStencilState:impl_->gridDepthState];
            [encoder setVertexBuffer:impl_->gridVertexBuffer offset:0 atIndex:0];
            [encoder setVertexBytes:viewProjection.Value().values.data() length:sizeof(viewProjection.Value().values) atIndex:1];
            const MetalGridStyle style{.color = geometry.color};
            [encoder setFragmentBytes:&style length:sizeof(style) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:static_cast<NSUInteger>(geometry.vertexCount)];
        }
        [encoder popDebugGroup];
        [encoder endEncoding];
        return Result<void>::Success();
    }

    /** @copydoc EditorViewportRendererMetal::TextureView */
    EditorViewportTextureView EditorViewportRendererMetal::TextureView() const noexcept {
        return EditorViewportTextureView{
            .textureId = reinterpret_cast<std::uintptr_t>((__bridge void *)impl_->colorTexture),
            .u0 = 0.0F,
            .v0 = 0.0F,
            .u1 = 1.0F,
            .v1 = 1.0F,
        };
    }

    /** @copydoc EditorViewportRendererMetal::IsReady */
    bool EditorViewportRendererMetal::IsReady() const noexcept {
        return impl_->initialized && impl_->colorTexture != nil && impl_->allocatedExtent.IsValid();
    }
}  // namespace Horo::Editor
