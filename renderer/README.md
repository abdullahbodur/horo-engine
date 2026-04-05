# Renderer Module

`renderer/` contains the OpenGL 4.1 rendering stack and asset-loading pipeline.

## Responsibilities

- GPU draw orchestration (`Renderer`)
- Camera view/projection control (`Camera`)
- Mesh abstractions (`Mesh`, `SkinnedMesh`) and draw submission
- Material/shader bindings (`Material`, `Shader`)
- Texture loading and binding (`Texture`)
- Lighting data (`Light`) and debug rendering helpers (`DebugDraw`, `DebugHUD`)
- Asset import
  - OBJ: `ObjLoader`
  - glTF/GLB skeletal assets: `GltfLoader`, `Skeleton`, `AnimationClip`

## Main APIs

- `Renderer::BeginScene(camera)`
- `Renderer::SetLights(lights)`
- `Renderer::Submit(mesh, modelMatrix, material)`
- `Renderer::SubmitSkinned(skinnedMesh, modelMatrix, material, boneMatrices)`
- `Renderer::EndScene()`

## Geometry Sources

- Procedural primitives (`Mesh::CreateSphere`, `CreateBox`, `CreatePlane`, etc.)
- Imported OBJ meshes (triangulated input expected)
- Skinned meshes from glTF/GLB for animation workflows

## Notes

- `Mesh`/`SkinnedMesh` are move-only RAII wrappers over VAO/VBO/EBO resources.
- `Material` can use either solid color or optional `albedoMap` texture.
- Shaders are copied to `build/.../bin/shaders` by CMake post-build command.
