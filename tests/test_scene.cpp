#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "scene/Registry.h"
#include "scene/Scene.h"
#include "scene/System.h"
#include "scene/components/BehaviorComponent.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/RigidBodyComponent.h"
#include "scene/components/TransformComponent.h"
#include "scene/systems/BehaviorSystem.h"
#include "scene/systems/CameraSystem.h"
#include "scene/systems/PhysicsSystem.h"

using namespace Monolith;
using Catch::Approx;

// ============================================================
// Registry::Clear
// ============================================================

TEST_CASE("Registry::Clear removes all entities", "[registry]") {
    Registry reg;
    reg.Create();
    reg.Create();
    reg.Create();

    reg.Clear();

    // After clear, new IDs should restart from 0
    Entity e = reg.Create();
    REQUIRE(e == 0);
}

TEST_CASE("Registry::Clear resets component pools", "[registry]") {
    Registry reg;
    struct Pos {
        float x;
        float y;
        float z;
    };

    Entity e = reg.Create();
    reg.Add<Pos>(e, {1, 2, 3});
    REQUIRE(reg.Has<Pos>(e));

    reg.Clear();

    // After clear the old entity is no longer alive
    REQUIRE_FALSE(reg.IsAlive(e));
}

TEST_CASE("Registry::Clear resets free list", "[registry]") {
    Registry reg;
    Entity a = reg.Create();
    reg.Destroy(a);

    reg.Clear();

    // Create after clear should produce 0 (fresh, not recycled 'a' from before
    // clear)
    Entity b = reg.Create();
    REQUIRE(b == 0);
}

// ============================================================
// Scene — CreateEntity
// ============================================================

TEST_CASE("Scene::CreateEntity returns valid entity with TransformComponent",
          "[scene]") {
    Scene scene;
    Entity e = scene.CreateEntity();
    REQUIRE(e != INVALID_ENTITY);
    REQUIRE(scene.GetRegistry().Has<TransformComponent>(e));
}

TEST_CASE("Scene::CreateEntity sets position on TransformComponent",
          "[scene]") {
    Scene scene;
    Vec3 pos{3, -1, 7};
    Entity e = scene.CreateEntity(pos);

    const auto &tc = scene.GetRegistry().Get<TransformComponent>(e);
    REQUIRE(tc.current.position.x == Approx(3));
    REQUIRE(tc.current.position.y == Approx(-1));
    REQUIRE(tc.current.position.z == Approx(7));
    // previous should also be set to the same position
    REQUIRE(tc.previous.position.x == Approx(3));
}

TEST_CASE("Scene::CreateEntity multiple entities are unique", "[scene]") {
    Scene scene;
    Entity a = scene.CreateEntity({0, 0, 0});
    Entity b = scene.CreateEntity({1, 0, 0});
    Entity c = scene.CreateEntity({2, 0, 0});
    REQUIRE(a != b);
    REQUIRE(b != c);
    REQUIRE(a != c);
}

// ============================================================
// Scene — systems
// ============================================================

// Minimal test system that counts OnUpdate calls
class CounterSystem : public System {
public:
    int count = 0;
    void OnUpdate(Registry &, float) override { ++count; }
};

TEST_CASE("Scene::UpdateSystems calls all registered systems", "[scene]") {
    Scene scene;
    auto cs = std::make_unique<CounterSystem>();
    const CounterSystem *csPtr = cs.get();
    scene.AddSystem(std::move(cs));

    scene.UpdateSystems(1.0f / 60.0f);
    scene.UpdateSystems(1.0f / 60.0f);

    REQUIRE(csPtr->count == 2);
}

TEST_CASE("Scene::RenderSystems calls render systems only", "[scene]") {
    Scene scene;
    auto update = std::make_unique<CounterSystem>();
    auto render = std::make_unique<CounterSystem>();
    const CounterSystem *updatePtr = update.get();
    const CounterSystem *renderPtr = render.get();

    scene.AddSystem(std::move(update));
    scene.AddRenderSystem(std::move(render));

    scene.UpdateSystems(1.0f / 60.0f);
    scene.RenderSystems(0.5f);

    REQUIRE(updatePtr->count == 1);
    REQUIRE(renderPtr->count == 1);
}

TEST_CASE("Scene::UpdateSystems with no systems is safe", "[scene]") {
    Scene scene;
    REQUIRE_NOTHROW(scene.UpdateSystems(1.0f / 60.0f));
}

TEST_CASE("Scene::Clear removes entities and resets physics", "[scene]") {
    Scene scene;
    scene.CreateEntity({0, 5, 0});
    scene.CreateEntity({1, 5, 0});

    scene.Clear();

    // After clear, registry should be empty (new entity gets ID 0)
    Entity e = scene.GetRegistry().Create();
    REQUIRE(e == 0);
    REQUIRE(scene.GetPhysics().GetBodies().empty());
}

// ============================================================
// BehaviorSystem
// ============================================================

class TestBehavior : public Behavior {
public:
    int callCount = 0;
    Entity lastSelf = INVALID_ENTITY;

    void OnUpdate(Entity self, Registry &, float) override {
        ++callCount;
        lastSelf = self;
    }
};

