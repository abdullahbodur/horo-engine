/** @file LauncherProjectTemplate.cpp
 *  @brief Implements scaffold generation for new launcher projects. */
#include "ui/launcher/LauncherProjectTemplate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>

#include "ui/editor/SceneDocument.h"
#include "ui/editor/SceneSerializer.h"

namespace Horo::Launcher {
    namespace fs = std::filesystem;

    namespace {
        /** @brief Writes a string to a file, creating parent directories as needed. */
        bool WriteTextFile(const fs::path &path, const std::string &content,
                           std::string *outError) {
            if (outError)
                outError->clear();

            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec) {
                if (outError)
                    *outError = ec.message();
                return false;
            }

            std::ofstream out(path);
            if (!out.is_open()) {
                if (outError)
                    *outError = "Failed to open file for writing: " + path.string();
                return false;
            }
            out << content;
            return true;
        }

        /** @brief Loads a canonical renderer shader from an SDK layout when available. */
        std::optional<std::string>
        LoadSdkShader(const fs::path &sdkRoot, const fs::path &relativePath) {
            const std::array candidates = {
                sdkRoot / relativePath,
                sdkRoot / "sdk" / relativePath,
            };
            for (const fs::path &candidate : candidates) {
                std::ifstream in(candidate);
                if (!in.is_open())
                    continue;
                return std::string(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
            }
            return std::nullopt;
        }

        /** @brief Prefers the SDK's canonical shader over the embedded fallback. */
        std::string ResolveTemplateShader(const fs::path &sdkRoot,
                                          const fs::path &relativePath,
                                          std::string fallback) {
            return LoadSdkShader(sdkRoot, relativePath).value_or(std::move(fallback));
        }

        /** @brief Generates the main.cpp source content for a new project template. */
        /** @brief Generates the main.cpp source content for a new project template. */
                /** @brief Generates the main.cpp source content for a new project template. */
        std::string BuildTemplateMainCpp(const std::string &projectName) {
            return
                   "#include <future>\n"
                   "#include <memory>\n"
                   "#include <stdexcept>\n"
                   "#include <string>\n"
                   "#include <unordered_map>\n"
                   "\n"
                   "#include \"core/Application.h\"\n"
                   "#include \"core/AssetProvider.h\"\n"
                   "#include \"core/Window.h\"\n"
                   "#include \"input/FPSCameraController.h\"\n"
                   "#include \"input/Input.h\"\n"
                   "#include \"input/KeyCodes.h\"\n"
                   "#include \"math/MathUtils.h\"\n"
                   "#include \"math/Transform.h\"\n"
                   "#include \"physics/BoxCollider.h\"\n"
                   "#include \"physics/PhysicsWorld.h\"\n"
                   "#include \"renderer/DebugDraw.h\"\n"
                   "#include \"renderer/Light.h\"\n"
                   "#include \"renderer/Material.h\"\n"
                   "#include \"renderer/ObjLoader.h\"\n"
                   "#include \"renderer/RenderViewUtils.h\"\n"
                   "#include \"renderer/Renderer.h\"\n"
                   "#include \"renderer/Shader.h\"\n"
                   "#include \"renderer/Texture.h\"\n"
                   "#include \"scene/Registry.h\"\n"
                   "#include \"scene/Scene.h\"\n"
                   "#include \"scene/SceneReferenceRuntime.h\"\n"
                   "#include \"scene/components/BehaviorComponent.h\"\n"
                   "#include \"scene/components/MeshComponent.h\"\n"
                   "#include \"scene/components/PlayerTagComponent.h\"\n"
                   "#include \"scene/components/RigidBodyComponent.h\"\n"
                   "#include \"scene/components/TransformComponent.h\"\n"
                   "#include \"scene/systems/BehaviorSystem.h\"\n"
                   "#include \"scene/systems/PhysicsSystem.h\"\n"
                   "#include \"scene/systems/RenderSystem.h\"\n"
                   "#include \"ui/editor/SceneDocument.h\"\n"
                   "#include \"ui/editor/SceneSerializer.h\"\n"
                   "\n"
                   "namespace {\n"
                   "\n"
                   "using namespace Horo;\n"
                   "\n"
                   "// ── Game states ────────────────────────────────────────────────────────────\n"
                   "\n"
                   "enum class GameState { Loading, Ready };\n"
                   "\n"
                   "// ── Asset loading (cached) ─────────────────────────────────────────────────\n"
                   "\n"
                   "std::shared_ptr<Shader> LoadDefaultShader() {\n"
                   "    static std::shared_ptr<Shader> shader;\n"
                   "    if (!shader) {\n"
                   "        shader = std::make_shared<Shader>(\n"
                   "            Shader::FromFiles(\"shaders/basic.vert\", \"shaders/basic.frag\"));\n"
                   "    }\n"
                   "    return shader;\n"
                   "}\n"
                   "\n"
                   "std::shared_ptr<Mesh> LoadMeshForTag(const std::string& meshTag) {\n"
                   "    static std::unordered_map<std::string, std::shared_ptr<Mesh>> cache;\n"
                   "    if (const auto it = cache.find(meshTag); it != cache.end())\n"
                   "        return it->second;\n"
                   "\n"
                   "    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();\n"
                   "    if (meshTag == \"box\" || meshTag == \"cube\")\n"
                   "        *mesh = Mesh::CreateBox();\n"
                   "    else if (meshTag == \"sphere\")\n"
                   "        *mesh = Mesh::CreateSphere();\n"
                   "    else if (meshTag == \"cylinder\" || meshTag == \"capsule\")\n"
                   "        *mesh = Mesh::CreateCylinder();\n"
                   "    else if (meshTag == \"pyramid\")\n"
                   "        *mesh = Mesh::CreatePyramid();\n"
                   "    else if (meshTag == \"plane\")\n"
                   "        *mesh = Mesh::CreatePlane();\n"
                   "    else if (meshTag == \"quad\")\n"
                   "        *mesh = Mesh::CreateQuad();\n"
                   "    else\n"
                   "        *mesh = ObjLoader::Load(meshTag);\n"
                   "    cache[meshTag] = mesh;\n"
                   "    return mesh;\n"
                   "}\n"
                   "\n"
                   "std::shared_ptr<Texture> LoadTextureIfPresent(const std::string& path) {\n"
                   "    if (path.empty())\n"
                   "        return std::shared_ptr<Texture>();\n"
                   "    static std::unordered_map<std::string, std::shared_ptr<Texture>> cache;\n"
                   "    if (const auto it = cache.find(path); it != cache.end())\n"
                   "        return it->second;\n"
                   "    auto texture = std::make_shared<Texture>(Texture::FromFile(path));\n"
                   "    cache[path] = texture;\n"
                   "    return texture;\n"
                   "}\n"
                   "\n"
                   "// ── Scene load result (produced on worker thread) ─────────────────────────\n"
                   "\n"
                   "struct SceneLoadResult {\n"
                   "    bool ok = false;\n"
                   "    std::string error;\n"
                   "    Editor::SceneDocument document;\n"
                   "    std::vector<Light> lights;\n"
                   "    std::optional<RuntimeSceneCamera> sceneCamera;\n"
                   "};\n"
                   "\n"
                   "/** @brief Reads and parses the scene file on a worker thread.\n"
                   " *  ECS mutations happen later on the main thread. */\n"
                   "SceneLoadResult LoadSceneAsync(const std::string& scenePath) {\n"
                   "    SceneLoadResult result;\n"
                   "\n"
                   "    std::string assetError;\n"
                   "    const auto sceneJson = ReadAssetText(scenePath, &assetError);\n"
                   "    if (!sceneJson) {\n"
                   "        result.error = \"Failed to read scene: \" + assetError;\n"
                   "        return result;\n"
                   "    }\n"
                   "\n"
                   "    const auto doc = Editor::SceneSerializer::LoadFromString(\n"
                   "        *sceneJson, scenePath);\n"
                   "    result.document = doc;\n"
                   "    result.ok = true;\n"
                   "    return result;\n"
                   "}\n"
                   "\n"
                   "// ── Player behavior ────────────────────────────────────────────────────────\n"
                   "\n"
                   "class PlayerBehavior final : public Behavior {\n"
                   "public:\n"
                   "    PlayerBehavior() = default;\n"
                   "\n"
                   "    void SetMoveSpeed(float v) { m_moveSpeed = v; }\n"
                   "    void SetJumpImpulse(float v) { m_jumpImpulse = v; }\n"
                   "    float GetYaw() const { return m_cameraController.GetYaw(); }\n"
                   "    float GetPitch() const { return m_cameraController.GetPitch(); }\n"
                   "\n"
                   "    void OnUpdate(Entity self, Registry& reg, float dt) override {\n"
                   "        m_cameraController.Update(dt);\n"
                   "\n"
                   "        if (!reg.Has<RigidBodyComponent>(self) ||\n"
                   "            !reg.Has<TransformComponent>(self))\n"
                   "            return;\n"
                   "\n"
                   "        auto& rb = reg.Get<RigidBodyComponent>(self);\n"
                   "        if (!rb.body) return;\n"
                   "\n"
                   "        const float yawRad = ToRadians(m_cameraController.GetYaw());\n"
                   "        const Vec3 forward = {-Sin(yawRad), 0.0f, -Cos(yawRad)};\n"
                   "        const Vec3 right = {Cos(yawRad), 0.0f, -Sin(yawRad)};\n"
                   "\n"
                   "        Vec3 moveDir = Vec3::Zero();\n"
                   "        if (Input::IsKeyDown(Key::W)) moveDir += forward;\n"
                   "        if (Input::IsKeyDown(Key::S)) moveDir -= forward;\n"
                   "        if (Input::IsKeyDown(Key::A)) moveDir -= right;\n"
                   "        if (Input::IsKeyDown(Key::D)) moveDir += right;\n"
                   "\n"
                   "        if (moveDir.LengthSq() > 0.0f) {\n"
                   "            moveDir = moveDir.Normalized();\n"
                   "            Vec3 vel = rb.body->velocity;\n"
                   "            vel.x = moveDir.x * m_moveSpeed;\n"
                   "            vel.z = moveDir.z * m_moveSpeed;\n"
                   "            rb.body->velocity = vel;\n"
                   "        } else {\n"
                   "            Vec3 vel = rb.body->velocity;\n"
                   "            vel.x *= 0.9f;\n"
                   "            vel.z *= 0.9f;\n"
                   "            rb.body->velocity = vel;\n"
                   "        }\n"
                   "\n"
                   "        if (Input::IsKeyPressed(Key::Space)) {\n"
                   "            if (rb.body->velocity.y > -0.5f && rb.body->velocity.y < 0.5f) {\n"
                   "                rb.body->velocity.y = m_jumpImpulse;\n"
                   "            }\n"
                   "        }\n"
                   "    }\n"
                   "\n"
                   "private:\n"
                   "    FPSCameraController m_cameraController;\n"
                   "    float m_moveSpeed = 6.0f;\n"
                   "    float m_jumpImpulse = 8.0f;\n"
                   "};\n"
                   "\n"
                   "// ── Game application ───────────────────────────────────────────────────────\n"
                   "\n"
                   "class GameApp final : public Application {\n"
                   "public:\n"
                   "    GameApp() : Application(BuildSpec()) {\n"
                   "        m_scene.AddSystem(std::make_unique<BehaviorSystem>());\n"
                   "        m_scene.AddSystem(\n"
                   "            std::make_unique<PhysicsSystem>(m_scene.GetPhysics()));\n"
                   "        m_scene.AddRenderSystem(\n"
                   "            std::make_unique<RenderSystem>(m_renderAlpha));\n"
                   "        m_referenceRuntime =\n"
                   "            std::make_unique<SceneReferenceRuntime>(&m_scene);\n"
                   "        m_referenceRuntime->SetPropEntityCreatedCallback(\n"
                   "            [this](const RuntimeSceneProp& prop, Entity entity,\n"
                   "                   Scene& sceneRef) {\n"
                   "                MeshComponent component;\n"
                   "                component.meshTag = prop.meshTag;\n"
                   "                component.mesh = LoadMeshForTag(prop.meshTag);\n"
                   "                component.material = std::make_shared<Material>();\n"
                   "                component.material->shader = LoadDefaultShader();\n"
                   "                component.material->albedoMap =\n"
                   "                    LoadTextureIfPresent(prop.albedoMap);\n"
                   "\n"
                   "                if (prop.id.find(\"wall_\") != std::string::npos)\n"
                   "                    component.material->color = {0.55f, 0.50f, 0.45f, 1.0f};\n"
                   "                else if (prop.id.find(\"_red_\") != std::string::npos)\n"
                   "                    component.material->color = {0.9f, 0.15f, 0.15f, 1.0f};\n"
                   "                else if (prop.id.find(\"_green_\") != std::string::npos)\n"
                   "                    component.material->color = {0.15f, 0.85f, 0.2f, 1.0f};\n"
                   "                else if (prop.id.find(\"_yellow_\") != std::string::npos)\n"
                   "                    component.material->color = {1.0f, 0.85f, 0.1f, 1.0f};\n"
                   "                else if (prop.id.find(\"_blue_\") != std::string::npos)\n"
                   "                    component.material->color = {0.2f, 0.35f, 1.0f, 1.0f};\n"
                   "                else if (prop.id.find(\"_gold_\") != std::string::npos)\n"
                   "                    component.material->color = {1.0f, 0.7f, 0.05f, 1.0f};\n"
                   "                else if (prop.id.find(\"_purple_\") != std::string::npos)\n"
                   "                    component.material->color = {0.65f, 0.25f, 1.0f, 1.0f};\n"
                   "                else if (prop.id.find(\"_brown_\") != std::string::npos)\n"
                   "                    component.material->color = {0.55f, 0.35f, 0.2f, 1.0f};\n"
                   "                else\n"
                   "                    component.material->color = {0.7f, 0.7f, 0.7f, 1.0f};\n"
                   "\n"
                   "                sceneRef.GetRegistry().Add<MeshComponent>(\n"
                   "                    entity, std::move(component));\n"
                   "            });\n"
                   "    }\n"
                   "\n"
                   "private:\n"
                   "    static AppSpec BuildSpec() {\n"
                   "        AppSpec spec;\n"
                   "        spec.name = \"" + projectName + "\";\n"
                   "        spec.width = 1600;\n"
                   "        spec.height = 900;\n"
                   "        spec.defaultSceneFile = \"assets/scenes/level.json\";\n"
                   "        return spec;\n"
                   "    }\n"
                   "\n"
                   "    // ── Loading ────────────────────────────────────────────────────────────\n"
                   "\n"
                   "    void OnInit() override {\n"
                   "        const RenderBackendInitResult backend =\n"
                   "            Renderer::InitializeBackend(\n"
                   "                {.requested = RenderBackendId::OpenGL,\n"
                   "                 .nativeWindowHandle = GetWindow().GetNativeHandle()});\n"
                   "        if (!backend.ok)\n"
                   "            throw std::runtime_error(\"Failed to initialize renderer backend: \" +\n"
                   "                                     backend.error);\n"
                   "\n"
                   "        DebugDraw::Init();\n"
                   "        Input::Init(GetWindow().GetNativeHandle());\n"
                   "        GetWindow().SetCursorMode(CursorMode::Disabled);\n"
                   "\n"
                   "        m_state = GameState::Loading;\n"
                   "\n"
                   "        // Kick off async scene file read + parse on worker thread\n"
                   "        if (!GetDefaultSceneFilePath().empty()) {\n"
                   "            m_loadFuture = std::async(std::launch::async, [this]() {\n"
                   "                return LoadSceneAsync(GetDefaultSceneFilePath());\n"
                   "            });\n"
                   "        } else {\n"
                   "            // No scene file — go straight to ready\n"
                   "            FinalizeLoading();\n"
                   "        }\n"
                   "    }\n"
                   "\n"
                   "    /** @brief Called on the main thread once the async scene parse completes. */\n"
                   "    void FinalizeLoading() {\n"
                   "        const SceneRuntimeOperationResult result =\n"
                   "            m_referenceRuntime->LoadDocument(m_loadResult.document);\n"
                   "        if (!result.ok) {\n"
                   "            throw std::runtime_error(\"Failed to load scene: \" + result.error);\n"
                   "        }\n"
                   "\n"
                   "        m_lights = m_referenceRuntime->GetLights();\n"
                   "\n"
                   "        if (m_referenceRuntime->GetSceneCamera().has_value()) {\n"
                   "            const RuntimeSceneCamera& cam =\n"
                   "                *m_referenceRuntime->GetSceneCamera();\n"
                   "            m_camera.position = cam.position;\n"
                   "            const float yawRad = ToRadians(cam.yaw);\n"
                   "            const float pitchRad = ToRadians(cam.pitch);\n"
                   "            const Vec3 forward = {\n"
                   "                -Sin(yawRad) * Cos(pitchRad),\n"
                   "                Sin(pitchRad),\n"
                   "                -Cos(yawRad) * Cos(pitchRad),\n"
                   "            };\n"
                   "            m_camera.target = cam.position + forward;\n"
                   "            m_camera.fovY = cam.fovY;\n"
                   "            m_camera.zNear = cam.nearClip;\n"
                   "            m_camera.zFar = cam.farClip;\n"
                   "        }\n"
                   "\n"
                   "        CreatePlayer();\n"
                   "        m_state = GameState::Ready;\n"
                   "    }\n"
                   "\n"
                   "    void CreatePlayer() {\n"
                   "        const Vec3 spawnPos =\n"
                   "            m_referenceRuntime->GetCoordinator()\n"
                   "                .GetCurrentDefinition()\n"
                   "                ? m_referenceRuntime->GetCoordinator()\n"
                   "                      .GetCurrentDefinition()\n"
                   "                      ->spawnPoint\n"
                   "                : Vec3{0.0f, 2.0f, 0.0f};\n"
                   "\n"
                   "        Entity player = m_scene.CreateEntity(spawnPos);\n"
                   "\n"
                   "        {\n"
                   "            MeshComponent mc;\n"
                   "            mc.meshTag = \"box\";\n"
                   "            mc.mesh = LoadMeshForTag(\"box\");\n"
                   "            mc.material = std::make_shared<Material>();\n"
                   "            mc.material->shader = LoadDefaultShader();\n"
                   "            mc.material->color = {0.2f, 0.5f, 0.9f, 1.0f};\n"
                   "            m_scene.GetRegistry().Add<MeshComponent>(player, std::move(mc));\n"
                   "        }\n"
                   "\n"
                   "        {\n"
                   "            RigidBody body =\n"
                   "                RigidBody::MakeBox({0.5f, 1.0f, 0.5f}, 1.0f);\n"
                   "            body.position = spawnPos;\n"
                   "            body.linearDamping = 0.1f;\n"
                   "            body.angularDamping = 1.0f;\n"
                   "            body.friction = 0.8f;\n"
                   "            body.restitution = 0.0f;\n"
                   "            body.invMass = 1.0f;\n"
                   "            RigidBody* bodyPtr = m_scene.GetPhysics().AddBody(std::move(body));\n"
                   "\n"
                   "            RigidBodyComponent rbc;\n"
                   "            rbc.body = bodyPtr;\n"
                   "            m_scene.GetRegistry().Add<RigidBodyComponent>(player, rbc);\n"
                   "        }\n"
                   "\n"
                   "        m_scene.GetRegistry().Add<PlayerTagComponent>(player);\n"
                   "\n"
                   "        {\n"
                   "            auto behavior = std::make_unique<PlayerBehavior>();\n"
                   "            behavior->SetMoveSpeed(6.0f);\n"
                   "            behavior->SetJumpImpulse(8.0f);\n"
                   "            m_playerBehavior = behavior.get();\n"
                   "            BehaviorComponent bc;\n"
                   "            bc.behavior = std::move(behavior);\n"
                   "            m_scene.GetRegistry().Add<BehaviorComponent>(player, std::move(bc));\n"
                   "        }\n"
                   "\n"
                   "        m_playerEntity = player;\n"
                   "    }\n"
                   "\n"
                   "    // ── Per-frame ──────────────────────────────────────────────────────────\n"
                   "\n"
                   "    void OnUpdate(float dt) override {\n"
                   "        // Check async load completion\n"
                   "        if (m_state == GameState::Loading) {\n"
                   "            if (m_loadFuture.valid() &&\n"
                   "                m_loadFuture.wait_for(std::chrono::seconds(0)) ==\n"
                   "                    std::future_status::ready) {\n"
                   "                m_loadResult = m_loadFuture.get();\n"
                   "                if (!m_loadResult.ok) {\n"
                   "                    throw std::runtime_error(\"Scene load failed: \" +\n"
                   "                                             m_loadResult.error);\n"
                   "                }\n"
                   "                FinalizeLoading();\n"
                   "            }\n"
                   "            return;\n"
                   "        }\n"
                   "\n"
                   "        Input::Poll();\n"
                   "\n"
                   "        if (m_playerEntity != INVALID_ENTITY &&\n"
                   "            m_scene.GetRegistry().Has<TransformComponent>(m_playerEntity)) {\n"
                   "            const auto& tc =\n"
                   "                m_scene.GetRegistry().Get<TransformComponent>(m_playerEntity);\n"
                   "            const Vec3 playerPos = tc.current.position;\n"
                   "\n"
                   "            if (m_playerBehavior) {\n"
                   "                const float yawRad = ToRadians(m_playerBehavior->GetYaw());\n"
                   "                const float pitchRad = ToRadians(m_playerBehavior->GetPitch());\n"
                   "                const float camDist = 8.0f;\n"
                   "                const float camHeight = 3.0f;\n"
                   "\n"
                   "                const Vec3 offset = {\n"
                   "                    -Sin(yawRad) * Cos(pitchRad) * camDist,\n"
                   "                    Sin(pitchRad) * camDist + camHeight,\n"
                   "                    -Cos(yawRad) * Cos(pitchRad) * camDist,\n"
                   "                };\n"
                   "                m_camera.position = playerPos - offset;\n"
                   "                m_camera.target = playerPos + Vec3{0.0f, 1.0f, 0.0f};\n"
                   "            }\n"
                   "        }\n"
                   "    }\n"
                   "\n"
                   "    void OnFixedUpdate(float dt) override {\n"
                   "        if (m_state != GameState::Ready) return;\n"
                   "        m_scene.UpdateSystems(dt);\n"
                   "    }\n"
                   "\n"
                   "    void OnRender(float alpha) override {\n"
                   "        m_renderAlpha = alpha;\n"
                   "\n"
                   "        if (m_state == GameState::Loading) {\n"
                   "            // Simple loading screen\n"
                   "            RenderFrameConfig frameConfig;\n"
                   "            frameConfig.debugLabel = \"Loading-frame\";\n"
                   "            frameConfig.clearColor = {0.05f, 0.05f, 0.08f, 1.0f};\n"
                   "            frameConfig.clearColorBuffer = true;\n"
                   "            frameConfig.clearDepthBuffer = true;\n"
                   "\n"
                   "            Renderer::BeginFrame(frameConfig);\n"
                   "            // No scene pass — just clear + end\n"
                   "            Renderer::EndFrame();\n"
                   "            return;\n"
                   "        }\n"
                   "\n"
                   "        RenderFrameConfig frameConfig;\n"
                   "        frameConfig.debugLabel = \"" + projectName + "-frame\";\n"
                   "        frameConfig.lights = m_lights;\n"
                   "        frameConfig.clearColor = {0.15f, 0.15f, 0.2f, 1.0f};\n"
                   "\n"
                   "        Renderer::BeginFrame(frameConfig);\n"
                   "        Renderer::BeginPass({RenderPassId::OpaqueScene,\n"
                   "                             BuildRenderView(m_camera),\n"
                   "                             \"" + projectName + "-scene\"});\n"
                   "        m_scene.RenderSystems(alpha);\n"
                   "        DebugDraw::Flush(m_camera);\n"
                   "        Renderer::EndPass();\n"
                   "        Renderer::EndFrame();\n"
                   "    }\n"
                   "\n"
                   "    void OnShutdown() override {\n"
                   "        if (m_referenceRuntime)\n"
                   "            m_referenceRuntime->Unload();\n"
                   "    }\n"
                   "\n"
                   "    // ── State ──────────────────────────────────────────────────────────────\n"
                   "\n"
                   "    GameState m_state = GameState::Loading;\n"
                   "    SceneLoadResult m_loadResult;\n"
                   "    std::future<SceneLoadResult> m_loadFuture;\n"
                   "\n"
                   "    Scene m_scene;\n"
                   "    std::unique_ptr<SceneReferenceRuntime> m_referenceRuntime;\n"
                   "    Camera m_camera;\n"
                   "    float m_renderAlpha = 0.0f;\n"
                   "    Entity m_playerEntity = INVALID_ENTITY;\n"
                   "    PlayerBehavior* m_playerBehavior = nullptr;\n"
                   "    std::vector<Light> m_lights;\n"
                   "};\n"
                   "\n"
                   "} // namespace\n"
                   "\n"
                   "int main(int argc, char** argv) {\n"
                   "    GameApp app;\n"
                   "    app.ParseArgs(argc, argv);\n"
                   "    app.Run();\n"
                   "    return 0;\n"
                   "}\n"
                   ;
        }
std::string BuildTemplateCMakeLists(const std::string &projectName,
                                            const std::string &projectId) {
            return "cmake_minimum_required(VERSION 3.25)\n"
                   "project(" +
                   projectId +
                   " LANGUAGES CXX C)\n"
                   "\n"
                   "set(CMAKE_CXX_STANDARD 20)\n"
                   "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
                   "set(CMAKE_CXX_EXTENSIONS OFF)\n"
                   "\n"
                   "if(EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/engine/CMakeLists.txt\")\n"
                   "  add_subdirectory(engine)\n"
                   "  set(HORO_ENGINE_TARGET HoroEngine)\n"
                   "  set(HORO_RENDERER_TARGET HoroEngine::RendererOpenGL)\n"
                   "else()\n"
                   "  find_package(HoroEngine CONFIG REQUIRED)\n"
                   "  set(HORO_ENGINE_TARGET HoroEngine::HoroEngine)\n"
                   "  set(HORO_RENDERER_TARGET HoroEngine::RendererOpenGL)\n"
                   "endif()\n"
                   "\n"
                   "file(GLOB_RECURSE PROJECT_SOURCES src/*.cpp)\n"
                   "add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})\n"
                   "set_target_properties(${PROJECT_NAME} PROPERTIES\n"
                   "  OUTPUT_NAME \"" +
                   projectName +
                   "\"\n"
                   "  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin\n"
                   "  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin\n"
                   "  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin\n"
                   "  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin\n"
                   "  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin\n"
                   ")\n"
                   "target_link_libraries(${PROJECT_NAME} PRIVATE "
                   "${HORO_ENGINE_TARGET} ${HORO_RENDERER_TARGET})\n"
                   "\n"
                   "add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD\n"
                   "  COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
                   "    ${CMAKE_SOURCE_DIR}/assets\n"
                   "    $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets\n"
                   ")\n"
                   "\n"
                   "add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD\n"
                   "  COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
                   "    ${CMAKE_CURRENT_SOURCE_DIR}/shaders\n"
                   "    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders\n"
                   ")\n";
        }

        /** @brief Normalizes a renderer backend string to a known lowercase identifier. */
        std::string NormalizeRendererBackend(std::string rendererBackend) {
            std::ranges::transform(rendererBackend, rendererBackend.begin(),
                                   [](unsigned char c) {
                                       return static_cast<char>(std::tolower(c));
                                   });
            if (rendererBackend == "opengl" || rendererBackend == "vulkan" ||
                rendererBackend == "null")
                return rendererBackend;
            return "opengl";
        }

        /** @brief Constructs the default scene document for a new project template. */
        /** @brief Constructs the default scene document for a new project template. */
                /** @brief Constructs the default scene document for a new project template. */
                /** @brief Constructs the default scene document for a new project template. */
                /** @brief Constructs the default scene document for a new project template. */
                /** @brief Constructs the default scene document for a new project template. */
                Editor::SceneDocument BuildTemplateScene() {
            Editor::SceneDocument doc;
            doc.version = 1;
            doc.sceneId = "level";
            doc.sceneName = "Room";
            doc.filePath = "assets/scenes/level.json";
            doc.settings["spawnPoint"] = "0.0,2.0,8.0";

            // ── Floor (24x24, top at y=-0.5) ──
            Editor::SceneObject floor;
            floor.id = "floor_000";
            floor.type = Editor::SceneObjectType::Panel;
            floor.position = {0.0f, -1.0f, 0.0f};
            floor.scale = {12.0f, 0.5f, 12.0f};
            doc.objects.push_back(floor);

            Editor::SceneObject floorVis;
            floorVis.id = "floor_vis";
            floorVis.type = Editor::SceneObjectType::Prop;
            floorVis.position = {0.0f, -0.5f, 0.0f};
            floorVis.scale = {24.0f, 0.1f, 24.0f};
            floorVis.props["mesh"] = "box";
            doc.objects.push_back(floorVis);

            // ── Wall colliders ──
            auto addWallPanel = [&](const char* id, float x, float y, float z,
                                     float hx, float hy, float hz) {
                Editor::SceneObject w;
                w.id = id;
                w.type = Editor::SceneObjectType::Panel;
                w.position = {x, y, z};
                w.scale = {hx, hy, hz};
                doc.objects.push_back(w);
            };
            addWallPanel("wall_north",  0.0f, 2.0f, -12.0f, 12.0f, 2.5f, 0.15f);
            addWallPanel("wall_south",  0.0f, 2.0f,  12.0f, 12.0f, 2.5f, 0.15f);
            addWallPanel("wall_east",  12.0f, 2.0f,  0.0f, 0.15f, 2.5f, 12.0f);
            addWallPanel("wall_west", -12.0f, 2.0f,  0.0f, 0.15f, 2.5f, 12.0f);

            // ── Wall visuals ──
            auto addWallVis = [&](const char* id, float x, float y, float z,
                                   float sx, float sy, float sz) {
                Editor::SceneObject w;
                w.id = id;
                w.type = Editor::SceneObjectType::Prop;
                w.position = {x, y, z};
                w.scale = {sx, sy, sz};
                w.props["mesh"] = "box";
                doc.objects.push_back(w);
            };
            addWallVis("wall_north_vis",  0.0f, 2.0f, -12.0f, 24.0f, 5.0f, 0.3f);
            addWallVis("wall_south_vis",  0.0f, 2.0f,  12.0f, 24.0f, 5.0f, 0.3f);
            addWallVis("wall_east_vis",  12.0f, 2.0f,  0.0f, 0.3f, 5.0f, 24.0f);
            addWallVis("wall_west_vis", -12.0f, 2.0f,  0.0f, 0.3f, 5.0f, 24.0f);

            // ── Lighting ──
            Editor::SceneObject sunLight;
            sunLight.id = "light_sun";
            sunLight.type = Editor::SceneObjectType::Light;
            sunLight.position = {0.0f, 10.0f, 0.0f};
            sunLight.pitch = -35.0f;
            sunLight.yaw = -35.0f;
            sunLight.props["lightType"] = "directional";
            sunLight.props["color"] = "1.0,0.96,0.88";
            sunLight.props["intensity"] = "1.0";
            doc.objects.push_back(sunLight);

            Editor::SceneObject centerLight;
            centerLight.id = "light_center";
            centerLight.type = Editor::SceneObjectType::Light;
            centerLight.position = {0.0f, 4.5f, 0.0f};
            centerLight.props["lightType"] = "point";
            centerLight.props["color"] = "1.0,0.95,0.85";
            centerLight.props["intensity"] = "8.0";
            centerLight.props["radius"] = "15.0";
            doc.objects.push_back(centerLight);

            Editor::SceneObject nwLight;
            nwLight.id = "light_nw";
            nwLight.type = Editor::SceneObjectType::Light;
            nwLight.position = {-8.0f, 4.0f, -8.0f};
            nwLight.props["lightType"] = "point";
            nwLight.props["color"] = "0.6,0.7,1.0";
            nwLight.props["intensity"] = "4.0";
            nwLight.props["radius"] = "10.0";
            doc.objects.push_back(nwLight);

            Editor::SceneObject seLight;
            seLight.id = "light_se";
            seLight.type = Editor::SceneObjectType::Light;
            seLight.position = {8.0f, 4.0f, 8.0f};
            seLight.props["lightType"] = "point";
            seLight.props["color"] = "1.0,0.8,0.5";
            seLight.props["intensity"] = "4.0";
            seLight.props["radius"] = "10.0";
            doc.objects.push_back(seLight);

            // ── Camera ──
            Editor::SceneObject camera;
            camera.id = "cam_000";
            camera.type = Editor::SceneObjectType::Camera;
            camera.position = {0.0f, 5.0f, 15.0f};
            camera.pitch = -10.0f;
            camera.props["fov"] = "60";
            doc.objects.push_back(camera);

            return doc;
        }
    } // namespace

