#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <string_view>
#include <unordered_map>

#include "core/Application.h"
#include "core/EngineLaunchArgs.h"
#include "core/LogBuffer.h"
#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "core/Screenshot.h"
#include "core/StringHash.h"
#include "core/Time.h"
#include "core/Window.h"
#include "math/MathUtils.h"
#include "renderer/Shader.h"
#include "tests/TestTempPaths.h"

using namespace Monolith;
using Catch::Approx;

// ===========================================================================
// Logger — just verifies the API doesn't crash (output goes to stdout/stderr)
// ===========================================================================

TEST_CASE("Logger: LogInfo does not crash", "[logger]") {
  REQUIRE_NOTHROW(LogInfo("Hello from test {}", 42));
}

TEST_CASE("Logger: LogWarn does not crash", "[logger]") {
  REQUIRE_NOTHROW(LogWarn("Warning: value is {}", 3.14f));
}

TEST_CASE("Logger: LogError does not crash", "[logger]") {
  REQUIRE_NOTHROW(LogError("Error code {}", -1));
}

TEST_CASE("Logger: LogInfo with no args does not crash", "[logger]") {
  REQUIRE_NOTHROW(LogInfo("Simple message without format args"));
}

TEST_CASE("Logger: LogInfo with Info level does not crash", "[logger]") {
  REQUIRE_NOTHROW(LogInfo("Direct call {}", "works"));
}

TEST_CASE("Logger: LogWarn with Warn level does not crash", "[logger]") {
  REQUIRE_NOTHROW(LogWarn("Warn direct"));
}

TEST_CASE("Logger: LogError with Error level does not crash", "[logger]") {
  REQUIRE_NOTHROW(LogError("Error direct"));
}

TEST_CASE("Logger: multiple rapid calls do not crash", "[logger]") {
  for (int i = 0; i < 100; ++i) {
    REQUIRE_NOTHROW(LogInfo("Iteration {}", i));
  }
}

TEST_CASE("Logger: long message is handled", "[logger]") {
  // Fill up close to the 1024-byte buffer
  std::string longMsg(800, 'A');
  REQUIRE_NOTHROW(LogInfo("{}", longMsg));
}

TEST_CASE("Logger: empty string message is handled", "[logger]") {
  REQUIRE_NOTHROW(LogInfo("{}", ""));
}

TEST_CASE("Logger: mixed formatted arguments are handled", "[logger]") {
  REQUIRE_NOTHROW(LogWarn("value={} ratio={:.2f} tag={}", 42, 0.5f, "maze"));
}

TEST_CASE("ProjectPath: Init accepts empty path", "[core][projectpath]") {
  ProjectPath::Init(std::filesystem::path{});
  REQUIRE(ProjectPath::Root().empty());
}

TEST_CASE("ProjectPath: explicit root and sdk root resolution",
          "[core][projectpath]") {
  namespace fs = std::filesystem;

  auto normalizePath = [](const fs::path &value) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(value, ec);
    if (ec)
      normalized = fs::absolute(value, ec);
    if (ec)
      normalized = value;
    return normalized.lexically_normal();
  };

  const fs::path tempRoot =
      Monolith::Tests::SecureTempBase() / "horo_projectpath_explicit";
  std::error_code ec;
  fs::remove_all(tempRoot, ec);
  fs::create_directories(tempRoot / "project" / "assets", ec);
  fs::create_directories(tempRoot / "sdk", ec);

  ProjectPath::SetProjectRoot({});
  REQUIRE_FALSE(ProjectPath::HasExplicitProjectRoot());

  ProjectPath::SetProjectRoot(tempRoot / "project" / "." / "subdir" / "..");
  REQUIRE(ProjectPath::HasExplicitProjectRoot());
  REQUIRE(normalizePath(ProjectPath::Root()) ==
          normalizePath(tempRoot / "project"));

  ProjectPath::SetSdkRoot({});
  REQUIRE(ProjectPath::SdkRoot() == ProjectPath::Root());

  ProjectPath::SetSdkRoot(tempRoot / "sdk" / ".");
  REQUIRE(normalizePath(ProjectPath::SdkRoot()) ==
          normalizePath(tempRoot / "sdk"));
  REQUIRE(normalizePath(ProjectPath::Resolve("assets/models/crate.obj")) ==
          normalizePath(tempRoot / "project" / "assets/models/crate.obj"));
  REQUIRE(normalizePath(ProjectPath::ResolveSdk("assets/shaders/base.vert")) ==
          normalizePath(tempRoot / "sdk" / "assets/shaders/base.vert"));

  ProjectPath::SetProjectRoot({});
  REQUIRE_FALSE(ProjectPath::HasExplicitProjectRoot());
}

