# Renderer Abstraction Audit

## Summary

The HoroEngine renderer currently has pervasive OpenGL coupling spread across nine source files
outside any dedicated backend layer. Every geometric primitive type (`Mesh`, `SkinnedMesh`,
`DebugDraw`, `DebugHUD`) allocates and manages raw OpenGL VAO/VBO/EBO/texture IDs directly in
their implementation files. The `Shader` and `Texture` classes are fully GL-specific: they wrap
`glCreateProgram`/`glCreateShader` and `glGenTextures` respectively, and expose raw GL handles
(`GetProgramID()`, `GetNativeId()`). Higher-level layers — `editor/EditorLayer.cpp`,
`core/Window.cpp`, and `launcher/UiAutomationRunner.cpp` — also call GL directly for FBO
management, viewport setup, and pixel readback. `Renderer.cpp` itself is already reasonably
clean (it delegates fully to `IRenderBackend`), but its `CreateDefaultOwnedBackend` assert
message references "OpenGL" by name. The `RenderBackendFactory` properly routes to Vulkan when
compiled in, but `Shader`, `Texture`, `Mesh`, `SkinnedMesh`, `DebugDraw`, and `DebugHUD` are
unconditionally OpenGL and have no abstraction path.

---

## Seam Inventory

| File | Coupling Type | Specific Items | Breakage Plan |
|------|--------------|----------------|---------------|
| `renderer/Shader.cpp` | GL calls + glad include | `glCreateShader`, `glCompileShader`, `glShaderSource`, `glGetShaderiv`, `glGetShaderInfoLog`, `glDeleteShader`, `glCreateProgram`, `glAttachShader`, `glLinkProgram`, `glGetProgramiv`, `glGetProgramInfoLog`, `glDeleteProgram`, `glUseProgram`, `glGetUniformLocation`, `glUniform1i/1f/2f/3f/4f`, `glUniformMatrix3fv`, `glUniformMatrix4fv` | Move entire implementation to `renderer/opengl/OpenGLShader.cpp`; `Shader` becomes a thin handle wrapping `IShader*` |
| `renderer/Shader.h` | GL concept leak | `GetProgramID()` returns raw `unsigned int` (GL program object handle) | Replace with `GetNativeHandle() -> uintptr_t` on `IShader`, or remove — callers should use `Bind()`/`Unbind()` |
| `renderer/Texture.cpp` | GL calls + glad include | `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glGenerateMipmap`, `glTexParameteri`, `glDeleteTextures`, `glActiveTexture` | Move to `renderer/opengl/OpenGLTexture.cpp`; `Texture` wraps `ITexture*` |
| `renderer/Texture.h` | GL concept leak | `GetNativeId()` returns raw `unsigned int` (GL texture ID); `GetRenderTargetHandle()` calls `RenderTargetHandle::OpenGLTexture(...)` | `ITexture::GetNativeHandle()` with backend-agnostic `RenderTargetHandle` factory |
| `renderer/Mesh.cpp` | GL calls + glad include | `GpuStorage` holds raw `vao/vbo/ebo` (`unsigned int`); `glGenVertexArrays`, `glGenBuffers`, `glBufferData`, `glEnableVertexAttribArray`, `glVertexAttribPointer`, `glBindVertexArray`, `glBindBuffer`, `glDrawElements`, `glPolygonMode`, `glDeleteBuffers`, `glDeleteVertexArrays` | Move `GpuStorage` and `Upload()` to `renderer/opengl/OpenGLMeshBuffer.cpp`; expose via `IMeshBuffer` |
| `renderer/SkinnedMesh.cpp` | GL calls + glad include | Same pattern as `Mesh.cpp` plus `glVertexAttribIPointer` for bone indices | Move to `renderer/opengl/OpenGLSkinnedMeshBuffer.cpp` |
| `renderer/SkinnedMesh.h` | GL concept leak in comments | Attribute layout comments reference `GL_FLOAT`, `GL_INT`, `glVertexAttribPointer`, `glVertexAttribIPointer` by name | Update comments to be API-agnostic after abstraction |
| `renderer/DebugDraw.cpp` | GL calls + glad include | `glGenVertexArrays`, `glGenBuffers`, `glBufferData/SubData`, `glEnableVertexAttribArray`, `glVertexAttribPointer`, `glBindVertexArray`, `glBindBuffer`, `glDrawArrays`, `glDeleteBuffers`, `glDeleteVertexArrays` | Move to `renderer/opengl/OpenGLDebugDraw.cpp` behind `IDebugDraw` interface or backend `DrawDebugLines()` method |
| `renderer/DebugHUD.cpp` | GL calls + glad include | Full VAO/VBO setup, `glGenTextures` for font atlas, `glTexImage2D`, `glIsEnabled`, `glEnable/Disable(GL_DEPTH_TEST/GL_BLEND/GL_CULL_FACE)`, `glBlendFunc`, `glDrawArrays`, `glActiveTexture`, `glBufferSubData` | Move to `renderer/opengl/OpenGLDebugHUD.cpp`; expose via `IDebugHUD` interface |
| `renderer/RenderBackendFactory.cpp` | Structural coupling | Directly `#include`s `OpenGLRenderBackend.h`; constructs `OpenGLRenderBackend` by name | Already the right pattern — keep, but rename/move OpenGL backend files under `renderer/opengl/` in Goal 2 |
| `renderer/Renderer.cpp` | Soft coupling | `CreateDefaultOwnedBackend` assert message says "OpenGL render backend" hardcoded | Update message to "default render backend" as part of any goal |
| `core/Window.cpp` | GL calls + glad include | `glGetString(GL_VERSION/GL_RENDERER)`, `glViewport` | Add `IRenderBackend::OnWindowResize(w,h)` and `GetAPIVersionString()` to replace direct GL calls |
| `editor/EditorLayer.cpp` | GL calls + glad include | Full FBO creation/deletion (`glGenFramebuffers`, `glGenRenderbuffers`, `glRenderbufferStorage`, `glFramebufferTexture2D`, `glFramebufferRenderbuffer`, `glCheckFramebufferStatus`), `glReadPixels`, `glClearColor`, `glClear`, `glEnable/Disable`, `glViewport`, `glGetIntegerv` | These should all go through `IRenderBackend`: `EnsureEditorViewportRenderTarget` already exists for the FBO; pixel readback via `ReadbackColorBgr8`; state management via `BeginPass`/`EndPass` |
| `launcher/UiAutomationRunner.cpp` | GL calls + glad include | `glGetIntegerv(GL_VIEWPORT,...)`, `glReadPixels`, `glGetError` | Route through `IRenderBackend::ReadbackColorBgr8` (already exists) and a new `GetViewport()` |

