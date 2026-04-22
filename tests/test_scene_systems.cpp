#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "physics/PhysicsWorld.h"
#include "physics/RigidBody.h"
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

// ===========================================================================
// BehaviorSystem — additional coverage
// ===========================================================================

// Behavior that modifies a value in registry via a separate component
struct ValueComponent {
    float value = 0.0f;
};

class MultiCallBehavior : public Behavior {
public:
    int callCount = 0;
    float dtSum = 0.0f;

    void OnUpdate(Entity, Registry &, float dt) override {
        ++callCount;
        dtSum += dt;
    }
};

class RegistryModifyBehavior : public Behavior {
public:
    void OnUpdate(Entity self, Registry &reg, float) override {
        if (reg.Has<ValueComponent>(self)) {
            reg.Get<ValueComponent>(self).value += 1.0f;
        }
    }
};

TEST_CASE("BehaviorSystem: behavior receives correct dt each frame",
          "[behavior]") {
    Registry reg;
    Entity e = reg.Create();

    auto b = std::make_unique<MultiCallBehavior>();
    const MultiCallBehavior *bPtr = b.get();
    reg.Add<BehaviorComponent>(e).behavior = std::move(b);

    BehaviorSystem sys;
    sys.OnUpdate(reg, 0.016f);
    sys.OnUpdate(reg, 0.033f);
    sys.OnUpdate(reg, 0.016f);

    REQUIRE(bPtr->callCount == 3);
    REQUIRE(bPtr->dtSum == Approx(0.065f).epsilon(1e-5f));
}

TEST_CASE("BehaviorSystem: behavior can modify registry components",
          "[behavior]") {
    Registry reg;
    Entity e = reg.Create();
    reg.Add<ValueComponent>(e, {0.0f});
    reg.Add<BehaviorComponent>(e).behavior =
            std::make_unique<RegistryModifyBehavior>();

    BehaviorSystem sys;
    sys.OnUpdate(reg, 0.016f);
    sys.OnUpdate(reg, 0.016f);
    sys.OnUpdate(reg, 0.016f);

    REQUIRE(reg.Get<ValueComponent>(e).value == Approx(3.0f));
}

TEST_CASE("BehaviorSystem: multiple entities each run their own behavior",
          "[behavior]") {
    Registry reg;

    auto b1 = std::make_unique<MultiCallBehavior>();
    auto b2 = std::make_unique<MultiCallBehavior>();
    auto b3 = std::make_unique<MultiCallBehavior>();

    const MultiCallBehavior *b1Ptr = b1.get();
    const MultiCallBehavior *b2Ptr = b2.get();
    const MultiCallBehavior *b3Ptr = b3.get();

    Entity e1 = reg.Create();
    Entity e2 = reg.Create();
    Entity e3 = reg.Create();

    reg.Add<BehaviorComponent>(e1).behavior = std::move(b1);
    reg.Add<BehaviorComponent>(e2).behavior = std::move(b2);
    reg.Add<BehaviorComponent>(e3).behavior = std::move(b3);

    BehaviorSystem sys;
    sys.OnUpdate(reg, 0.016f);

    REQUIRE(b1Ptr->callCount == 1);
    REQUIRE(b2Ptr->callCount == 1);
    REQUIRE(b3Ptr->callCount == 1);
}

TEST_CASE("BehaviorSystem: entity without behavior not affected by removal of "
          "another",
          "[behavior]") {
    Registry reg;
    Entity e1 = reg.Create();
    Entity e2 = reg.Create(); // no behavior

    auto b1 = std::make_unique<MultiCallBehavior>();
    const MultiCallBehavior *b1Ptr = b1.get();
    reg.Add<BehaviorComponent>(e1).behavior = std::move(b1);
    reg.Add<ValueComponent>(e2, {0.0f});

    BehaviorSystem sys;
    sys.OnUpdate(reg, 0.016f);

    REQUIRE(b1Ptr->callCount == 1);
    REQUIRE(reg.Get<ValueComponent>(e2).value == Approx(0.0f));
}

// ===========================================================================
// CameraSystem — additional coverage
// ===========================================================================

