#include "EditorViewportRendererOpenGL.h"
#include "editor/renderer/EditorRendererErrors.h"

#include <SDL3/SDL_video.h>
#include <glad/gl.h>

#include <algorithm>
#include <string>
#include <utility>

namespace Horo::Editor
{
namespace
{
constexpr std::uint32_t maxViewportDimension = 8192;

[[nodiscard]] Error MakeViewportError(const ErrorCodeDescriptor &descriptor, std::string message)
{
    return MakeError(descriptor, std::move(message));
}

[[nodiscard]] Result<std::uint32_t> CompileShader(const std::uint32_t type, const char *source)
{
    const std::uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
    {
        return Result<std::uint32_t>::Success(shader);
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    return Result<std::uint32_t>::Failure(
        MakeViewportError(RendererErrors::ViewportShaderCompileFailed, "Viewport shader compilation failed: " + log));
}
} // namespace

/** @copydoc EditorViewportRendererOpenGL::~EditorViewportRendererOpenGL */
EditorViewportRendererOpenGL::~EditorViewportRendererOpenGL()
{
    Shutdown();
}

/** @copydoc EditorViewportRendererOpenGL::Initialize */
Result<void> EditorViewportRendererOpenGL::Initialize()
{
    if (initialized_)
    {
        return Result<void>::Failure(MakeViewportError(RendererErrors::ViewportAlreadyInitialized,
                                                       "Editor viewport renderer is already initialized."));
    }
    if (gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) == 0)
    {
        return Result<void>::Failure(
            MakeViewportError(RendererErrors::ViewportOpenGLDispatchFailed, "Failed to load OpenGL entry points."));
    }
    if (const Result<void> program = CreateProgram(); program.HasError())
    {
        Shutdown();
        return program;
    }
    initialized_ = true;
    return Result<void>::Success();
}

/** @copydoc EditorViewportRendererOpenGL::Shutdown */
void EditorViewportRendererOpenGL::Shutdown() noexcept
{
    DestroyTarget();
    for (auto &[id, mesh] : meshes_)
        DestroyMesh(mesh);
    meshes_.clear();
    if (program_ != 0)
    {
        glDeleteProgram(program_);
        program_ = 0;
    }
    mvpLocation_ = -1;
    selectionColorLocation_ = -1;
    selectionStrengthLocation_ = -1;
    requestedExtent_ = {};
    targetHandle_ = {};
    initialized_ = false;
}

/** @copydoc EditorViewportRendererOpenGL::RequestExtent */
void EditorViewportRendererOpenGL::RequestExtent(const EditorViewportExtent extent) noexcept
{
    requestedExtent_.width = std::min(extent.width, maxViewportDimension);
    requestedExtent_.height = std::min(extent.height, maxViewportDimension);
}

/** @copydoc EditorViewportRendererOpenGL::RequestedExtent */
EditorViewportExtent EditorViewportRendererOpenGL::RequestedExtent() const noexcept
{
    return requestedExtent_;
}

/** @copydoc EditorViewportRendererOpenGL::ExecuteStaticMeshPass */
Result<void> EditorViewportRendererOpenGL::ExecuteStaticMeshPass(const Render::StaticMeshPassDescriptor &descriptor)
{
    if (!initialized_)
    {
        return Result<void>::Failure(
            MakeViewportError(RendererErrors::ViewportNotInitialized, "Viewport renderer is not initialized."));
    }
    // A panel must request an extent every UI frame. Consuming the request keeps
    // hidden/inactive viewport tabs from spending GPU time in the background.
    const EditorViewportExtent requestedExtent = std::exchange(requestedExtent_, {});
    if (!requestedExtent.IsValid())
    {
        return Result<void>::Success();
    }
    if (!descriptor.IsValid() || descriptor.extent.width != requestedExtent.width ||
        descriptor.extent.height != requestedExtent.height)
    {
        return Result<void>::Failure(
            MakeViewportError(RendererErrors::ViewportInvalidScene, "Editor viewport scene data is invalid."));
    }
    if (targetHandle_.IsValid() && targetHandle_ != descriptor.target)
    {
        return Result<void>::Failure(
            MakeViewportError(RendererErrors::ViewportStaleTarget, "Viewport pass references a stale render target."));
    }
    targetHandle_ = descriptor.target;
    if (requestedExtent.width != allocatedExtent_.width || requestedExtent.height != allocatedExtent_.height)
    {
        if (const Result<void> recreated = RecreateTarget(requestedExtent); recreated.HasError())
        {
            return recreated;
        }
    }
    if (const Result<void> synchronized = SynchronizeMeshes(descriptor.scene.meshResources); synchronized.HasError())
        return synchronized;

    const float aspect = static_cast<float>(allocatedExtent_.width) / static_cast<float>(allocatedExtent_.height);

    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    GLint previousDepthFunction = 0;
    GLint previousViewport[4]{};
    GLfloat previousClearColor[4]{};
    const GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean scissorTestWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean previousColorMask[4]{};
    GLboolean previousDepthMask = GL_TRUE;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunction);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, static_cast<GLsizei>(allocatedExtent_.width), static_cast<GLsizei>(allocatedExtent_.height));
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(descriptor.clearColor.red, descriptor.clearColor.green, descriptor.clearColor.blue,
                 descriptor.clearColor.alpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    for (const Render::RenderStaticMeshInstance &instance : descriptor.scene.instances)
    {
        const auto mesh = meshes_.find(instance.mesh.id.value);
        if (mesh == meshes_.end())
            return Result<void>::Failure(MakeViewportError(RendererErrors::ViewportStaleMeshResource,
                                                           "Viewport instance references a stale mesh resource."));
        glBindVertexArray(mesh->second.vertexArray);
        const Result<Math::Mat4> mvp = BuildRenderMvp(descriptor.scene.camera, instance.localToWorld, aspect,
                                                     Math::ClipDepthRange::NegativeOneToOne);
        if (mvp.HasError())
            return Result<void>::Failure(mvp.ErrorValue());
        glUniformMatrix4fv(mvpLocation_, 1, GL_FALSE, mvp.Value().values.data());
        glUniform3f(selectionColorLocation_, instance.presentation.tint.x, instance.presentation.tint.y,
                    instance.presentation.tint.z);
        glUniform1f(selectionStrengthLocation_, instance.presentation.tintStrength);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->second.indexCount), GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glUseProgram(static_cast<GLuint>(previousProgram));
    glDepthFunc(static_cast<GLenum>(previousDepthFunction));
    if (depthTestWasEnabled == GL_TRUE)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    if (scissorTestWasEnabled == GL_TRUE)
    {
        glEnable(GL_SCISSOR_TEST);
    }
    else
    {
        glDisable(GL_SCISSOR_TEST);
    }
    if (blendWasEnabled == GL_TRUE)
    {
        glEnable(GL_BLEND);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    if (cullFaceWasEnabled == GL_TRUE)
    {
        glEnable(GL_CULL_FACE);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }
    glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
    glDepthMask(previousDepthMask);
    glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    return Result<void>::Success();
}

/** @copydoc EditorViewportRendererOpenGL::TextureView */
EditorViewportTextureView EditorViewportRendererOpenGL::TextureView() const noexcept
{
    return EditorViewportTextureView{
        .textureId = static_cast<std::uintptr_t>(colorTexture_),
        .u0 = 0.0F,
        .v0 = 1.0F,
        .u1 = 1.0F,
        .v1 = 0.0F,
    };
}

/** @copydoc EditorViewportRendererOpenGL::IsReady */
bool EditorViewportRendererOpenGL::IsReady() const noexcept
{
    return initialized_ && colorTexture_ != 0 && allocatedExtent_.IsValid();
}

Result<void> EditorViewportRendererOpenGL::CreateProgram()
{
    static constexpr const char *vertexSource = R"glsl(#version 150 core
in vec3 aPosition;
in vec3 aNormal;
in vec2 aUv;
out vec3 vColor;
uniform mat4 uMvp;
void main()
{
    float light = 0.55 + 0.45 * max(dot(normalize(aNormal), normalize(vec3(0.4, 0.8, 0.3))), 0.0);
    vColor = vec3(0.62, 0.67, 0.74) * light;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)glsl";
    static constexpr const char *fragmentSource = R"glsl(#version 150 core
in vec3 vColor;
out vec4 outColor;
uniform vec3 uSelectionColor;
uniform float uSelectionStrength;
void main()
{
    outColor = vec4(mix(vColor, uSelectionColor, uSelectionStrength), 1.0);
}
)glsl";

    auto vertex = CompileShader(GL_VERTEX_SHADER, vertexSource);
    if (vertex.HasError())
    {
        return Result<void>::Failure(vertex.ErrorValue());
    }
    auto fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (fragment.HasError())
    {
        glDeleteShader(vertex.Value());
        return Result<void>::Failure(fragment.ErrorValue());
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertex.Value());
    glAttachShader(program_, fragment.Value());
    glBindAttribLocation(program_, 0, "aPosition");
    glBindAttribLocation(program_, 1, "aNormal");
    glBindAttribLocation(program_, 2, "aUv");
    glBindFragDataLocation(program_, 0, "outColor");
    glLinkProgram(program_);
    glDeleteShader(vertex.Value());
    glDeleteShader(fragment.Value());

    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        GLint logLength = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(program_, logLength, nullptr, log.data());
        return Result<void>::Failure(
            MakeViewportError(RendererErrors::ViewportShaderLinkFailed, "Viewport shader linking failed: " + log));
    }
    mvpLocation_ = glGetUniformLocation(program_, "uMvp");
    selectionColorLocation_ = glGetUniformLocation(program_, "uSelectionColor");
    selectionStrengthLocation_ = glGetUniformLocation(program_, "uSelectionStrength");
    if (mvpLocation_ < 0 || selectionColorLocation_ < 0 || selectionStrengthLocation_ < 0)
    {
        return Result<void>::Failure(MakeViewportError(RendererErrors::ViewportShaderContractInvalid,
                                                       "Viewport shader is missing a required frame uniform."));
    }
    return Result<void>::Success();
}