TEST_CASE("BehaviorSystem calls behavior for each entity", "[behavior]") {
    Registry reg;
    Entity e1 = reg.Create();
    Entity e2 = reg.Create();

    auto b1 = std::make_unique<TestBehavior>();
    auto b2 = std::make_unique<TestBehavior>();
    const TestBehavior *b1Ptr = b1.get();
    const TestBehavior *b2Ptr = b2.get();

    reg.Add<BehaviorComponent>(e1).behavior = std::move(b1);
    reg.Add<BehaviorComponent>(e2).behavior = std::move(b2);

    BehaviorSystem sys;
    sys.OnUpdate(reg, 1.0f / 60.0f);

    REQUIRE(b1Ptr->callCount == 1);
    REQUIRE(b1Ptr->lastSelf == e1);
    REQUIRE(b2Ptr->callCount == 1);
    REQUIRE(b2Ptr->lastSelf == e2);
}

TEST_CASE("BehaviorSystem: no BehaviorComponents is safe", "[behavior]") {
    Registry reg;
    reg.Create();
    reg.Create();

    BehaviorSystem sys;
    REQUIRE_NOTHROW(sys.OnUpdate(reg, 1.0f / 60.0f));
}

// ============================================================
// CameraSystem
// ============================================================

TEST_CASE("CameraSystem syncs camera position from active entity",
          "[camera-system]") {
    Registry reg;
    Entity e = reg.Create();

    TransformComponent tc;
    tc.current.position = {5, 2, -3};
    reg.Add<TransformComponent>(e, tc);

    CameraComponent cc;
    cc.isActive = true;
    reg.Add<CameraComponent>(e, cc);

    CameraSystem sys;
    sys.OnUpdate(reg, 1.0f / 60.0f);

    const auto &updated = reg.Get<CameraComponent>(e);
    REQUIRE(updated.camera.position.x == Approx(5));
    REQUIRE(updated.camera.position.y == Approx(2));
    REQUIRE(updated.camera.position.z == Approx(-3));
}

TEST_CASE("CameraSystem: inactive camera is not updated", "[camera-system]") {
    Registry reg;
    Entity e = reg.Create();

    TransformComponent tc;
    tc.current.position = {99, 99, 99};
    reg.Add<TransformComponent>(e, tc);

    CameraComponent cc;
    cc.isActive = false; // inactive
    cc.camera.position = {0, 0, 0};
    reg.Add<CameraComponent>(e, cc);

    CameraSystem sys;
    sys.OnUpdate(reg, 1.0f / 60.0f);

    const auto &updated = reg.Get<CameraComponent>(e);
    // Should not have been updated
    REQUIRE(updated.camera.position.x == Approx(0));
}

TEST_CASE("CameraSystem: camera without transform is safe", "[camera-system]") {
    Registry reg;
    Entity e = reg.Create();

    CameraComponent cc;
    cc.isActive = true;
    reg.Add<CameraComponent>(e, cc);
    // No TransformComponent added

    CameraSystem sys;
    REQUIRE_NOTHROW(sys.OnUpdate(reg, 1.0f / 60.0f));
}

// ============================================================
// PhysicsSystem
// ============================================================

TEST_CASE("PhysicsSystem syncs body position to TransformComponent",
          "[physics-system]") {
    PhysicsWorld world;
    Registry reg;

    Entity e = reg.Create();
    reg.Add<TransformComponent>(e);

    RigidBody *body = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
    body->position = {3, 4, 5};

    RigidBodyComponent rbc;
    rbc.body = body;
    reg.Add<RigidBodyComponent>(e, rbc);

    PhysicsSystem sys(world);
    sys.OnUpdate(reg, 1.0f / 60.0f);

    const auto &tc = reg.Get<TransformComponent>(e);
    // After one step (gravity applied), position should have moved down slightly
    // but the sync should have updated the component
    REQUIRE(tc.current.position.y < 4.0f); // gravity pulled it down in world.Step
}

TEST_CASE("PhysicsSystem: entity without RigidBodyComponent is skipped",
          "[physics-system]") {
    PhysicsWorld world;
    Registry reg;

    Entity e = reg.Create();
    reg.Add<TransformComponent>(e);
    // No RigidBodyComponent

    PhysicsSystem sys(world);
    REQUIRE_NOTHROW(sys.OnUpdate(reg, 1.0f / 60.0f));
}

TEST_CASE("PhysicsSystem: null body pointer is skipped safely",
          "[physics-system]") {
    PhysicsWorld world;
    Registry reg;

    Entity e = reg.Create();
    reg.Add<TransformComponent>(e);

    RigidBodyComponent rbc;
    rbc.body = nullptr; // null body
    reg.Add<RigidBodyComponent>(e, rbc);

    PhysicsSystem sys(world);
    REQUIRE_NOTHROW(sys.OnUpdate(reg, 1.0f / 60.0f));
}

TEST_CASE("PhysicsSystem stores previous transform before sync",
          "[physics-system]") {
    PhysicsWorld world;
    Registry reg;

    Entity e = reg.Create();
    TransformComponent tc;
    tc.current.position = {1, 2, 3};
    tc.previous.position = {0, 0, 0};
    reg.Add<TransformComponent>(e, tc);

    RigidBody *body = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
    body->position = {1, 2, 3};
    RigidBodyComponent rbc;
    rbc.body = body;
    reg.Add<RigidBodyComponent>(e, rbc);

    PhysicsSystem sys(world);
    sys.OnUpdate(reg, 1.0f / 60.0f);

    const auto &updated = reg.Get<TransformComponent>(e);
    // previous should have been set to the old current (1,2,3) before sync
    REQUIRE(updated.previous.position.x == Approx(1));
    REQUIRE(updated.previous.position.y == Approx(2));
    REQUIRE(updated.previous.position.z == Approx(3));
}
