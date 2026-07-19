#include <catch2/catch_test_macros.hpp>

#include "Horo/Foundation/Handles.h"
#include "Horo/Foundation/Paths.h"
#include "Horo/Foundation/Time.h"

#include <chrono>

namespace
{
struct TextureTag;
struct MeshTag;

TEST_CASE("Project Path Normalizes Portable Separators", "[unit][foundation]")
{
    const auto result = Horo::ProjectPath::Parse("Assets\\Textures/../UI/icon.png");
    REQUIRE((result.HasValue()));
    REQUIRE((result.Value().String() == "Assets/UI/icon.png"));
}

TEST_CASE("Project Path Rejects Root Escape", "[unit][foundation]")
{
    const auto result = Horo::ProjectPath::Parse("../../outside.txt");
    REQUIRE((result.HasError()));
}

TEST_CASE("Typed Handles Reject Different Resource Tags", "[unit][foundation]")
{
    constexpr Horo::Handle<TextureTag> texture{.index = 4, .generation = 9};
    constexpr Horo::Handle<MeshTag> mesh{.index = 4, .generation = 9};
    REQUIRE((texture.IsValid()));
    REQUIRE((mesh.IsValid()));
    static_assert(!std::is_same_v<decltype(texture), decltype(mesh)>);
}

TEST_CASE("Default Handle Is Invalid", "[unit][foundation]")
{
    constexpr Horo::Handle<TextureTag> handle{};
    REQUIRE((!handle.IsValid()));
}

TEST_CASE("Monotonic Duration Uses Steady Clock Duration", "[unit][foundation]")
{
    constexpr Horo::Duration duration = Horo::Duration::FromMilliseconds(25);
    REQUIRE((duration.ToMilliseconds() == 25));
    constexpr Horo::Duration sum = duration + Horo::Duration::FromMilliseconds(5);
    REQUIRE((sum.ToMilliseconds() == 30));
    constexpr Horo::Duration precise = Horo::Duration::FromNanoseconds(16'666'667);
    REQUIRE((precise.ToNanoseconds() == 16'666'667));
    REQUIRE(((sum - duration).ToMilliseconds() == 5));
    REQUIRE((precise > Horo::Duration{}));
}
} // namespace
