#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/Application.h"
#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "core/Window.h"
#include "math/MathUtils.h"
#include "renderer/Shader.h"

using namespace Monolith;
using Catch::Approx;

// ===========================================================================
// Logger — just verifies the API doesn't crash (output goes to stdout/stderr)
// ===========================================================================

TEST_CASE("Logger: LOG_INFO does not crash", "[logger]") {
    REQUIRE_NOTHROW(LOG_INFO("Hello from test %d", 42));
}

TEST_CASE("Logger: LOG_WARN does not crash", "[logger]") {
    REQUIRE_NOTHROW(LOG_WARN("Warning: value is %f", 3.14f));
}

TEST_CASE("Logger: LOG_ERROR does not crash", "[logger]") {
    REQUIRE_NOTHROW(LOG_ERROR("Error code %d", -1));
}

TEST_CASE("Logger: LOG_INFO with no args does not crash", "[logger]") {
    REQUIRE_NOTHROW(LOG_INFO("Simple message without format args"));
}

TEST_CASE("Logger: Log() with Info level does not crash", "[logger]") {
    REQUIRE_NOTHROW(Log(LogLevel::Info, __FILE__, __LINE__, "Direct call %s", "works"));
}

TEST_CASE("Logger: Log() with Warn level does not crash", "[logger]") {
    REQUIRE_NOTHROW(Log(LogLevel::Warn, __FILE__, __LINE__, "Warn direct"));
}

TEST_CASE("Logger: Log() with Error level does not crash", "[logger]") {
    REQUIRE_NOTHROW(Log(LogLevel::Error, __FILE__, __LINE__, "Error direct"));
}

TEST_CASE("Logger: multiple rapid calls do not crash", "[logger]") {
    for (int i = 0; i < 100; ++i) {
        REQUIRE_NOTHROW(LOG_INFO("Iteration %d", i));
    }
}

TEST_CASE("Logger: long message is handled", "[logger]") {
    // Fill up close to the 1024-byte buffer
    std::string longMsg(800, 'A');
    REQUIRE_NOTHROW(LOG_INFO("%s", longMsg.c_str()));
}

TEST_CASE("Logger: empty string message is handled", "[logger]") {
    REQUIRE_NOTHROW(LOG_INFO("%s", ""));
}

TEST_CASE("Logger: mixed formatted arguments are handled", "[logger]") {
    REQUIRE_NOTHROW(LOG_WARN("value=%d ratio=%.2f tag=%s", 42, 0.5f, "maze"));
}

TEST_CASE("ProjectPath: Init accepts empty path", "[core][projectpath]") {
    ProjectPath::Init(std::filesystem::path{});
    REQUIRE(ProjectPath::Root().empty());
}

TEST_CASE("ProjectPath: explicit root and sdk root resolution", "[core][projectpath]") {
    namespace fs = std::filesystem;

    auto normalizePath = [](const fs::path& value) {
        std::error_code ec;
        fs::path normalized = fs::weakly_canonical(value, ec);
        if (ec)
            normalized = fs::absolute(value, ec);
        if (ec)
            normalized = value;
        return normalized.lexically_normal();
    };

    const fs::path tempRoot = fs::temp_directory_path() / "horo_projectpath_explicit";
    std::error_code ec;
    fs::remove_all(tempRoot, ec);
    fs::create_directories(tempRoot / "project" / "assets", ec);
    fs::create_directories(tempRoot / "sdk", ec);

    ProjectPath::SetProjectRoot({});
    REQUIRE_FALSE(ProjectPath::HasExplicitProjectRoot());

    ProjectPath::SetProjectRoot(tempRoot / "project" / "." / "subdir" / "..");
    REQUIRE(ProjectPath::HasExplicitProjectRoot());
    REQUIRE(normalizePath(ProjectPath::Root()) == normalizePath(tempRoot / "project"));

    ProjectPath::SetSdkRoot({});
    REQUIRE(ProjectPath::SdkRoot() == ProjectPath::Root());

    ProjectPath::SetSdkRoot(tempRoot / "sdk" / ".");
    REQUIRE(normalizePath(ProjectPath::SdkRoot()) == normalizePath(tempRoot / "sdk"));
    REQUIRE(normalizePath(ProjectPath::Resolve("assets/models/crate.obj")) ==
            normalizePath(tempRoot / "project" / "assets/models/crate.obj"));
    REQUIRE(normalizePath(ProjectPath::ResolveSdk("assets/shaders/base.vert")) ==
            normalizePath(tempRoot / "sdk" / "assets/shaders/base.vert"));

    ProjectPath::SetProjectRoot({});
    REQUIRE_FALSE(ProjectPath::HasExplicitProjectRoot());
}