TEST_CASE("ProjectPath: Init discovers project root upward and seeds sdk root",
          "[core][projectpath]") {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      Monolith::Tests::SecureTempBase() / "horo_projectpath_init";
  std::error_code ec;
  fs::remove_all(tempRoot, ec);
  fs::create_directories(tempRoot / "repo" / "assets", ec);
  fs::create_directories(tempRoot / "repo" / "bin" / "nested", ec);

  {
    std::ofstream presets(tempRoot / "repo" / "CMakePresets.json");
    REQUIRE(presets.is_open());
    presets << "{}";
  }

  ProjectPath::SetProjectRoot({});
  ProjectPath::SetSdkRoot({});
  ProjectPath::Init(tempRoot / "repo" / "bin" / "nested");

  REQUIRE(ProjectPath::Root() == fs::absolute(tempRoot / "repo"));
  REQUIRE(ProjectPath::SdkRoot() == ProjectPath::Root());
}

TEST_CASE("ProjectPath: Init fallback keeps explicit sdk root",
          "[core][projectpath]") {
  namespace fs = std::filesystem;

  auto normalizePath = [](const fs::path &value) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(value, ec);
    if (ec)
      normalized = fs::absolute(value, ec);
    if (ec)
      normalized = value;
    return normalized.lexically_normal();
  };

  const fs::path tempRoot =
      Monolith::Tests::SecureTempBase() / "horo_projectpath_fallback";
  std::error_code ec;
  fs::remove_all(tempRoot, ec);
  fs::create_directories(tempRoot / "bin" / "nested", ec);
  fs::create_directories(tempRoot / "sdk_root", ec);

  ProjectPath::SetProjectRoot({});
  ProjectPath::SetSdkRoot(tempRoot / "sdk_root");
  ProjectPath::Init(tempRoot / "bin" / "nested");

  REQUIRE(normalizePath(ProjectPath::Root()) ==
          normalizePath(tempRoot / "bin" / "nested"));
  REQUIRE(normalizePath(ProjectPath::SdkRoot()) ==
          normalizePath(tempRoot / "sdk_root"));
}

TEST_CASE(
    "ProjectPath: Init fallback seeds sdk root when no project markers exist",
    "[core][projectpath]") {
  namespace fs = std::filesystem;

  auto normalizePath = [](const fs::path &value) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(value, ec);
    if (ec)
      normalized = fs::absolute(value, ec);
    if (ec)
      normalized = value;
    return normalized.lexically_normal();
  };

  const fs::path tempRoot =
      Monolith::Tests::SecureTempBase() / "horo_projectpath_no_markers";
  std::error_code ec;
  fs::remove_all(tempRoot, ec);
  fs::create_directories(tempRoot / "bin" / "nested", ec);

  ProjectPath::SetProjectRoot({});
  ProjectPath::SetSdkRoot({});
  ProjectPath::Init(tempRoot / "bin" / "nested");

  REQUIRE(normalizePath(ProjectPath::Root()) ==
          normalizePath(tempRoot / "bin" / "nested"));
  REQUIRE(ProjectPath::SdkRoot() == ProjectPath::Root());
  REQUIRE(normalizePath(ProjectPath::Resolve("")) ==
          normalizePath(ProjectPath::Root()));
  REQUIRE(normalizePath(ProjectPath::ResolveSdk("")) ==
          normalizePath(ProjectPath::SdkRoot()));
}

