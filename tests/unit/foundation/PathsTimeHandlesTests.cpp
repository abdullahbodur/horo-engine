#include "Horo/Foundation/Handles.h"
#include "Horo/Foundation/Paths.h"
#include "Horo/Foundation/Time.h"

#include <cassert>
#include <chrono>

namespace
{

struct TextureTag;
struct MeshTag;

void ProjectPathNormalizesPortableSeparators()
{
    const auto result = Horo::ProjectPath::Parse("Assets\\Textures/../UI/icon.png");
    assert(result.HasValue());
    assert(result.Value().String() == "Assets/UI/icon.png");
}

void ProjectPathRejectsRootEscape()
{
    const auto result = Horo::ProjectPath::Parse("../../outside.txt");
    assert(result.HasError());
}

void TypedHandlesRejectDifferentResourceTags()
{
    const Horo::Handle<TextureTag> texture{.index = 4, .generation = 9};
    const Horo::Handle<MeshTag> mesh{.index = 4, .generation = 9};
    assert(texture.IsValid());
    assert(mesh.IsValid());
    static_assert(!std::is_same_v<decltype(texture), decltype(mesh)>);
}

void DefaultHandleIsInvalid()
{
    const Horo::Handle<TextureTag> handle{};
    assert(!handle.IsValid());
}

void MonotonicDurationUsesSteadyClockDuration()
{
    const Horo::Duration duration = Horo::Duration::FromMilliseconds(25);
    assert(duration.ToMilliseconds() == 25);
    const Horo::Duration sum = duration + Horo::Duration::FromMilliseconds(5);
    assert(sum.ToMilliseconds() == 30);
}

} // namespace

int main()
{
    ProjectPathNormalizesPortableSeparators();
    ProjectPathRejectsRootEscape();
    TypedHandlesRejectDifferentResourceTags();
    DefaultHandleIsInvalid();
    MonotonicDurationUsesSteadyClockDuration();
    return 0;
}
