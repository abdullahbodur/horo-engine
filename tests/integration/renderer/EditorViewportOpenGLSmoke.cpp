#include <catch2/catch_test_macros.hpp>

#include "Horo/Runtime/Render/RenderFrontend.h"
#include "Horo/Runtime/Scene/PrimitiveMesh.h"
#include "editor/renderer/opengl/EditorViewportRendererOpenGL.h"
#include "editor/renderer/opengl/SdlOpenGLPresentationPort.h"
#include "runtime/renderer/modules/opengl/OpenGLBackendModule.h"

#include <SDL3/SDL.h>

#include <glad/gl.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

namespace
{
using namespace Horo;
using namespace Horo::Editor;
using namespace Horo::Render;

void Check(const bool condition)
{
    REQUIRE((condition));
}

void WritePpm(const std::filesystem::path &path, const std::span<const std::uint8_t> rgba, const std::uint32_t width,
              const std::uint32_t height)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Check(output.is_open());
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint32_t sourceY = height - 1 - y;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(sourceY) * width + x) * 4;
            output.write(reinterpret_cast<const char *>(rgba.data() + offset), 3);
        }
    }
    Check(output.good());
}
} // namespace

TEST_CASE("Editor Viewport Open GL Smoke", "[integration][renderer][gpu]")
{
    constexpr std::uint32_t width = 512;
    constexpr std::uint32_t height = 384;
    const std::filesystem::path outputPath = "horo-viewport-primitives.ppm";

    Check(SDL_Init(SDL_INIT_VIDEO));
    Check(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1));
    Check(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24));
    SDL_Window *window = SDL_CreateWindow("Horo viewport smoke", 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    Check(window != nullptr);

    SdlOpenGLPresentationPort presentationPort{*window};
    RenderBackendRegistry registry;
    Check(RegisterOpenGLRenderBackend(registry, presentationPort).HasValue());
    Check(registry.Seal().HasValue());
    auto frontendResult = RenderFrontend::Create(registry, RenderBackendId{"opengl"},
                                                 RenderBackendConfig{.requirePresentation = true,
                                                                     .enableValidation = false,
                                                                     .maxFramesInFlight = 2,
                                                                     .presentMode = PresentMode::Immediate});
    Check(frontendResult.HasValue());
    std::unique_ptr<RenderFrontend> frontend = std::move(frontendResult).Value();

    Check(gladLoadGL(SDL_GL_GetProcAddress) != 0);
    GLuint callerVertexArray = 0;
    GLuint callerArrayBuffer = 0;
    GLuint callerDrawFramebuffer = 0;
    GLuint callerReadFramebuffer = 0;
    glGenVertexArrays(1, &callerVertexArray);
    glGenBuffers(1, &callerArrayBuffer);
    glGenFramebuffers(1, &callerDrawFramebuffer);
    glGenFramebuffers(1, &callerReadFramebuffer);
    glBindVertexArray(callerVertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, callerArrayBuffer);

    EditorViewportRendererOpenGL viewport;
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
        const MeshData &mesh = meshLease.Data();
        const RenderMeshHandle meshHandle{meshLease.Id(), 1};
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
             mesh.localBounds,
             CoreDefaultMaterial,
             {.tint = {0.12F, 0.72F, 1.0F}, .tintStrength = index == 0 ? 0.65F : 0.0F}});
        meshLeases.push_back(std::move(meshLease));
    }
    const EditorViewportSceneView viewportScene{
        .camera = {}, .meshResources = meshResources, .instances = viewportInstances};

    GLint initializedVertexArray = 0;
    GLint initializedArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &initializedVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &initializedArrayBuffer);
    Check(initializedVertexArray == static_cast<GLint>(callerVertexArray));
    Check(initializedArrayBuffer == static_cast<GLint>(callerArrayBuffer));
    viewport.RequestExtent(EditorViewportExtent{width, height});
    Check(frontend->AttachStaticMeshPassExecutor(viewport).HasValue());
    auto viewportTargetResult = frontend->CreateOffscreenTarget({width, height});
    Check(viewportTargetResult.HasValue());
    const RenderTargetHandle viewportTarget = viewportTargetResult.Value();

    auto begun = frontend->BeginFrame(FrameDescriptor{.frameNumber = 1, .outputExtent = {640, 480}});
    Check(begun.HasValue());
    RenderFrameScope frame = std::move(begun).Value();
    const std::array passes{
        RenderPassDescriptor{
            .id = RenderPassId{1},
            .kind = RenderPassKind::Graphics,
            .staticMesh =
                StaticMeshPassDescriptor{
                    .target = viewportTarget,
                    .extent = {width, height},
                    .scene = RenderSceneView{ToRenderCamera(viewportScene.camera), viewportScene.meshResources,
                                             viewportScene.instances},
                },
        },
        RenderPassDescriptor{
            .id = RenderPassId{2},
            .kind = RenderPassKind::Graphics,
            .primaryOutput = PrimaryOutputAttachment{},
        },
    };

    glViewport(7, 9, 111, 113);
    glClearColor(0.2F, 0.3F, 0.4F, 0.5F);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glEnable(GL_SCISSOR_TEST);
    glScissor(3, 5, 7, 11);
    glEnable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT_AND_BACK);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, callerDrawFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, callerReadFramebuffer);

    GLint restoredDrawFramebuffer = 0;
    GLint restoredReadFramebuffer = 0;
    GLint restoredViewport[4]{};
    GLint restoredDepthFunction = 0;
    GLint restoredScissorBox[4]{};
    GLboolean restoredColorMask[4]{};
    GLboolean restoredDepthMask = GL_TRUE;
    GLfloat restoredClearColor[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoredDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredReadFramebuffer);
    glGetIntegerv(GL_VIEWPORT, restoredViewport);
    glGetIntegerv(GL_DEPTH_FUNC, &restoredDepthFunction);
    glGetIntegerv(GL_SCISSOR_BOX, restoredScissorBox);
    glGetBooleanv(GL_COLOR_WRITEMASK, restoredColorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &restoredDepthMask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, restoredClearColor);
    Check(restoredDrawFramebuffer == static_cast<GLint>(callerDrawFramebuffer));
    Check(restoredReadFramebuffer == static_cast<GLint>(callerReadFramebuffer));
    Check(restoredViewport[0] == 7 && restoredViewport[1] == 9 && restoredViewport[2] == 111 &&
          restoredViewport[3] == 113);
    Check(glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
    Check(restoredDepthFunction == GL_ALWAYS);
    Check(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
    Check(glIsEnabled(GL_BLEND) == GL_TRUE);
    Check(glIsEnabled(GL_CULL_FACE) == GL_TRUE);
    Check(restoredScissorBox[0] == 3 && restoredScissorBox[1] == 5 && restoredScissorBox[2] == 7 &&
          restoredScissorBox[3] == 11);
    Check(restoredColorMask[0] == GL_FALSE && restoredColorMask[1] == GL_FALSE && restoredColorMask[2] == GL_FALSE &&
          restoredColorMask[3] == GL_FALSE);
    Check(restoredDepthMask == GL_FALSE);
    Check(std::fabs(restoredClearColor[0] - 0.2F) < 0.001F && std::fabs(restoredClearColor[3] - 0.5F) < 0.001F);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // The primary pass executes after external viewport GL work and re-establishes
    // the swapchain output state before GUI rendering/presentation.
    Check(frame.Execute(passes).HasValue());
    Check(viewport.IsReady());
    const EditorViewportTextureView firstTextureView = viewport.TextureView();
    Check(firstTextureView.IsValid());
    Check(firstTextureView.v0 == 1.0F && firstTextureView.v1 == 0.0F);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(firstTextureView.textureId));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    std::size_t coloredPixels = 0;
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        const bool differsFromBackground = pixels[offset] > 20 || pixels[offset + 1] > 24 || pixels[offset + 2] > 32;
        coloredPixels += differsFromBackground ? 1 : 0;
    }
    Check(coloredPixels > 10000);
    const std::size_t center = (static_cast<std::size_t>(height / 2) * width + width / 2) * 4;
    Check(pixels[center] < 150 && pixels[center + 1] > 130 && pixels[center + 2] > 150);
    WritePpm(outputPath, pixels, width, height);

    Check(frame.Present().HasValue());
    frontend->DetachStaticMeshPassExecutor(viewport);
    Check(frontend->ReleaseOffscreenTarget(viewportTarget).HasValue());
    viewport.Shutdown();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDeleteBuffers(1, &callerArrayBuffer);
    glDeleteVertexArrays(1, &callerVertexArray);
    glDeleteFramebuffers(1, &callerDrawFramebuffer);
    glDeleteFramebuffers(1, &callerReadFramebuffer);
    frontend.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
}