TEST_CASE("ProjectPath: root setters fall back to original path on "
          "canonicalization errors",
          "[core][projectpath]") {
  namespace fs = std::filesystem;

  const std::string longName(5000, 'x');
  const fs::path longRelativePath = fs::path(longName) / "child";

  ProjectPath::SetProjectRoot({});
  ProjectPath::SetSdkRoot({});

  ProjectPath::SetProjectRoot(longRelativePath);
  ProjectPath::SetSdkRoot(longRelativePath);

  const std::filesystem::path normalizedRoot =
      ProjectPath::Root().lexically_normal();
  const std::filesystem::path normalizedSdkRoot =
      ProjectPath::SdkRoot().lexically_normal();
  REQUIRE(normalizedRoot.filename() == "child");
  REQUIRE(normalizedSdkRoot.filename() == "child");
  REQUIRE(normalizedRoot.string().find(longName) != std::string::npos);
  REQUIRE(normalizedSdkRoot.string().find(longName) != std::string::npos);
}

#ifndef _WIN32
TEST_CASE("ProjectPath: root setters tolerate unavailable current directory",
          "[core][projectpath]") {
  namespace fs = std::filesystem;

  const fs::path savedCwd = fs::current_path();
  const fs::path tempRoot =
      Monolith::Tests::SecureTempBase() / "horo_projectpath_deleted_cwd";
  std::error_code ec;
  fs::remove_all(tempRoot, ec);
  fs::create_directories(tempRoot, ec);
  REQUIRE_FALSE(ec);

  fs::current_path(tempRoot, ec);
  REQUIRE_FALSE(ec);

  fs::remove_all(tempRoot, ec);
  if (ec) {
    fs::current_path(savedCwd, ec);
    REQUIRE_FALSE(ec);
    SUCCEED("Current directory could not be removed on this runner");
    return;
  }

  ProjectPath::SetProjectRoot("relative/project/root");
  ProjectPath::SetSdkRoot("relative/sdk/root");

  fs::current_path(savedCwd, ec);
  REQUIRE_FALSE(ec);

  REQUIRE(ProjectPath::Root() == fs::path("relative/project/root"));
  REQUIRE(ProjectPath::SdkRoot() == fs::path("relative/sdk/root"));
}
#endif

TEST_CASE(
    "WindowGraphicsApiTraits: OpenGL keeps window-owned presentation behavior",
    "[core][window]") {
  const WindowGraphicsApiTraits traits =
      GetWindowGraphicsApiTraits(WindowGraphicsApi::OpenGL);

  REQUIRE(traits.createsClientContext);
  REQUIRE(traits.windowOwnsPresentation);
  REQUIRE(traits.windowOwnsVSync);
  REQUIRE(traits.windowOwnsViewportResize);
  REQUIRE(traits.requestsMsaaSamples);
}

TEST_CASE("WindowGraphicsApiTraits: Vulkan leaves presentation and resize to "
          "the backend",
          "[core][window]") {
  const WindowGraphicsApiTraits traits =
      GetWindowGraphicsApiTraits(WindowGraphicsApi::Vulkan);

  REQUIRE_FALSE(traits.createsClientContext);
  REQUIRE_FALSE(traits.windowOwnsPresentation);
  REQUIRE_FALSE(traits.windowOwnsVSync);
  REQUIRE_FALSE(traits.windowOwnsViewportResize);
  REQUIRE_FALSE(traits.requestsMsaaSamples);
}

TEST_CASE("AppSpec: aggregate initialization keeps scene path compatibility",
          "[core][application]") {
  const AppSpec spec{"Compat App", 1280, 720, true,
                     "assets/scenes/starter_world.json"};

  REQUIRE(spec.name == "Compat App");
  REQUIRE(spec.defaultSceneFile == "assets/scenes/starter_world.json");
  REQUIRE(spec.graphicsApi == WindowGraphicsApi::OpenGL);
}

TEST_CASE("Shader: FromFiles throws ShaderException for missing files",
          "[core][shader]") {
  REQUIRE_THROWS_AS(Shader::FromFiles("/no/such/vertex_shader.vert",
                                      "/no/such/fragment_shader.frag"),
                    ShaderException);
}