    bool CreateLauncherProjectTemplate(
        const LauncherProjectTemplateRequest &request,
        LauncherProjectDocument *outProjectDocument, std::string *outError) {
        if (outError)
            outError->clear();

        if (request.projectRoot.empty()) {
            if (outError)
                *outError = "Project root is empty.";
            return false;
        }
        if (request.projectName.empty()) {
            if (outError)
                *outError = "Project name is empty.";
            return false;
        }

        std::error_code ec;
        fs::create_directories(request.projectRoot, ec);
        if (ec) {
            if (outError)
                *outError = ec.message();
            return false;
        }

        const fs::path normalizedRoot = fs::weakly_canonical(request.projectRoot, ec);
        const fs::path projectRoot = ec ? request.projectRoot : normalizedRoot;
        const std::string projectId = SanitizeProjectId(request.projectName);
        const std::string rendererBackend =
                NormalizeRendererBackend(request.rendererBackend);
        const std::string exeName =
#ifdef _WIN32
                request.projectName + ".exe";
#else
        request.projectName;
#endif

        LauncherProjectDocument doc;
        doc.manifest.schemaVersion = 1;
        doc.manifest.projectId = projectId;
        doc.manifest.projectName = request.projectName;
        doc.manifest.defaultScene = "assets/scenes/level.json";
        doc.manifest.configureCommand = {
            "cmake",
            {
                "-S", "${projectDir}", "-B",
                "${projectDir}/build",
                "-DCMAKE_PREFIX_PATH=${horoSdkRoot}"
            },
            "${projectDir}"
        };
        // Append renderer flag only for real backends; passing -DHORO_RENDERER=null
        // generates projects that fail at startup since the template still links OpenGL.
        if (rendererBackend != "null" && !rendererBackend.empty())
          doc.manifest.configureCommand.args.push_back(
              "-DHORO_RENDERER=" + rendererBackend);
        doc.manifest.buildCommand = {
            "cmake",
            {"--build", "${projectDir}/build", "--config", "Debug"},
            "${projectDir}"
        };
        doc.manifest.runCommand = {
            "${projectDir}/build/bin/" + exeName, {}, "${projectDir}"
        };

        if (!WriteTextFile(projectRoot / "src" / "main.cpp",
                           BuildTemplateMainCpp(request.projectName), outError))
            return false;
        if (!WriteTextFile(projectRoot / "CMakeLists.txt",
                           BuildTemplateCMakeLists(request.projectName, projectId),
                           outError))
            return false;

        if (!WriteTextFile(projectRoot / "shaders" / "basic.vert",
                           ResolveTemplateShader(
                               request.sdkRoot, "renderer/shaders/basic.vert",
                           "#version 410 core\n"
                           "layout(location = 0) in vec3 aPos;\n"
                           "layout(location = 1) in vec3 aNormal;\n"
                           "layout(location = 2) in vec2 aUv;\n"
                           "\n"
                           "uniform mat4 u_model;\n"
                           "uniform mat4 u_view;\n"
                           "uniform mat4 u_projection;\n"
                           "\n"
                           "out vec3 v_normal;\n"
                           "out vec3 v_worldPos;\n"
                           "out vec2 v_uv;\n"
                           "\n"
                           "void main() {\n"
                           "    v_uv = aUv;\n"
                           "    vec4 worldPos = u_model * vec4(aPos, 1.0);\n"
                           "    v_worldPos = worldPos.xyz;\n"
                           "    // Use transpose(inverse) for correct normal transform under non-uniform scale.\n"
                           "    // For uniform scale meshes, mat3(u_model) is sufficient and faster.\n"
                           "    v_normal = normalize(mat3(transpose(inverse(u_model))) * aNormal);\n"
                           "    gl_Position = u_projection * u_view * worldPos;\n"
                           "}\n"),
                           outError))
            return false;

        if (!WriteTextFile(projectRoot / "shaders" / "basic.frag",
                           ResolveTemplateShader(
                               request.sdkRoot, "renderer/shaders/basic.frag",
                           "#version 410 core\n"
                           "\n"
                           "in vec3 v_normal;\n"
                           "in vec3 v_worldPos;\n"
                           "in vec2 v_uv;\n"
                           "\n"
                           "out vec4 FragColor;\n"
                           "\n"
                           "// Material uniforms (set by engine BindMaterial)\n"
                           "uniform vec4 u_color;\n"
                           "uniform sampler2D u_albedoMap;\n"
                           "uniform int u_hasTexture;\n"
                           "uniform float u_roughness;\n"
                           "uniform float u_metallic;\n"
                           "\n"
                           "// Camera (set by engine per draw)\n"
                           "uniform vec3 u_cameraPos;\n"
                           "uniform mat4 u_view;\n"
                           "\n"
                           "// Light array (set by engine UploadLights)\n"
                           "#define MAX_LIGHTS 8\n"
                           "\n"
                           "struct LightData {\n"
                           "    int type;       // 0 Directional, 1 Point, 2 Spot, 3 Rect, 4 Sky\n"
                           "    vec3 position;\n"
                           "    vec3 direction;\n"
                           "    vec3 color;     // pre-multiplied with intensity\n"
                           "    float radius;\n"
                           "};\n"
                           "\n"
                           "uniform int u_lightCount;\n"
                           "uniform LightData u_lights[MAX_LIGHTS];\n"
                           "uniform sampler2DShadow u_shadowMap;\n"
                           "uniform mat4 u_shadowLightSpaceMatrices[4];\n"
                           "uniform float u_shadowCascadeSplits[4];\n"
                           "uniform float u_shadowTexelWorldSizes[4];\n"
                           "uniform int u_shadowCascadeCount = 0;\n"
                           "uniform int u_shadowUsesAtlas = 0;\n"
                           "uniform int u_shadowEnabled = 0;\n"
                           "uniform int u_shadowLightIndex = -1;\n"
                           "uniform samplerCube u_pointShadowMap;\n"
                           "uniform int u_pointShadowEnabled = 0;\n"
                           "uniform int u_pointShadowLightIndex = -1;\n"
                           "uniform float u_pointShadowFarPlane = 1.0;\n"
                           "\n"
                           "vec2 ShadowAtlasOffset(int cascade)\n"
                           "{\n"
                           "    if (u_shadowUsesAtlas == 0)\n"
                           "        return vec2(0.0);\n"
                           "    return vec2(float(cascade % 2), float(cascade / 2)) * 0.5;\n"
                           "}\n"
                           "\n"
                           "float SampleShadowCascade(int cascade, vec3 pos, vec3 N, vec3 lightDir)\n"
                           "{\n"
                           "    float normalOffset = u_shadowTexelWorldSizes[cascade] *\n"
                           "                         mix(0.75, 2.25, 1.0 - max(dot(N, lightDir), 0.0));\n"
                           "    vec4 lightSpace = u_shadowLightSpaceMatrices[cascade] *\n"
                           "                      vec4(pos + N * normalOffset, 1.0);\n"
                           "    vec3 projCoords = lightSpace.xyz / lightSpace.w;\n"
                           "    projCoords = projCoords * 0.5 + 0.5;\n"
                           "    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||\n"
                           "        projCoords.y < 0.0 || projCoords.y > 1.0 ||\n"
                           "        projCoords.z < 0.0 || projCoords.z > 1.0)\n"
                           "        return 1.0;\n"
                           "\n"
                           "    float atlasScale = u_shadowUsesAtlas != 0 ? 0.5 : 1.0;\n"
                           "    vec2 localTexel = 1.0 / (vec2(textureSize(u_shadowMap, 0)) * atlasScale);\n"
                           "    vec2 margin = localTexel * 3.5;\n"
                           "    vec2 atlasOffset = ShadowAtlasOffset(cascade);\n"
                           "    float slopeBias = 1.0 - max(dot(N, lightDir), 0.0);\n"
                           "    float referenceDepth = projCoords.z - max(0.00012, 0.0009 * slopeBias);\n"
                           "    float visibility = 0.0;\n"
                           "    float weightSum = 0.0;\n"
                           "    for (int y = -2; y <= 2; ++y) {\n"
                           "        for (int x = -2; x <= 2; ++x) {\n"
                           "            float weight = float(3 - abs(x)) * float(3 - abs(y));\n"
                           "            vec2 localUv = clamp(\n"
                           "                projCoords.xy + vec2(float(x), float(y)) * localTexel,\n"
                           "                margin, vec2(1.0) - margin);\n"
                           "            vec2 atlasUv = atlasOffset + localUv * atlasScale;\n"
                           "            visibility += texture(u_shadowMap,\n"
                           "                                  vec3(atlasUv, referenceDepth)) * weight;\n"
                           "            weightSum += weight;\n"
                           "        }\n"
                           "    }\n"
                           "    return visibility / weightSum;\n"
                           "}\n"
                           "\n"
                           "float ShadowVisibility(int lightIndex, LightData L, vec3 pos, vec3 N)\n"
                           "{\n"
                           "    if (u_shadowEnabled == 0 || lightIndex != u_shadowLightIndex ||\n"
                           "        u_shadowCascadeCount <= 0)\n"
                           "        return 1.0;\n"
                           "\n"
                           "    vec3 lightDir;\n"
                           "    if (L.type == 0)\n"
                           "        lightDir = normalize(-L.direction);\n"
                           "    else if (L.type == 2)\n"
                           "        lightDir = normalize(L.position - pos);\n"
                           "    else\n"
                           "        return 1.0;\n"
                           "\n"
                           "    if (L.type == 2)\n"
                           "        return SampleShadowCascade(0, pos, N, lightDir);\n"
                           "\n"
                           "    float viewDepth = -(u_view * vec4(pos, 1.0)).z;\n"
                           "    if (viewDepth > u_shadowCascadeSplits[u_shadowCascadeCount - 1])\n"
                           "        return 1.0;\n"
                           "\n"
                           "    int cascade = 0;\n"
                           "    for (int i = 0; i < 3; ++i) {\n"
                           "        if (i + 1 < u_shadowCascadeCount &&\n"
                           "            viewDepth > u_shadowCascadeSplits[i])\n"
                           "            cascade = i + 1;\n"
                           "    }\n"
                           "\n"
                           "    float visibility = SampleShadowCascade(cascade, pos, N, lightDir);\n"
                           "    if (cascade + 1 < u_shadowCascadeCount) {\n"
                           "        float previousSplit =\n"
                           "            cascade == 0 ? 0.0 : u_shadowCascadeSplits[cascade - 1];\n"
                           "        float blendWidth =\n"
                           "            (u_shadowCascadeSplits[cascade] - previousSplit) * 0.12;\n"
                           "        float blend = smoothstep(\n"
                           "            u_shadowCascadeSplits[cascade] - blendWidth,\n"
                           "            u_shadowCascadeSplits[cascade], viewDepth);\n"
                           "        if (blend > 0.0) {\n"
                           "            float nextVisibility =\n"
                           "                SampleShadowCascade(cascade + 1, pos, N, lightDir);\n"
                           "            visibility = mix(visibility, nextVisibility, blend);\n"
                           "        }\n"
                           "    }\n"
                           "    return mix(0.08, 1.0, visibility);\n"
                           "}\n"
                           "\n"
                           "float PointShadowVisibility(int lightIndex, LightData L, vec3 pos, vec3 N)\n"
                           "{\n"
                           "    if (u_pointShadowEnabled == 0 || lightIndex != u_pointShadowLightIndex ||\n"
                           "        L.type != 1)\n"
                           "        return 1.0;\n"
                           "\n"
                           "    vec3 lightDir = normalize(L.position - pos);\n"
                           "    float surfaceAngle = max(dot(N, lightDir), 0.0);\n"
                           "    float unoffsetDistance = length(pos - L.position);\n"
                           "    float texelWorldSize =\n"
                           "        max(unoffsetDistance, 0.001) * 2.0 /\n"
                           "        float(textureSize(u_pointShadowMap, 0).x);\n"
                           "    float normalOffset =\n"
                           "        texelWorldSize * mix(1.5, 4.0, 1.0 - surfaceAngle);\n"
                           "    vec3 receiverPos = pos + N * normalOffset;\n"
                           "    vec3 lightToFrag = receiverPos - L.position;\n"
                           "    float currentDistance = length(lightToFrag);\n"
                           "    if (currentDistance > L.radius)\n"
                           "        return 1.0;\n"
                           "\n"
                           "    float bias =\n"
                           "        max(texelWorldSize * mix(1.25, 3.0, 1.0 - surfaceAngle), 0.0015);\n"
                           "    vec3 axis = abs(lightDir.y) < 0.98 ? vec3(0.0, 1.0, 0.0)\n"
                           "                                      : vec3(1.0, 0.0, 0.0);\n"
                           "    vec3 tangent = normalize(cross(axis, lightDir));\n"
                           "    vec3 bitangent = cross(lightDir, tangent);\n"
                           "    float sourceRadius = clamp(L.radius * 0.004, 0.025, 0.10);\n"
                           "\n"
                           "    float blockerDistance = 0.0;\n"
                           "    int blockerCount = 0;\n"
                           "    for (int i = 0; i < 24; ++i) {\n"
                           "        float sampleIndex = float(i) + 0.5;\n"
                           "        float radius = sqrt(sampleIndex / 24.0);\n"
                           "        float angle = sampleIndex * 2.39996323;\n"
                           "        vec2 disk = vec2(cos(angle), sin(angle)) * radius * sourceRadius;\n"
                           "        vec3 sampleVector =\n"
                           "            lightToFrag + tangent * disk.x + bitangent * disk.y;\n"
                           "        float closestDistance = texture(u_pointShadowMap, sampleVector).r;\n"
                           "        if (length(sampleVector) - bias > closestDistance) {\n"
                           "            blockerDistance += closestDistance;\n"
                           "            blockerCount += 1;\n"
                           "        }\n"
                           "    }\n"
                           "    if (blockerCount == 0)\n"
                           "        return 1.0;\n"
                           "\n"
                           "    blockerDistance /= float(blockerCount);\n"
                           "    float penumbra =\n"
                           "        max(currentDistance - blockerDistance, 0.0) /\n"
                           "        max(blockerDistance, 0.001);\n"
                           "    float diskRadius =\n"
                           "        clamp(texelWorldSize * 1.5 + sourceRadius * penumbra,\n"
                           "              texelWorldSize * 1.5,\n"
                           "              max(sourceRadius * 1.5, texelWorldSize * 2.0));\n"
                           "    float shadow = 0.0;\n"
                           "    int filterSampleCount = penumbra < 0.04 ? 24 : 48;\n"
                           "    for (int i = 0; i < 48; ++i) {\n"
                           "        if (i >= filterSampleCount)\n"
                           "            break;\n"
                           "        float sampleIndex = float(i) + 0.5;\n"
                           "        float radius = sqrt(sampleIndex / float(filterSampleCount));\n"
                           "        float angle = sampleIndex * 2.39996323;\n"
                           "        vec2 disk = vec2(cos(angle), sin(angle)) * radius * diskRadius;\n"
                           "        vec3 sampleVector =\n"
                           "            lightToFrag + tangent * disk.x + bitangent * disk.y;\n"
                           "        float closestDistance = texture(u_pointShadowMap, sampleVector).r;\n"
                           "        shadow += length(sampleVector) - bias > closestDistance ? 1.0 : 0.0;\n"
                           "    }\n"
                           "    shadow /= float(filterSampleCount);\n"
                           "    return 1.0 - shadow * 0.92;\n"
                           "}\n"
                           "void main() {\n"
                           "    // Resolve base color\n"
                           "    vec4 base = (u_hasTexture == 1) ? texture(u_albedoMap, v_uv) : u_color;\n"
                           "\n"
                           "    vec3 N = normalize(v_normal);\n"
                           "    vec3 V = normalize(u_cameraPos - v_worldPos);\n"
                           "\n"
                           "    vec3 ambient = vec3(0.0);\n"
                           "\n"
                           "    vec3 totalLight = vec3(0.0);\n"
                           "\n"
                           "    for (int i = 0; i < u_lightCount && i < MAX_LIGHTS; ++i) {\n"
                           "        LightData L = u_lights[i];\n"
                           "        vec3 lightColor = L.color;\n"
                           "\n"
                           "        vec3 lightDir;\n"
                           "        float attenuation = 1.0;\n"
                           "\n"
                           "        if (L.type == 4) {\n"
                           "            float sky = 0.35 + 0.65 * max(N.y, 0.0);\n"
                           "            totalLight += lightColor * base.rgb * sky;\n"
                           "            continue;\n"
                           "        } else if (L.type == 0) {\n"
                           "            // Directional light\n"
                           "            lightDir = normalize(-L.direction);\n"
                           "        } else {\n"
                           "            vec3 toLight = L.position - v_worldPos;\n"
                           "            float dist = length(toLight);\n"
                           "            if (dist > L.radius) continue;\n"
                           "            lightDir = toLight / dist;\n"
                           "\n"
                           "            float dOverR = dist / max(L.radius, 0.001);\n"
                           "            float window = max(0.0, 1.0 - dOverR * dOverR * dOverR * dOverR);\n"
                           "            attenuation = (window * window) / (1.0 + dist * dist * 0.08);\n"
                           "            if (L.type == 2) {\n"
                           "                float cone = dot(normalize(L.direction), normalize(v_worldPos - L.position));\n"
                           "                attenuation *= smoothstep(cos(radians(24.0)), cos(radians(14.0)), cone);\n"
                           "            } else if (L.type == 3) {\n"
                           "                float facing = max(dot(normalize(L.direction), normalize(v_worldPos - L.position)), 0.0);\n"
                           "                attenuation *= facing;\n"
                           "            }\n"
                           "        }\n"
                           "\n"
                           "        // Diffuse (Lambert)\n"
                           "        float NdotL = max(dot(N, lightDir), 0.0);\n"
                           "        vec3 diffuse = lightColor * NdotL * base.rgb;\n"
                           "\n"
                           "        // Specular (Blinn-Phong)\n"
                           "        vec3 H = normalize(lightDir + V);\n"
                           "        float shininess = mix(8.0, 256.0, 1.0 - u_roughness);\n"
                           "        float spec = pow(max(dot(N, H), 0.0), shininess) *\n"
                           "                     (1.0 - u_roughness) * (1.0 - u_roughness);\n"
                           "        vec3 specColor = mix(vec3(0.04), base.rgb, u_metallic);\n"
                           "        vec3 specular = lightColor * spec * specColor;\n"
                           "\n"
                           "        float visibility = ShadowVisibility(i, L, v_worldPos, N) *\n"
                           "                           PointShadowVisibility(i, L, v_worldPos, N);\n"
                           "        totalLight += (diffuse + specular) * attenuation * visibility;\n"
                           "    }\n"
                           "\n"
                           "    vec3 color = ambient + totalLight;\n"
                           "    color = color / (color + vec3(1.0));\n"
                           "    color = pow(color, vec3(1.0 / 2.2));\n"
                           "    FragColor = vec4(color, base.a);\n"
                           "}\n"),
                           outError))
            return false;

        try {
            Editor::SceneSerializer::SaveToFile(
                BuildTemplateScene(),
                (projectRoot / "assets" / "scenes" / "level.json").string());
        } catch (const Editor::SceneSerializerException &e) {
            if (outError)
                *outError = e.what();
            return false;
        }

        if (!SaveProjectManifestDocument(projectRoot, &doc, outError))
            return false;

        if (outProjectDocument)
            *outProjectDocument = doc;
        return true;
    }
} // namespace Horo::Launcher