Result<void> EditorViewportRendererOpenGL::SynchronizeMeshes(
    const std::span<const EditorViewportMeshResourceView> resources)
{
    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);

    for (const EditorViewportMeshResourceView &resource : resources)
    {
        if (auto existing = meshes_.find(resource.handle.id.value); existing != meshes_.end())
        {
            if (existing->second.generation == resource.handle.generation)
                continue;
            DestroyMesh(existing->second);
            meshes_.erase(existing);
        }
        GpuMesh mesh{.indexCount = static_cast<std::uint32_t>(resource.indices.size()),
                     .generation = resource.handle.generation};
        glGenVertexArrays(1, &mesh.vertexArray);
        glBindVertexArray(mesh.vertexArray);
        glGenBuffers(1, &mesh.vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(resource.vertices.size_bytes()), resource.vertices.data(),
                     GL_STATIC_DRAW);
        glGenBuffers(1, &mesh.indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(resource.indices.size_bytes()),
                     resource.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Render::MeshVertex)), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Render::MeshVertex)),
                              reinterpret_cast<const void *>(offsetof(Render::MeshVertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Render::MeshVertex)),
                              reinterpret_cast<const void *>(offsetof(Render::MeshVertex, uv)));
        if (mesh.vertexArray == 0 || mesh.vertexBuffer == 0 || mesh.indexBuffer == 0)
        {
            DestroyMesh(mesh);
            glBindVertexArray(static_cast<GLuint>(previousVertexArray));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
            return Result<void>::Failure(MakeViewportError(RendererErrors::ViewportGeometryCreationFailed,
                                                           "Failed to upload a viewport mesh resource."));
        }
        meshes_.emplace(resource.handle.id.value, mesh);
    }
    for (auto mesh = meshes_.begin(); mesh != meshes_.end();)
    {
        const bool present = std::ranges::any_of(resources, [&](const EditorViewportMeshResourceView &resource) {
            return resource.handle.id.value == mesh->first && resource.handle.generation == mesh->second.generation;
        });
        if (!present)
        {
            DestroyMesh(mesh->second);
            mesh = meshes_.erase(mesh);
        }
        else
            ++mesh;
    }
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    return Result<void>::Success();
}