TEST_CASE("Shader: FromFiles throws when second shader file is missing",
          "[core][shader]") {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      Monolith::Tests::SecureTempBase() / "horo_shader_io";
  std::error_code ec;
  fs::remove_all(tempRoot, ec);
  fs::create_directories(tempRoot, ec);

  const fs::path vertexPath = tempRoot / "test.vert";
  {
    std::ofstream vertex(vertexPath);
    REQUIRE(vertex.is_open());
    vertex << "#version 410 core\nvoid main(){ gl_Position = vec4(0.0); }\n";
  }

  REQUIRE_THROWS_AS(Shader::FromFiles(vertexPath.string(),
                                      (tempRoot / "missing.frag").string()),
                    ShaderException);
}

TEST_CASE("Shader: FromFiles throws when first shader file is missing",
          "[core][shader]") {
  namespace fs = std::filesystem;

  const fs::path tempRoot =
      Monolith::Tests::SecureTempBase() / "horo_shader_io_first_missing";
  std::error_code ec;
  fs::remove_all(tempRoot, ec);
  fs::create_directories(tempRoot, ec);

  const fs::path fragmentPath = tempRoot / "test.frag";
  {
    std::ofstream fragment(fragmentPath);
    REQUIRE(fragment.is_open());
    fragment
        << "#version 410 core\nout vec4 c; void main(){ c = vec4(1.0); }\n";
  }

  REQUIRE_THROWS_AS(Shader::FromFiles((tempRoot / "missing.vert").string(),
                                      fragmentPath.string()),
                    ShaderException);
}

TEST_CASE(
    "EngineLaunchArgs: keeps previous project path when --project has no value",
    "[core][cli]") {
  std::string arg0 = "horo";
  std::string projectInline = "--project=./Playable";
  std::string projectFlag = "--project";
  std::array<char *, 4> argv = {arg0.data(), projectInline.data(),
                                projectFlag.data(), nullptr};

  const EngineLaunchOptions options =
      ParseEngineLaunchOptions(static_cast<int>(argv.size()), argv.data());
  REQUIRE(options.projectPath == std::filesystem::path("./Playable"));
}

TEST_CASE("EngineLaunchArgs: supports empty inline project value",
          "[core][cli]") {
  std::string arg0 = "horo";
  std::string projectInline = "--project=";
  std::array<char *, 2> argv = {arg0.data(), projectInline.data()};

  const EngineLaunchOptions options =
      ParseEngineLaunchOptions(static_cast<int>(argv.size()), argv.data());
  REQUIRE(options.projectPath.empty());
}

TEST_CASE("EngineLaunchArgs: ParseEditorStartupCli delegates to launch option "
          "parsing",
          "[core][cli]") {
  std::string arg0 = "horo";
  std::string editorArg = "--editor";
  std::string playArg = "--play";
  std::array<char *, 4> argv = {arg0.data(), nullptr, editorArg.data(),
                                playArg.data()};

  REQUIRE(ParseEditorStartupCli(static_cast<int>(argv.size()), argv.data()) ==
          EditorStartupCli::ForcePlay);
}

TEST_CASE("EngineLaunchArgs: --project without usable value keeps defaults",
          "[core][cli]") {
  std::string arg0 = "horo";
  std::string projectFlag = "--project";
  std::string unknown = "--ignored";
  std::array<char *, 4> argv = {arg0.data(), projectFlag.data(), nullptr,
                                unknown.data()};

  const EngineLaunchOptions options =
      ParseEngineLaunchOptions(static_cast<int>(argv.size()), argv.data());
  REQUIRE(options.projectPath.empty());
  REQUIRE(options.editorStartup == EditorStartupCli::Default);
}

TEST_CASE("EngineLaunchArgs: trailing --project token is ignored",
          "[core][cli]") {
  std::string arg0 = "horo";
  std::string projectFlag = "--project";
  std::array<char *, 2> argv = {arg0.data(), projectFlag.data()};

  const EngineLaunchOptions options =
      ParseEngineLaunchOptions(static_cast<int>(argv.size()), argv.data());
  REQUIRE(options.projectPath.empty());
  REQUIRE(options.editorStartup == EditorStartupCli::Default);
}

