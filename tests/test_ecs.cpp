#include <catch2/catch_test_macros.hpp>

#include "math/Vec3.h"
#include "scene/ComponentPool.h"
#include "scene/Entity.h"
#include "scene/Registry.h"

using namespace Monolith;

// Simple test components — no dependencies on ECS components
struct Position {
  float x = 0;
  float y = 0;
  float z = 0;
};

struct Velocity {
  float vx = 0;
  float vy = 0;
  float vz = 0;
};

struct Tag {};

// ============================================================
// ComponentPool
// ============================================================

TEST_CASE("ComponentPool starts empty", "[ecs][pool]") {
  ComponentPool<Position> pool;
  REQUIRE(pool.Size() == 0);
  REQUIRE(pool.GetAll().empty());
  REQUIRE(pool.GetEntities().empty());
}

TEST_CASE("ComponentPool Add and Has", "[ecs][pool]") {
  ComponentPool<Position> pool;
  Entity e = 42;

  REQUIRE_FALSE(pool.Has(e));
  pool.Add(e, Position{1, 2, 3});
  REQUIRE(pool.Has(e));
  REQUIRE(pool.Size() == 1);
}

TEST_CASE("ComponentPool Get returns added component", "[ecs][pool]") {
  ComponentPool<Position> pool;
  Entity e = 7;
  pool.Add(e, Position{4, 5, 6});

  const Position &p = pool.Get(e);
  REQUIRE(p.x == 4);
  REQUIRE(p.y == 5);
  REQUIRE(p.z == 6);
}

TEST_CASE("ComponentPool Get is modifiable", "[ecs][pool]") {
  ComponentPool<Position> pool;
  Entity e = 3;
  pool.Add(e);
  pool.Get(e).x = 99.0f;
  REQUIRE(pool.Get(e).x == 99.0f);
}

TEST_CASE("ComponentPool const Get", "[ecs][pool]") {
  ComponentPool<Position> pool;
  Entity e = 1;
  pool.Add(e, Position{10, 20, 30});

  const ComponentPool<Position> &cpool = pool;
  const Position &p = cpool.Get(e);
  REQUIRE(p.x == 10);
}

TEST_CASE("ComponentPool Remove eliminates entry", "[ecs][pool]") {
  ComponentPool<Position> pool;
  Entity e = 5;
  pool.Add(e, Position{1, 2, 3});
  REQUIRE(pool.Has(e));

  pool.Remove(e);
  REQUIRE_FALSE(pool.Has(e));
  REQUIRE(pool.Size() == 0);
}

TEST_CASE("ComponentPool Remove non-existent entity is safe", "[ecs][pool]") {
  ComponentPool<Position> pool;
  // Should not assert/crash — Remove on missing entity is a no-op
  pool.Remove(999);
  REQUIRE(pool.Size() == 0);
}

TEST_CASE("ComponentPool Remove-swap preserves remaining components", "[ecs][pool]") {
  ComponentPool<Position> pool;
  Entity a = 1;
  Entity b = 2;
  Entity c = 3;
  pool.Add(a, Position{1, 0, 0});
  pool.Add(b, Position{2, 0, 0});
  pool.Add(c, Position{3, 0, 0});

  pool.Remove(a); // removes first, triggers swap with last

  REQUIRE_FALSE(pool.Has(a));
  REQUIRE(pool.Has(b));
  REQUIRE(pool.Has(c));
  REQUIRE(pool.Get(b).x == 2);
  REQUIRE(pool.Get(c).x == 3);
  REQUIRE(pool.Size() == 2);
}

TEST_CASE("ComponentPool Remove middle entry with swap", "[ecs][pool]") {
  ComponentPool<Position> pool;
  Entity a = 10;
  Entity b = 20;
  Entity c = 30;
  pool.Add(a, Position{10, 0, 0});
  pool.Add(b, Position{20, 0, 0});
  pool.Add(c, Position{30, 0, 0});

  pool.Remove(b); // remove middle

  REQUIRE_FALSE(pool.Has(b));
  REQUIRE(pool.Has(a));
  REQUIRE(pool.Has(c));
  REQUIRE(pool.Get(a).x == 10);
  REQUIRE(pool.Get(c).x == 30);
}

TEST_CASE("ComponentPool GetAll returns dense storage", "[ecs][pool]") {
  ComponentPool<Position> pool;
  pool.Add(1, Position{1, 0, 0});
  pool.Add(2, Position{2, 0, 0});

  const auto &all = pool.GetAll();
  REQUIRE(all.size() == 2);
}

TEST_CASE("ComponentPool GetEntities returns entity list", "[ecs][pool]") {
  ComponentPool<Position> pool;
  Entity e1 = 100;
  Entity e2 = 200;
  pool.Add(e1);
  pool.Add(e2);

  const auto &entities = pool.GetEntities();
  REQUIRE(entities.size() == 2);
  // Both entities must appear
  bool foundE1 = false;
  bool foundE2 = false;
  for (Entity e : entities) {
    if (e == e1)
      foundE1 = true;
    if (e == e2)
      foundE2 = true;
  }
  REQUIRE(foundE1);
  REQUIRE(foundE2);
}

TEST_CASE("ComponentPool multiple adds and removes maintain consistency", "[ecs][pool]") {
  ComponentPool<Velocity> pool;
  for (Entity i = 0; i < 10; ++i)
    pool.Add(i, Velocity{(float)i, 0, 0});

  REQUIRE(pool.Size() == 10);

  // Remove every other entity
  for (Entity i = 0; i < 10; i += 2)
    pool.Remove(i);

  REQUIRE(pool.Size() == 5);

  // Remaining entities (1, 3, 5, 7, 9) should still be accessible
  for (Entity i = 1; i < 10; i += 2) {
    REQUIRE(pool.Has(i));
    REQUIRE(pool.Get(i).vx == (float)i);
  }
}