void EditorViewportRendererOpenGL::DestroyMesh(GpuMesh &mesh) noexcept
{
    if (mesh.indexBuffer != 0)
        glDeleteBuffers(1, &mesh.indexBuffer);
    if (mesh.vertexBuffer != 0)
        glDeleteBuffers(1, &mesh.vertexBuffer);
    if (mesh.vertexArray != 0)
        glDeleteVertexArrays(1, &mesh.vertexArray);
    mesh = {};
}

Result<void> EditorViewportRendererOpenGL::RecreateTarget(const EditorViewportExtent extent)
{
    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousTexture = 0;
    GLint previousRenderbuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

    if (framebuffer_ == 0)
    {
        glGenFramebuffers(1, &framebuffer_);
    }
    if (colorTexture_ == 0)
    {
        glGenTextures(1, &colorTexture_);
    }
    if (depthBuffer_ == 0)
    {
        glGenRenderbuffers(1, &depthBuffer_);
    }

    // Keep object identities stable across resize. ImGui records the texture ID
    // before the viewport render pass executes later in the same frame.
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(extent.width), static_cast<GLsizei>(extent.height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(extent.width),
                          static_cast<GLsizei>(extent.height));
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        DestroyTarget();
        return Result<void>::Failure(
            MakeViewportError(RendererErrors::ViewportFramebufferIncomplete,
                              "OpenGL viewport framebuffer is incomplete."));
    }
    allocatedExtent_ = extent;
    return Result<void>::Success();
}

void EditorViewportRendererOpenGL::DestroyTarget() noexcept
{
    if (depthBuffer_ != 0)
    {
        glDeleteRenderbuffers(1, &depthBuffer_);
        depthBuffer_ = 0;
    }
    if (colorTexture_ != 0)
    {
        glDeleteTextures(1, &colorTexture_);
        colorTexture_ = 0;
    }
    if (framebuffer_ != 0)
    {
        glDeleteFramebuffers(1, &framebuffer_);
        framebuffer_ = 0;
    }
    allocatedExtent_ = {};
}
} // namespace Horo::Editor