TEST_CASE("EngineLaunchArgs: null argv slot after --project does not consume "
          "the next flag",
          "[core][cli]") {
  std::string arg0 = "horo";
  std::string projectFlag = "--project";
  std::string editorFlag = "--editor";
  std::array<char *, 4> argv = {arg0.data(), projectFlag.data(), nullptr,
                                editorFlag.data()};

  const EngineLaunchOptions options =
      ParseEngineLaunchOptions(static_cast<int>(argv.size()), argv.data());
  REQUIRE(options.projectPath.empty());
  REQUIRE(options.editorStartup == EditorStartupCli::ForceEditor);
}

// ===========================================================================
// MathUtils — verify constants and inline functions
// ===========================================================================

TEST_CASE("MathUtils: PI value is correct", "[mathutils]") {
  REQUIRE(PI == Approx(3.14159265358979f).epsilon(1e-6f));
}

TEST_CASE("MathUtils: TWO_PI == 2*PI", "[mathutils]") {
  REQUIRE(TWO_PI == Approx(2.0f * PI).epsilon(1e-6f));
}

TEST_CASE("MathUtils: DEG2RAD * 180 == PI", "[mathutils]") {
  REQUIRE(180.0f * DEG2RAD == Approx(PI).epsilon(1e-5f));
}

TEST_CASE("MathUtils: RAD2DEG * PI == 180", "[mathutils]") {
  REQUIRE(PI * RAD2DEG == Approx(180.0f).epsilon(1e-4f));
}

TEST_CASE("MathUtils: ToRadians(180) == PI", "[mathutils]") {
  REQUIRE(ToRadians(180.0f) == Approx(PI).epsilon(1e-5f));
}

TEST_CASE("MathUtils: ToDegrees(PI) == 180", "[mathutils]") {
  REQUIRE(ToDegrees(PI) == Approx(180.0f).epsilon(1e-4f));
}

TEST_CASE("MathUtils: Clamp in range", "[mathutils]") {
  REQUIRE(Clamp(0.5f, 0.0f, 1.0f) == Approx(0.5f));
}

TEST_CASE("MathUtils: Clamp below range", "[mathutils]") {
  REQUIRE(Clamp(-1.0f, 0.0f, 1.0f) == Approx(0.0f));
}

TEST_CASE("MathUtils: Clamp above range", "[mathutils]") {
  REQUIRE(Clamp(2.0f, 0.0f, 1.0f) == Approx(1.0f));
}

TEST_CASE("MathUtils: Clamp01", "[mathutils]") {
  REQUIRE(Clamp01(-0.5f) == Approx(0.0f));
  REQUIRE(Clamp01(0.5f) == Approx(0.5f));
  REQUIRE(Clamp01(1.5f) == Approx(1.0f));
}

TEST_CASE("MathUtils: Lerp at t=0 returns a", "[mathutils]") {
  REQUIRE(Lerp(3.0f, 7.0f, 0.0f) == Approx(3.0f));
}

TEST_CASE("MathUtils: Lerp at t=1 returns b", "[mathutils]") {
  REQUIRE(Lerp(3.0f, 7.0f, 1.0f) == Approx(7.0f));
}

TEST_CASE("MathUtils: Lerp at t=0.5 returns midpoint", "[mathutils]") {
  REQUIRE(Lerp(0.0f, 10.0f, 0.5f) == Approx(5.0f));
}

TEST_CASE("MathUtils: Abs positive", "[mathutils]") {
  REQUIRE(Abs(3.0f) == Approx(3.0f));
}

TEST_CASE("MathUtils: Abs negative", "[mathutils]") {
  REQUIRE(Abs(-3.0f) == Approx(3.0f));
}

TEST_CASE("MathUtils: Sqrt", "[mathutils]") {
  REQUIRE(Sqrt(4.0f) == Approx(2.0f));
  REQUIRE(Sqrt(9.0f) == Approx(3.0f));
  REQUIRE(Sqrt(0.0f) == Approx(0.0f));
}

