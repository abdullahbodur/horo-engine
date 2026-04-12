#include "launcher/StandaloneProjectTemplate.h"

#include <fstream>

#include "editor/SceneDocument.h"
#include "editor/SceneSerializer.h"

namespace Monolith::Standalone {

namespace fs = std::filesystem;

namespace {

bool WriteTextFile(const fs::path& path, const std::string& content, std::string* outError) {
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

std::string BuildTemplateMainCpp(const std::string& projectName) {
  return
      "#include <memory>\n"
      "#include <stdexcept>\n"
      "#include <string>\n"
      "#include <unordered_map>\n"
      "\n"
      "#include \"core/Application.h\"\n"
      "#include \"editor/SceneSerializer.h\"\n"
      "#include \"math/MathUtils.h\"\n"
      "#include \"renderer/DebugDraw.h\"\n"
      "#include \"renderer/Material.h\"\n"
      "#include \"renderer/ObjLoader.h\"\n"
      "#include \"renderer/RenderViewUtils.h\"\n"
      "#include \"renderer/Renderer.h\"\n"
      "#include \"renderer/Shader.h\"\n"
      "#include \"renderer/Texture.h\"\n"
      "#include \"scene/Scene.h\"\n"
      "#include \"scene/SceneReferenceRuntime.h\"\n"
      "#include \"scene/components/MeshComponent.h\"\n"
      "#include \"scene/systems/BehaviorSystem.h\"\n"
      "#include \"scene/systems/PhysicsSystem.h\"\n"
      "#include \"scene/systems/RenderSystem.h\"\n"
      "\n"
      "namespace {\n"
      "\n"
      "using namespace Monolith;\n"
      "\n"
      "std::shared_ptr<Shader> LoadDefaultShader() {\n"
      "  static std::shared_ptr<Shader> shader;\n"
      "  if (!shader) {\n"
      "    shader = std::make_shared<Shader>(Shader::FromFiles(\"shaders/basic.vert\", \"shaders/basic.frag\"));\n"
      "  }\n"
      "  return shader;\n"
      "}\n"
      "\n"
      "std::shared_ptr<Mesh> LoadMeshForTag(const std::string& meshTag) {\n"
      "  static std::unordered_map<std::string, std::shared_ptr<Mesh>> cache;\n"
      "  if (const auto it = cache.find(meshTag); it != cache.end())\n"
      "    return it->second;\n"
      "\n"
      "  std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();\n"
      "  if (meshTag == \"box\")\n"
      "    *mesh = Mesh::CreateBox();\n"
      "  else if (meshTag == \"sphere\")\n"
      "    *mesh = Mesh::CreateSphere();\n"
      "  else if (meshTag == \"cylinder\")\n"
      "    *mesh = Mesh::CreateCylinder();\n"
      "  else if (meshTag == \"pyramid\")\n"
      "    *mesh = Mesh::CreatePyramid();\n"
      "  else if (meshTag == \"plane\")\n"
      "    *mesh = Mesh::CreatePlane();\n"
      "  else\n"
      "    *mesh = ObjLoader::Load(meshTag);\n"
      "  cache[meshTag] = mesh;\n"
      "  return mesh;\n"
      "}\n"
      "\n"
      "std::shared_ptr<Texture> LoadTextureIfPresent(const std::string& path) {\n"
      "  if (path.empty())\n"
      "    return std::shared_ptr<Texture>();\n"
      "  static std::unordered_map<std::string, std::shared_ptr<Texture>> cache;\n"
      "  if (const auto it = cache.find(path); it != cache.end())\n"
      "    return it->second;\n"
      "  auto texture = std::make_shared<Texture>(Texture::FromFile(path));\n"
      "  cache[path] = texture;\n"
      "  return texture;\n"
      "}\n"
      "\n"
      "class GameApp final : public Application {\n"
      " public:\n"
      "  GameApp() : Application(BuildSpec()) {\n"
      "    m_scene.AddSystem(std::make_unique<BehaviorSystem>());\n"
      "    m_scene.AddSystem(std::make_unique<PhysicsSystem>(m_scene.physics));\n"
      "    m_scene.AddRenderSystem(std::make_unique<RenderSystem>(m_camera, m_renderAlpha));\n"
      "    m_referenceRuntime = std::make_unique<SceneReferenceRuntime>(&m_scene);\n"
      "    m_referenceRuntime->SetPropEntityCreatedCallback(\n"
      "        [this](const RuntimeSceneProp& prop, Entity entity, Scene& sceneRef) {\n"
      "          MeshComponent component;\n"
      "          component.meshTag = prop.meshTag;\n"
      "          component.mesh = LoadMeshForTag(prop.meshTag);\n"
      "          component.material = std::make_shared<Material>();\n"
      "          component.material->shader = LoadDefaultShader();\n"
      "          component.material->albedoMap = LoadTextureIfPresent(prop.albedoMap);\n"
      "          sceneRef.registry.Add<MeshComponent>(entity, std::move(component));\n"
      "        });\n"
      "  }\n"
      "\n"
      " private:\n"
      "  static AppSpec BuildSpec() {\n"
      "    AppSpec spec;\n"
      "    spec.name = \"" + projectName + "\";\n"
      "    spec.width = 1600;\n"
      "    spec.height = 900;\n"
      "    spec.defaultSceneFile = \"assets/scenes/level.json\";\n"
      "    return spec;\n"
      "  }\n"
      "\n"
      "  void OnInit() override {\n"
      "    const RenderBackendInitResult backend = Renderer::InitializeBackend(\n"
      "        {.requested = RenderBackendId::OpenGL, .nativeWindowHandle = GetWindow().GetNativeHandle()});\n"
      "    if (!backend.ok)\n"
      "      throw std::runtime_error(\"Failed to initialize renderer backend: \" + backend.error);\n"
      "    DebugDraw::Init();\n"
      "    if (!GetDefaultSceneFilePath().empty()) {\n"
      "      const auto doc = Editor::SceneSerializer::LoadFromFile(GetDefaultSceneFilePath());\n"
      "      const SceneRuntimeOperationResult result = m_referenceRuntime->LoadDocument(doc);\n"
      "      if (!result.ok)\n"
      "        throw std::runtime_error(\"Failed to load default scene: \" + result.error);\n"
      "      if (m_referenceRuntime->GetSceneCamera().has_value()) {\n"
      "        const RuntimeSceneCamera& camera = *m_referenceRuntime->GetSceneCamera();\n"
      "        m_camera.position = camera.position;\n"
      "        const float yawRad = ToRadians(camera.yaw);\n"
      "        const float pitchRad = ToRadians(camera.pitch);\n"
      "        const Vec3 forward = {\n"
      "            -Sin(yawRad) * Cos(pitchRad),\n"
      "            Sin(pitchRad),\n"
      "            -Cos(yawRad) * Cos(pitchRad),\n"
      "        };\n"
      "        m_camera.target = camera.position + forward;\n"
      "        m_camera.fovY = camera.fovY;\n"
      "        m_camera.zNear = camera.nearClip;\n"
      "        m_camera.zFar = camera.farClip;\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "\n"
      "  void OnUpdate(float dt) override {\n"
      "    (void)dt;\n"
      "  }\n"
      "\n"
      "  void OnFixedUpdate(float dt) override {\n"
      "    m_scene.UpdateSystems(dt);\n"
      "  }\n"
      "\n"
      "  void OnRender(float alpha) override {\n"
      "    m_renderAlpha = alpha;\n"
      "    Renderer::BeginFrame({{}, \"" + projectName + "-frame\"});\n"
      "    Renderer::BeginPass({RenderPassId::OpaqueScene, BuildRenderView(m_camera), \"" + projectName + "-scene\"});\n"
      "    m_scene.RenderSystems(alpha);\n"
      "    DebugDraw::Flush(m_camera);\n"
      "    Renderer::EndPass();\n"
      "    Renderer::EndFrame();\n"
      "  }\n"
      "\n"
      "  void OnShutdown() override {\n"
      "    if (m_referenceRuntime)\n"
      "      m_referenceRuntime->Unload();\n"
      "  }\n"
      "\n"
      "  Scene m_scene;\n"
      "  std::unique_ptr<SceneReferenceRuntime> m_referenceRuntime;\n"
      "  Camera m_camera;\n"
      "  float m_renderAlpha = 0.0f;\n"
      "};\n"
      "\n"
      "}  // namespace\n"
      "\n"
      "int main(int argc, char** argv) {\n"
      "  GameApp app;\n"
      "  app.ParseArgs(argc, argv);\n"
      "  app.Run();\n"
      "  return 0;\n"
      "}\n";
}

std::string BuildTemplateCMakeLists(const std::string& projectName, const std::string& projectId) {
  return
      "cmake_minimum_required(VERSION 3.25)\n"
      "project(" + projectId + " LANGUAGES CXX C)\n"
      "\n"
      "set(CMAKE_CXX_STANDARD 20)\n"
      "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
      "set(CMAKE_CXX_EXTENSIONS OFF)\n"
      "\n"
      "if(EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/engine/CMakeLists.txt\")\n"
      "  add_subdirectory(engine)\n"
      "  set(HORO_ENGINE_TARGET MonolithEngine)\n"
      "else()\n"
      "  find_package(MonolithEngine CONFIG REQUIRED)\n"
      "  set(HORO_ENGINE_TARGET MonolithEngine::MonolithEngine)\n"
      "endif()\n"
      "\n"
      "add_executable(${PROJECT_NAME}\n"
      "  src/main.cpp\n"
      ")\n"
      "set_target_properties(${PROJECT_NAME} PROPERTIES\n"
      "  OUTPUT_NAME \"" + projectName + "\"\n"
      "  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin\n"
      "  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin\n"
      "  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin\n"
      "  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin\n"
      "  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin\n"
      ")\n"
      "target_link_libraries(${PROJECT_NAME} PRIVATE ${HORO_ENGINE_TARGET})\n"
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

Editor::SceneDocument BuildTemplateScene() {
  Editor::SceneDocument doc;
  doc.version = 1;
  doc.sceneId = "level";
  doc.sceneName = "Level";
  doc.filePath = "assets/scenes/level.json";
  doc.settings["spawnPoint"] = "0.0,1.0,6.0";

  Editor::SceneObject panel;
  panel.id = "floor_000";
  panel.type = Editor::SceneObjectType::Panel;
  panel.position = {0.0f, -1.0f, 0.0f};
  panel.scale = {8.0f, 1.0f, 8.0f};
  doc.objects.push_back(panel);

  Editor::SceneObject light;
  light.id = "sun_000";
  light.type = Editor::SceneObjectType::Light;
  light.position = {3.0f, 6.0f, -2.0f};
  light.props["lightType"] = "directional";
  light.props["intensity"] = "1.5";
  light.props["color"] = "1.0,0.95,0.85";
  doc.objects.push_back(light);

  Editor::SceneObject camera;
  camera.id = "cam_000";
  camera.type = Editor::SceneObjectType::Camera;
  camera.position = {0.0f, 2.0f, 8.0f};
  camera.props["fov"] = "60";
  doc.objects.push_back(camera);
  return doc;
}

}  // namespace

bool CreateStandaloneProjectTemplate(const StandaloneProjectTemplateRequest& request,
                                     StandaloneProjectDocument* outProjectDocument,
                                     std::string* outError) {
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
  const std::string exeName =
#ifdef _WIN32
      request.projectName + ".exe";
#else
      request.projectName;
#endif

  StandaloneProjectDocument doc;
  doc.manifest.schemaVersion = 1;
  doc.manifest.projectId = projectId;
  doc.manifest.projectName = request.projectName;
  doc.manifest.defaultScene = "assets/scenes/level.json";
  doc.manifest.configureCommand = {"cmake",
                                   {"-S",
                                    "${projectDir}",
                                    "-B",
                                    "${projectDir}/build",
                                    "-DCMAKE_PREFIX_PATH=${horoSdkRoot}"},
                                   "${projectDir}"};
  doc.manifest.buildCommand = {"cmake",
                               {"--build", "${projectDir}/build", "--config", "Debug"},
                               "${projectDir}"};
  doc.manifest.runCommand = {"${projectDir}/build/bin/" + exeName, {}, "${projectDir}"};

  if (!WriteTextFile(projectRoot / "src" / "main.cpp", BuildTemplateMainCpp(request.projectName), outError))
    return false;
  if (!WriteTextFile(projectRoot / "CMakeLists.txt",
                     BuildTemplateCMakeLists(request.projectName, projectId),
                     outError))
    return false;

  if (!WriteTextFile(projectRoot / "shaders" / "basic.vert",
                     "#version 410 core\n"
                     "layout(location = 0) in vec3 aPos;\n"
                     "layout(location = 1) in vec3 aNormal;\n"
                     "layout(location = 2) in vec2 aUv;\n"
                     "uniform mat4 u_model;\n"
                     "uniform mat4 u_view;\n"
                     "uniform mat4 u_projection;\n"
                     "out vec2 v_uv;\n"
                     "void main() {\n"
                     "  v_uv = aUv;\n"
                     "  gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);\n"
                     "}\n",
                     outError))
    return false;

  if (!WriteTextFile(projectRoot / "shaders" / "basic.frag",
                     "#version 410 core\n"
                     "in vec2 v_uv;\n"
                     "out vec4 FragColor;\n"
                     "uniform vec4 u_color;\n"
                     "uniform sampler2D u_albedoMap;\n"
                     "uniform int u_hasTexture;\n"
                     "void main() {\n"
                     "  vec4 base = (u_hasTexture == 1) ? texture(u_albedoMap, v_uv) : u_color;\n"
                     "  FragColor = base;\n"
                     "}\n",
                     outError))
    return false;

  try {
    Editor::SceneSerializer::SaveToFile(BuildTemplateScene(),
                                        (projectRoot / "assets" / "scenes" / "level.json").string());
  } catch (const std::exception& e) {
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

}  // namespace Monolith::Standalone
