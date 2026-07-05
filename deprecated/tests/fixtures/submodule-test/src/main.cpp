/** @file main.cpp
 *  @brief Submodule smoke test — minimal executable that exercises
 *         Horo Engine public headers and linkage when the engine is
 *         consumed via add_subdirectory().
 *
 *  This is NOT a unit test.  It is a build-and-link validation that
 *  runs in CI to prove the submodule consumption path stays healthy.
 */

#include "math/Vec3.h"
#include "scene/Scene.h"

#include <cstdlib>

int main() {
    // ---- Exercise the math layer -----------------------------------
    const Horo::Vec3 v{1.0f, 2.0f, 3.0f};
    const float len = v.Length();

    // Belt-and-suspenders: Length of a non-zero vector must be > 0.
    if (len <= 0.0f)
        return EXIT_FAILURE;

    // Dot-product sanity.
    const Horo::Vec3 u = Horo::Vec3::Up();
    const float dot = Horo::Vec3::Dot(v, u);
    if (dot != 2.0f)
        return EXIT_FAILURE;

    // ---- Exercise the scene layer (construction + teardown) --------
    {
        Horo::Scene scene;
        scene.CreateEntity(Horo::Vec3::Zero());
        scene.Clear();
    }

    return EXIT_SUCCESS;
}