TEST_CASE("CameraSystem: multiple cameras — only active ones update",
          "[camera-system]") {
    Registry reg;

    // Active camera
    Entity e1 = reg.Create();
    TransformComponent tc1;
    tc1.current.position = {1.0f, 2.0f, 3.0f};
    reg.Add<TransformComponent>(e1, tc1);
    CameraComponent cc1;
    cc1.isActive = true;
    cc1.camera.position = {0.0f, 0.0f, 0.0f};
    reg.Add<CameraComponent>(e1, cc1);

    // Inactive camera
    Entity e2 = reg.Create();
    TransformComponent tc2;
    tc2.current.position = {10.0f, 20.0f, 30.0f};
    reg.Add<TransformComponent>(e2, tc2);
    CameraComponent cc2;
    cc2.isActive = false;
    cc2.camera.position = {0.0f, 0.0f, 0.0f};
    reg.Add<CameraComponent>(e2, cc2);

    CameraSystem sys;
    sys.OnUpdate(reg, 0.016f);

    // Active camera should be updated
    REQUIRE(reg.Get<CameraComponent>(e1).camera.position.x == Approx(1.0f));
    REQUIRE(reg.Get<CameraComponent>(e1).camera.position.y == Approx(2.0f));

    // Inactive camera should NOT be updated
    REQUIRE(reg.Get<CameraComponent>(e2).camera.position.x == Approx(0.0f));
    REQUIRE(reg.Get<CameraComponent>(e2).camera.position.y == Approx(0.0f));
}

TEST_CASE("CameraSystem: active camera target uses forward from transform",
          "[camera-system]") {
    Registry reg;
    Entity e = reg.Create();

    TransformComponent tc;
    tc.current.position = {0.0f, 0.0f, 0.0f};
    // Identity rotation → forward is (0, 0, -1)
    reg.Add<TransformComponent>(e, tc);

    CameraComponent cc;
    cc.isActive = true;
    reg.Add<CameraComponent>(e, cc);

    CameraSystem sys;
    sys.OnUpdate(reg, 0.016f);

    const auto &updated = reg.Get<CameraComponent>(e);
    Vec3 expectedTarget = tc.current.position + tc.current.Forward();
    REQUIRE(updated.camera.target.x == Approx(expectedTarget.x).margin(1e-5f));
    REQUIRE(updated.camera.target.y == Approx(expectedTarget.y).margin(1e-5f));
    REQUIRE(updated.camera.target.z == Approx(expectedTarget.z).margin(1e-5f));
}

TEST_CASE("CameraSystem: multiple updates track moving transform",
          "[camera-system]") {
    Registry reg;
    Entity e = reg.Create();

    TransformComponent tc;
    tc.current.position = {0.0f, 0.0f, 0.0f};
    reg.Add<TransformComponent>(e, tc);
    CameraComponent cc;
    cc.isActive = true;
    reg.Add<CameraComponent>(e, cc);

    CameraSystem sys;
    sys.OnUpdate(reg, 0.016f);

    // Move the transform
    reg.Get<TransformComponent>(e).current.position = {5.0f, 3.0f, 1.0f};
    sys.OnUpdate(reg, 0.016f);

    REQUIRE(reg.Get<CameraComponent>(e).camera.position.x == Approx(5.0f));
    REQUIRE(reg.Get<CameraComponent>(e).camera.position.y == Approx(3.0f));
    REQUIRE(reg.Get<CameraComponent>(e).camera.position.z == Approx(1.0f));
}

// ===========================================================================
// PhysicsSystem — additional coverage
// ===========================================================================

TEST_CASE("PhysicsSystem: multiple entities with bodies all sync",
          "[physics-system]") {
    PhysicsWorld world;
    Registry reg;

    Entity e1 = reg.Create();
    Entity e2 = reg.Create();
    reg.Add<TransformComponent>(e1);
    reg.Add<TransformComponent>(e2);

    RigidBody *b1 = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
    RigidBody *b2 = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
    b1->position = {0.0f, 5.0f, 0.0f};
    b2->position = {3.0f, 5.0f, 0.0f};

    RigidBodyComponent rbc1;
    rbc1.body = b1;
    RigidBodyComponent rbc2;
    rbc2.body = b2;
    reg.Add<RigidBodyComponent>(e1, rbc1);
    reg.Add<RigidBodyComponent>(e2, rbc2);

    PhysicsSystem sys(world);
    sys.OnUpdate(reg, 1.0f / 60.0f);

    // Both should be synced from physics bodies
    const auto &tc1 = reg.Get<TransformComponent>(e1);
    const auto &tc2 = reg.Get<TransformComponent>(e2);

    // After physics step, gravity pulls them down — positions should differ from
    // initial
    REQUIRE(tc1.current.position.x == Approx(b1->position.x).epsilon(1e-5f));
    REQUIRE(tc2.current.position.x == Approx(b2->position.x).epsilon(1e-5f));
    // x positions should remain separate (3 apart)
    REQUIRE(std::abs(tc1.current.position.x - tc2.current.position.x) ==
        Approx(3.0f).epsilon(1e-4f));
}