TEST_CASE("MathUtils: Sin and Cos", "[mathutils]") {
  REQUIRE(Sin(0.0f) == Approx(0.0f).margin(1e-6f));
  REQUIRE(Cos(0.0f) == Approx(1.0f).epsilon(1e-6f));
  REQUIRE(Sin(PI * 0.5f) == Approx(1.0f).epsilon(1e-6f));
  REQUIRE(Cos(PI) == Approx(-1.0f).epsilon(1e-5f));
}

TEST_CASE("MathUtils: Tan(PI/4) == 1", "[mathutils]") {
  REQUIRE(Tan(PI / 4.0f) == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("MathUtils: Acos boundaries", "[mathutils]") {
  REQUIRE(Acos(1.0f) == Approx(0.0f).margin(1e-6f));
  REQUIRE(Acos(-1.0f) == Approx(PI).epsilon(1e-5f));
  // Clamped: out-of-range inputs should not produce NaN
  REQUIRE(Acos(2.0f) == Approx(0.0f).margin(1e-6f)); // clamped to 1
  REQUIRE(Acos(-2.0f) == Approx(PI).epsilon(1e-5f)); // clamped to -1
}

TEST_CASE("MathUtils: Atan2 basic quadrants", "[mathutils]") {
  REQUIRE(Atan2(0.0f, 1.0f) == Approx(0.0f).margin(1e-6f));
  REQUIRE(Atan2(1.0f, 0.0f) == Approx(PI * 0.5f).epsilon(1e-5f));
}

TEST_CASE("MathUtils: Min and Max", "[mathutils]") {
  REQUIRE(Min(3.0f, 7.0f) == Approx(3.0f));
  REQUIRE(Max(3.0f, 7.0f) == Approx(7.0f));
  REQUIRE(Min(-1.0f, -5.0f) == Approx(-5.0f));
}

TEST_CASE("MathUtils: NearlyEqual", "[mathutils]") {
  REQUIRE(NearlyEqual(1.0f, 1.0f));
  REQUIRE(NearlyEqual(1.0f, 1.0f + EPSILON * 0.5f));
  REQUIRE_FALSE(NearlyEqual(1.0f, 2.0f));
}

TEST_CASE("MathUtils: NearlyZero", "[mathutils]") {
  REQUIRE(NearlyZero(0.0f));
  REQUIRE(NearlyZero(EPSILON * 0.5f));
  REQUIRE_FALSE(NearlyZero(1.0f));
}

TEST_CASE("MathUtils: Floor and Ceil", "[mathutils]") {
  REQUIRE(Floor(2.9f) == Approx(2.0f));
  REQUIRE(Ceil(2.1f) == Approx(3.0f));
  REQUIRE(Floor(-0.5f) == Approx(-1.0f));
  REQUIRE(Ceil(-0.5f) == Approx(0.0f));
}

TEST_CASE("MathUtils: Round", "[mathutils]") {
  REQUIRE(Round(2.4f) == Approx(2.0f));
  REQUIRE(Round(2.6f) == Approx(3.0f));
}

TEST_CASE("MathUtils: Pow", "[mathutils]") {
  REQUIRE(Pow(2.0f, 10.0f) == Approx(1024.0f));
  REQUIRE(Pow(3.0f, 2.0f) == Approx(9.0f));
  REQUIRE(Pow(5.0f, 0.0f) == Approx(1.0f));
}

// ===========================================================================
// LogBuffer — null-pointer guard paths and getter coverage
// ===========================================================================

TEST_CASE("LogBuffer: CopyLinesTo nullptr is a no-op", "[logbuffer]") {
  auto &buf = Monolith::LogBuffer::Instance();
  buf.Clear();
  REQUIRE_NOTHROW(buf.CopyLinesTo(nullptr));
}

TEST_CASE("LogBuffer: GetCounts with nullptr args does not crash",
          "[logbuffer]") {
  auto &buf = Monolith::LogBuffer::Instance();
  buf.Clear();
  REQUIRE_NOTHROW(buf.GetCounts(nullptr, nullptr, nullptr));
  int i = 0;
  int w = 0;
  int e = 0;
  REQUIRE_NOTHROW(buf.GetCounts(&i, nullptr, nullptr));
  REQUIRE_NOTHROW(buf.GetCounts(nullptr, &w, nullptr));
  REQUIRE_NOTHROW(buf.GetCounts(nullptr, nullptr, &e));
}

TEST_CASE("LogBuffer: MaxLines getter returns configured value",
          "[logbuffer]") {
  auto &buf = Monolith::LogBuffer::Instance();
  const size_t original = buf.MaxLines();
  buf.SetMaxLines(256);
  REQUIRE(buf.MaxLines() == 256);
  buf.SetMaxLines(original);
}

TEST_CASE("LogBuffer: Push with null file pointer does not crash",
          "[logbuffer]") {
  auto &buf = Monolith::LogBuffer::Instance();
  REQUIRE_NOTHROW(
      buf.Push(Monolith::LogLevel::Info, nullptr, 0, "null-file test"));
}

TEST_CASE("LogBuffer: Revision increments after Push and Clear",
          "[logbuffer]") {
  auto &buf = Monolith::LogBuffer::Instance();
  buf.Clear();
  const uint64_t rev0 = buf.Revision();
  const auto loc = std::source_location::current();
  buf.Push(Monolith::LogLevel::Warn, loc.file_name(),
           static_cast<int>(loc.line()), "rev test");
  REQUIRE(buf.Revision() > rev0);
  const uint64_t rev1 = buf.Revision();
  buf.Clear();
  REQUIRE(buf.Revision() > rev1);
}

// ===========================================================================
// Screenshot — safe early-return branch (invalid viewport dims)
// ===========================================================================

TEST_CASE("Screenshot: zero width returns empty string", "[screenshot]") {
  const std::string outDir = Monolith::Tests::SecureTempBase().string();
  REQUIRE(Monolith::Screenshot::Save(0, 100, outDir).empty());
}

TEST_CASE("Screenshot: zero height returns empty string", "[screenshot]") {
  const std::string outDir = Monolith::Tests::SecureTempBase().string();
  REQUIRE(Monolith::Screenshot::Save(100, 0, outDir).empty());
}

TEST_CASE("Screenshot: negative dimensions return empty string",
          "[screenshot]") {
  const std::string outDir = Monolith::Tests::SecureTempBase().string();
  REQUIRE(Monolith::Screenshot::Save(-1, -1, outDir).empty());
}

// ===========================================================================
// StringHash — heterogeneous lookup coverage
// ===========================================================================

TEST_CASE("StringHash: operator() hashes string_view consistently",
          "[stringhash]") {
  Monolith::StringHash hasher;
  REQUIRE(hasher(std::string_view("hello")) ==
          hasher(std::string_view("hello")));
  REQUIRE(hasher(std::string_view("hello")) !=
          hasher(std::string_view("world")));
}

TEST_CASE("StringHash: heterogeneous find with string_view key",
          "[stringhash]") {
  std::unordered_map<std::string, int, Monolith::StringHash, std::equal_to<>> m;
  m["alpha"] = 1;
  m["beta"] = 2;
  REQUIRE(m.find(std::string_view("alpha")) != m.end());
  REQUIRE(m.find(std::string_view("gamma")) == m.end());
  REQUIRE(m.count(std::string_view("beta")) == 1);
}

// ===========================================================================
// Time — FIXED_DT constant and ConsumeFixedStep false branch
// ===========================================================================

TEST_CASE("Time: FIXED_DT is 120 Hz", "[time]") {
  REQUIRE(Monolith::Time::FIXED_DT == Approx(1.0f / 120.0f));
}

TEST_CASE("Time: ConsumeFixedStep returns false when accumulator is zero",
          "[time]") {
  // After default construction the static accumulator is zero — no steps ready.
  REQUIRE_FALSE(Monolith::Time::ConsumeFixedStep());
}