---

## Backend Interface Gaps

The following methods need to be added to `IRenderBackend` (or a new `IResourceFactory`) to
replace the remaining direct GL calls in non-backend files:

| Method | Currently Called From | Reason |
|--------|-----------------------|--------|
| `OnWindowResize(int w, int h)` | `core/Window.cpp` | Replaces `glViewport` on resize |
| `GetAPIVersionString() -> std::string` | `core/Window.cpp` | Replaces `glGetString(GL_VERSION/GL_RENDERER)` |
| `GetViewport(int& x, int& y, int& w, int& h)` | `launcher/UiAutomationRunner.cpp` | Replaces `glGetIntegerv(GL_VIEWPORT,...)` |
| `SetClearColor(float r, float g, float b, float a)` | `editor/EditorLayer.cpp` | Replaces `glClearColor` + `glClear` |
| `SetDepthTest(bool)` / `SetCullFace(bool)` / `SetBlend(bool)` | `editor/EditorLayer.cpp` | State management currently done inline |

Resource-level factory methods (new `IResourceFactory` interface or added to backend):

| Method | Replaces |
|--------|---------|
| `CreateShader(vertSrc, fragSrc) -> IShader*` | `Shader::FromSource` direct GL |
| `CreateTexture(pixels, w, h, format) -> ITexture*` | `Texture::FromFile` / `CreateWhite1x1` direct GL |
| `CreateMeshBuffer(vertices, indices) -> IMeshBuffer*` | `Mesh::Upload` / `SkinnedMesh::Upload` direct GL |
| `CreateDebugLineBuffer(maxLines) -> IDebugLineBuffer*` | `DebugDraw` internal GL setup |
| `CreateDebugHudBuffer(maxGlyphs) -> IDebugHudBuffer*` | `DebugHUD` internal GL setup |