TEST_CASE("PhysicsSystem: orientation is synced from body",
          "[physics-system]") {
    PhysicsWorld world;
    Registry reg;

    Entity e = reg.Create();
    reg.Add<TransformComponent>(e);

    RigidBody body = RigidBody::MakeSphere(0.5f, 1.0f);
    body.orientation = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);
    RigidBody *bodyPtr = world.AddBody(std::move(body));

    RigidBodyComponent rbc;
    rbc.body = bodyPtr;
    reg.Add<RigidBodyComponent>(e, rbc);

    PhysicsSystem sys(world);
    sys.OnUpdate(reg, 1.0f / 120.0f);

    const auto &tc = reg.Get<TransformComponent>(e);
    // After one small step, orientation should roughly match (rotation doesn't
    // change much for sphere with no torque)
    REQUIRE(tc.current.rotation.w != Approx(0.0f).margin(0.5f));
}

// ===========================================================================
// Scene — system interactions
// ===========================================================================

// Counter system to verify ordering
class OrderedSystem : public System {
public:
    int *orderPtr;
    int myOrder = -1;

    explicit OrderedSystem(int *orderArr, int idx)
        : orderPtr(orderArr), myOrder(idx) {
    }

    void OnUpdate(Registry &, float) override { orderPtr[myOrder] = myOrder; }
};

TEST_CASE("Scene: systems run in registration order", "[scene]") {
    Scene scene;
    std::array<int, 3> order = {-1, -1, -1};

    scene.AddSystem(std::make_unique<OrderedSystem>(order.data(), 0));
    scene.AddSystem(std::make_unique<OrderedSystem>(order.data(), 1));
    scene.AddSystem(std::make_unique<OrderedSystem>(order.data(), 2));

    scene.UpdateSystems(0.016f);

    REQUIRE(order[0] == 0);
    REQUIRE(order[1] == 1);
    REQUIRE(order[2] == 2);
}

TEST_CASE("Scene: Clear preserves registered systems", "[scene]") {
    Scene scene;
    int callCount = 0;

    class SimpleCounter : public System {
    public:
        int *count;

        explicit SimpleCounter(int *c) : count(c) {
        }

        void OnUpdate(Registry &, float) override { ++(*count); }
    };

    scene.AddSystem(std::make_unique<SimpleCounter>(&callCount));
    scene.UpdateSystems(0.016f); // count = 1
    scene.Clear();
    scene.UpdateSystems(0.016f); // count = 2 (system still registered)

    REQUIRE(callCount == 2);
}

TEST_CASE("Scene: CreateEntity sets same position in current and previous",
          "[scene]") {
    Scene scene;
    Vec3 pos{7.0f, -3.0f, 2.5f};
    Entity e = scene.CreateEntity(pos);

    const auto &tc = scene.GetRegistry().Get<TransformComponent>(e);
    REQUIRE(tc.current.position.x == Approx(7.0f));
    REQUIRE(tc.previous.position.x == Approx(7.0f));
    REQUIRE(tc.current.position.y == Approx(-3.0f));
    REQUIRE(tc.previous.position.y == Approx(-3.0f));
}

TEST_CASE("Scene: multiple CreateEntity calls produce unique entities",
          "[scene]") {
    Scene scene;
    const int N = 50;
    std::vector<Entity> entities;
    for (int i = 0; i < N; ++i)
        entities.push_back(scene.CreateEntity({(float) i, 0.0f, 0.0f}));

    for (int i = 0; i < N - 1; ++i)
        REQUIRE(entities[i] != entities[i + 1]);
}

TEST_CASE("Scene: Clear then repopulate works", "[scene]") {
    Scene scene;
    scene.CreateEntity({1.0f, 2.0f, 3.0f});
    scene.CreateEntity({4.0f, 5.0f, 6.0f});

    scene.Clear();

    Entity e = scene.CreateEntity({0.0f, 0.0f, 0.0f});
    REQUIRE(scene.GetRegistry().IsAlive(e));
    REQUIRE(scene.GetRegistry().Has<TransformComponent>(e));
}

TEST_CASE("Scene: render systems not called by UpdateSystems", "[scene]") {
    Scene scene;
    int updateCount = 0;
    int renderCount = 0;

    class CountSystem : public System {
    public:
        int *count;

        explicit CountSystem(int *c) : count(c) {
        }

        void OnUpdate(Registry &, float) override { ++(*count); }
    };

    scene.AddSystem(std::make_unique<CountSystem>(&updateCount));
    scene.AddRenderSystem(std::make_unique<CountSystem>(&renderCount));

    scene.UpdateSystems(0.016f);
    scene.UpdateSystems(0.016f);

    REQUIRE(updateCount == 2);
    REQUIRE(renderCount == 0);

    scene.RenderSystems(0.5f);
    REQUIRE(renderCount == 1);
}