TEST_CASE("ProjectPath: Init discovers project root upward and seeds sdk root", "[core][projectpath]") {
    namespace fs = std::filesystem;

    const fs::path tempRoot = fs::temp_directory_path() / "horo_projectpath_init";
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

TEST_CASE("WindowGraphicsApiTraits: OpenGL keeps window-owned presentation behavior",
          "[core][window]") {
    const WindowGraphicsApiTraits traits = GetWindowGraphicsApiTraits(WindowGraphicsApi::OpenGL);

    REQUIRE(traits.createsClientContext);
    REQUIRE(traits.windowOwnsPresentation);
    REQUIRE(traits.windowOwnsVSync);
    REQUIRE(traits.windowOwnsViewportResize);
    REQUIRE(traits.requestsMsaaSamples);
}

TEST_CASE("WindowGraphicsApiTraits: Vulkan leaves presentation and resize to the backend",
          "[core][window]") {
    const WindowGraphicsApiTraits traits = GetWindowGraphicsApiTraits(WindowGraphicsApi::Vulkan);

    REQUIRE_FALSE(traits.createsClientContext);
    REQUIRE_FALSE(traits.windowOwnsPresentation);
    REQUIRE_FALSE(traits.windowOwnsVSync);
    REQUIRE_FALSE(traits.windowOwnsViewportResize);
    REQUIRE_FALSE(traits.requestsMsaaSamples);
}

TEST_CASE("AppSpec: aggregate initialization keeps scene path compatibility", "[core][application]") {
    const AppSpec spec{"Compat App", 1280, 720, true, "assets/scenes/starter_world.json"};

    REQUIRE(spec.name == "Compat App");
    REQUIRE(spec.defaultSceneFile == "assets/scenes/starter_world.json");
    REQUIRE(spec.graphicsApi == WindowGraphicsApi::OpenGL);
}

TEST_CASE("Shader: FromFiles throws ShaderException for missing files", "[core][shader]") {
    REQUIRE_THROWS_AS(Shader::FromFiles("/no/such/vertex_shader.vert", "/no/such/fragment_shader.frag"),
                      ShaderException);
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
    REQUIRE(Clamp01(0.5f)  == Approx(0.5f));
    REQUIRE(Clamp01(1.5f)  == Approx(1.0f));
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
    REQUIRE(Sqrt(4.0f)  == Approx(2.0f));
    REQUIRE(Sqrt(9.0f)  == Approx(3.0f));
    REQUIRE(Sqrt(0.0f)  == Approx(0.0f));
}

TEST_CASE("MathUtils: Sin and Cos", "[mathutils]") {
    REQUIRE(Sin(0.0f)  == Approx(0.0f).margin(1e-6f));
    REQUIRE(Cos(0.0f)  == Approx(1.0f).epsilon(1e-6f));
    REQUIRE(Sin(PI * 0.5f) == Approx(1.0f).epsilon(1e-6f));
    REQUIRE(Cos(PI) == Approx(-1.0f).epsilon(1e-5f));
}

TEST_CASE("MathUtils: Tan(PI/4) == 1", "[mathutils]") {
    REQUIRE(Tan(PI / 4.0f) == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("MathUtils: Acos boundaries", "[mathutils]") {
    REQUIRE(Acos(1.0f)  == Approx(0.0f).margin(1e-6f));
    REQUIRE(Acos(-1.0f) == Approx(PI).epsilon(1e-5f));
    // Clamped: out-of-range inputs should not produce NaN
    REQUIRE(Acos(2.0f)  == Approx(0.0f).margin(1e-6f));  // clamped to 1
    REQUIRE(Acos(-2.0f) == Approx(PI).epsilon(1e-5f));   // clamped to -1
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
    REQUIRE(Ceil(2.1f)  == Approx(3.0f));
    REQUIRE(Floor(-0.5f) == Approx(-1.0f));
    REQUIRE(Ceil(-0.5f)  == Approx(0.0f));
}

TEST_CASE("MathUtils: Round", "[mathutils]") {
    REQUIRE(Round(2.4f) == Approx(2.0f));
    REQUIRE(Round(2.6f) == Approx(3.0f));
}

TEST_CASE("MathUtils: Pow", "[mathutils]") {
    REQUIRE(Pow(2.0f, 10.0f) == Approx(1024.0f));
    REQUIRE(Pow(3.0f, 2.0f)  == Approx(9.0f));
    REQUIRE(Pow(5.0f, 0.0f)  == Approx(1.0f));
}