---

## Resource Types to Abstract

Based on the audit, the following abstract resource interfaces need to be created (Goal 2):

| Interface | Current Concrete Type | Key Operations |
|-----------|-----------------------|----------------|
| `IShader` | `Shader` (fully GL) | `Bind()`, `Unbind()`, `SetUniform*(name, value)`, `SetMat4Array(name, count, data)` |
| `ITexture` | `Texture` (fully GL) | `Bind(slot)`, `Unbind()`, `GetNativeHandle()`, `GetRenderTargetHandle()` |
| `IMeshBuffer` | `Mesh::GpuStorage` (GL VAO/VBO/EBO) | `Draw()`, `DrawWireframe()` |
| `ISkinnedMeshBuffer` | `SkinnedMesh::GpuStorage` | `Draw()` |
| `IDebugLineBuffer` | `DebugDraw` static GL state | `Update(lines)`, `Draw()`, `Release()` |
| `IDebugHudBuffer` | `DebugHUD` static GL state | `UpdateGlyphs(glyphs)`, `Draw()`, `Release()` |
| `IFramebuffer` | Raw GL FBO in `EditorLayer` | `Bind()`, `Unbind()`, `Resize(w,h)`, `GetColorTextureHandle()` |

---

## Files That Are Already Clean

The following files in `renderer/` have **no direct OpenGL coupling** and do not need to change
during the abstraction (they operate purely in terms of higher-level renderer concepts):

| File | Notes |
|------|-------|
| `renderer/Renderer.h` / `Renderer.cpp` | Fully delegates to `IRenderBackend`; only soft issue is "OpenGL" in one assert message |
| `renderer/IRenderBackend.h` | Clean abstract interface |
| `renderer/RenderBackend.h` / `RenderBackend.cpp` | Backend ID enum and capability structs — no GL |
| `renderer/RenderBackendFactory.h` | Header is clean; `.cpp` includes OpenGL backend header (by design) |
| `renderer/RenderTypes.h` | Pure data types, no GL |
| `renderer/RenderTargetHandle.h` | Has `OpenGLTexture()` factory method — needs renaming in Goal 2, but no GL calls |
| `renderer/RenderContext.h` / `RenderContext.cpp` | No GL coupling found |
| `renderer/Camera.h` / `Camera.cpp` | Math only |
| `renderer/Light.h` | Data struct only |
| `renderer/Material.h` | Data struct — references `Shader` and `Texture` but no GL directly |
| `renderer/AnimationClip.h` / `AnimationClip.cpp` | No GL coupling |
| `renderer/Skeleton.h` / `Skeleton.cpp` | No GL coupling |
| `renderer/GltfLoader.h` / `GltfLoader.cpp` | Parses geometry, no GL |
| `renderer/ObjLoader.h` / `ObjLoader.cpp` | Parses geometry, no GL |
| `renderer/MeshCache.h` / `MeshCache.cpp` | Cache layer, no GL |
| `renderer/SkinnedVertex.h` | Vertex struct, no GL |
| `renderer/VulkanRenderBackend.h` / `.cpp` | Backend implementation (out of scope) |
| `renderer/OpenGLRenderBackend.h` / `.cpp` | Backend implementation (intentionally GL) |
