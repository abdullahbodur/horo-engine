# Renderer Module

`renderer/` contains the rendering foundation, its OpenGL backend, and the asset-loading pipeline.

## Responsibilities

- GPU draw orchestration (`Renderer`) and backend seam types (`IRenderBackend`, `RenderFrameConfig`, `RenderPassConfig`)
- Camera view/projection control (`Camera`)
- Mesh abstractions (`Mesh`, `SkinnedMesh`) and draw submission
- Material/shader bindings (`Material`, `Shader`)
- Texture loading and binding (`Texture`)
- Lighting data (`Light`) and debug rendering helpers (`DebugDraw`, `DebugHUD`)
- Asset import
  - OBJ: `ObjLoader`
  - glTF/GLB skeletal assets: `GltfLoader`, `Skeleton`, `AnimationClip`

## Main APIs

- `Renderer::BeginFrame(frameConfig)`
- `Renderer::BeginPass(passConfig)`
- `Renderer::Submit(mesh, modelMatrix, material)`
- `Renderer::SubmitSkinned(skinnedMesh, modelMatrix, material, boneMatrices)`
- `Renderer::SubmitWireframe(mesh, modelMatrix, shader, r, g, b)`
- `Renderer::EndPass()`
- `Renderer::EndFrame()`

`RenderFrameConfig` now includes `giPipeline` toggles for scaffolded GI stages (SSR, SSGI, temporal resolve, composite). These stages are disabled by default and require explicit per-frame opt-in.

## Geometry Sources

- Procedural primitives (`Mesh::CreateSphere`, `CreateBox`, `CreatePlane`, etc.)
- Imported OBJ meshes (triangulated input expected)
- Skinned meshes from glTF/GLB for animation workflows

## Notes

- `Mesh`/`SkinnedMesh` are move-only RAII wrappers over VAO/VBO/EBO resources.
- `Material` can use either solid color or optional `albedoMap` texture.
- Shaders are copied to `build/.../bin/shaders` by CMake post-build command.
- Vulkan supports GI scaffold pass IDs (`ScreenSpaceReflections`, `ScreenSpaceGlobalIllumination`, `TemporalGiResolve`, `GiComposite`) behind frame-level enable flags.