// ============================================================
// Registry
// ============================================================

TEST_CASE("Registry Create returns valid entity", "[ecs][registry]") {
  Registry reg;
  Entity e = reg.Create();
  REQUIRE(e != INVALID_ENTITY);
  REQUIRE(reg.IsAlive(e));
}

TEST_CASE("Registry Create gives unique IDs", "[ecs][registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  Entity c = reg.Create();
  REQUIRE(a != b);
  REQUIRE(b != c);
  REQUIRE(a != c);
}

TEST_CASE("Registry IsAlive returns false for unknown entity", "[ecs][registry]") {
  Registry reg;
  REQUIRE_FALSE(reg.IsAlive(9999));
  REQUIRE_FALSE(reg.IsAlive(INVALID_ENTITY));
}

TEST_CASE("Registry Destroy marks entity as dead", "[ecs][registry]") {
  Registry reg;
  Entity e = reg.Create();
  REQUIRE(reg.IsAlive(e));

  reg.Destroy(e);
  REQUIRE_FALSE(reg.IsAlive(e));
}

TEST_CASE("Registry entity IDs are reused after destroy", "[ecs][registry]") {
  Registry reg;
  Entity first = reg.Create();
  reg.Destroy(first);
  Entity reused = reg.Create();
  // The recycled ID must equal the destroyed one
  REQUIRE(reused == first);
  REQUIRE(reg.IsAlive(reused));
}

TEST_CASE("Registry Add and Has component", "[ecs][registry]") {
  Registry reg;
  Entity e = reg.Create();

  REQUIRE_FALSE(reg.Has<Position>(e));
  reg.Add<Position>(e, Position{1, 2, 3});
  REQUIRE(reg.Has<Position>(e));
}

TEST_CASE("Registry Get returns correct component value", "[ecs][registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Position>(e, Position{7, 8, 9});

  const Position &p = reg.Get<Position>(e);
  REQUIRE(p.x == 7);
  REQUIRE(p.y == 8);
  REQUIRE(p.z == 9);
}

TEST_CASE("Registry Get is modifiable", "[ecs][registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Position>(e);
  reg.Get<Position>(e).x = 42.0f;
  REQUIRE(reg.Get<Position>(e).x == 42.0f);
}

TEST_CASE("Registry const Get", "[ecs][registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Position>(e, Position{3, 6, 9});

  const Registry &creg = reg;
  const Position &p = creg.Get<Position>(e);
  REQUIRE(p.z == 9);
}

TEST_CASE("Registry Remove component", "[ecs][registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Position>(e, Position{1, 2, 3});
  REQUIRE(reg.Has<Position>(e));

  reg.Remove<Position>(e);
  REQUIRE_FALSE(reg.Has<Position>(e));
}

TEST_CASE("Registry multiple component types per entity", "[ecs][registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Position>(e, Position{1, 2, 3});
  reg.Add<Velocity>(e, Velocity{4, 5, 6});

  REQUIRE(reg.Has<Position>(e));
  REQUIRE(reg.Has<Velocity>(e));

  REQUIRE(reg.Get<Position>(e).x == 1);
  REQUIRE(reg.Get<Velocity>(e).vx == 4);
}

TEST_CASE("Registry Has returns false for component on different entity", "[ecs][registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  reg.Add<Position>(a);

  REQUIRE(reg.Has<Position>(a));
  REQUIRE_FALSE(reg.Has<Position>(b));
}

TEST_CASE("Registry GetEntities returns entities with component", "[ecs][registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  reg.Create(); // c — no Tag

  reg.Add<Tag>(a);
  reg.Add<Tag>(b);

  const auto &entities = reg.GetEntities<Tag>();
  REQUIRE(entities.size() == 2);
  bool foundA = false;
  bool foundB = false;
  for (Entity e : entities) {
    if (e == a)
      foundA = true;
    if (e == b)
      foundB = true;
  }
  REQUIRE(foundA);
  REQUIRE(foundB);
}

TEST_CASE("Registry GetEntities returns empty for unknown component type", "[ecs][registry]") {
  Registry reg;
  reg.Create();
  const auto &entities = reg.GetEntities<Velocity>();
  REQUIRE(entities.empty());
}

TEST_CASE("Registry: destroying entity with components leaves others intact", "[ecs][registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  reg.Add<Position>(a, Position{1, 0, 0});
  reg.Add<Position>(b, Position{2, 0, 0});

  reg.Destroy(a);

  REQUIRE_FALSE(reg.IsAlive(a));
  REQUIRE(reg.IsAlive(b));
  REQUIRE(reg.Has<Position>(b));
  REQUIRE(reg.Get<Position>(b).x == 2);
}

TEST_CASE("Registry many entities lifecycle", "[ecs][registry]") {
  Registry reg;
  const int N = 50;
  std::vector<Entity> entities;
  entities.reserve(N);

  for (int i = 0; i < N; ++i) {
    Entity e = reg.Create();
    reg.Add<Position>(e, Position{(float)i, 0, 0});
    entities.push_back(e);
  }

  // Remove even-indexed
  for (int i = 0; i < N; i += 2)
    reg.Destroy(entities[i]);

  // Odd-indexed should still be alive with correct values
  for (int i = 1; i < N; i += 2) {
    REQUIRE(reg.IsAlive(entities[i]));
    REQUIRE(reg.Get<Position>(entities[i]).x == (float)i);
  }
}
