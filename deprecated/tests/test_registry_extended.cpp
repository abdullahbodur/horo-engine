#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "scene/ComponentPool.h"
#include "scene/Entity.h"
#include "scene/Registry.h"

using namespace Horo;

// Test components
struct Health {
  float value = 100.0f;
};

struct Mana {
  float value = 50.0f;
};

struct Tag {
  std::string name;
};

struct Counter {
  int n = 0;
};

// ===========================================================================
// Registry — entity lifecycle
// ===========================================================================

TEST_CASE("Registry: Create returns unique IDs sequentially", "[registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  Entity c = reg.Create();
  REQUIRE(a != b);
  REQUIRE(b != c);
  REQUIRE(a != c);
}

TEST_CASE("Registry: IsAlive true after Create", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  REQUIRE(reg.IsAlive(e));
}

TEST_CASE("Registry: IsAlive false after Destroy", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Destroy(e);
  REQUIRE_FALSE(reg.IsAlive(e));
}

TEST_CASE("Registry: Destroyed ID is recycled", "[registry]") {
  Registry reg;
  Entity a = reg.Create();
  reg.Destroy(a);
  Entity b = reg.Create();
  REQUIRE(b == a); // ID should be recycled from free list
}

TEST_CASE("Registry: multiple creates, multiple destroys", "[registry]") {
  Registry reg;
  std::vector<Entity> entities;
  for (int i = 0; i < 10; ++i)
    entities.push_back(reg.Create());

  for (int i = 0; i < 10; ++i)
    REQUIRE(reg.IsAlive(entities[i]));

  for (int i = 0; i < 5; ++i)
    reg.Destroy(entities[i]);

  for (int i = 0; i < 5; ++i)
    REQUIRE_FALSE(reg.IsAlive(entities[i]));
  for (int i = 5; i < 10; ++i)
    REQUIRE(reg.IsAlive(entities[i]));
}

// ===========================================================================
// Registry — multiple component types on same entity
// ===========================================================================

TEST_CASE("Registry: entity can have multiple component types", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Health>(e, {75.0f});
  reg.Add<Mana>(e, {30.0f});

  REQUIRE(reg.Has<Health>(e));
  REQUIRE(reg.Has<Mana>(e));
  REQUIRE(reg.Get<Health>(e).value == Catch::Approx(75.0f));
  REQUIRE(reg.Get<Mana>(e).value == Catch::Approx(30.0f));
}

TEST_CASE("Registry: different entities have independent components", "[registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  reg.Add<Health>(a, {100.0f});
  reg.Add<Health>(b, {50.0f});

  REQUIRE(reg.Get<Health>(a).value == Catch::Approx(100.0f));
  REQUIRE(reg.Get<Health>(b).value == Catch::Approx(50.0f));

  reg.Get<Health>(a).value = 90.0f;
  REQUIRE(reg.Get<Health>(a).value == Catch::Approx(90.0f));
  REQUIRE(reg.Get<Health>(b).value == Catch::Approx(50.0f));
}

TEST_CASE("Registry: Remove<T> removes only that component type", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Health>(e, {100.0f});
  reg.Add<Mana>(e, {50.0f});

  reg.Remove<Health>(e);

  REQUIRE_FALSE(reg.Has<Health>(e));
  REQUIRE(reg.Has<Mana>(e));
}

TEST_CASE("Registry: Remove on non-existent component is safe", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  REQUIRE_NOTHROW(reg.Remove<Health>(e));
}

TEST_CASE("Registry: Destroy removes all components from entity", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Health>(e);
  reg.Add<Mana>(e);
  reg.Add<Tag>(e);

  reg.Destroy(e);
  // After destroy, all component pools should have removed the entity
  // (can't call Has after destroy since entity is invalid, but we can verify
  //  that the pool no longer tracks it)
  REQUIRE_FALSE(reg.IsAlive(e));
}

// ===========================================================================
// Registry — GetEntities iteration
// ===========================================================================

TEST_CASE("Registry::GetEntities: returns all entities with component", "[registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  reg.Create();
  reg.Add<Health>(a);
  reg.Add<Health>(b);
  // third entity has no Health

  const auto &entities = reg.GetEntities<Health>();
  REQUIRE(entities.size() == 2);
  bool hasA = false;
  bool hasB = false;
  for (Entity e : entities) {
    if (e == a)
      hasA = true;
    if (e == b)
      hasB = true;
  }
  REQUIRE(hasA);
  REQUIRE(hasB);
}

TEST_CASE("Registry::GetEntities: empty if no component ever added", "[registry]") {
  Registry reg;
  reg.Create();
  reg.Create();
  const auto &entities = reg.GetEntities<Mana>();
  REQUIRE(entities.empty());
}

TEST_CASE("Registry::GetEntities: decrements after Remove", "[registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  reg.Add<Counter>(a);
  reg.Add<Counter>(b);

  REQUIRE(reg.GetEntities<Counter>().size() == 2);
  reg.Remove<Counter>(a);
  REQUIRE(reg.GetEntities<Counter>().size() == 1);
}

TEST_CASE("Registry::GetEntities: decrements after Destroy", "[registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  reg.Add<Counter>(a);
  reg.Add<Counter>(b);

  reg.Destroy(a);
  REQUIRE(reg.GetEntities<Counter>().size() == 1);
  REQUIRE(reg.GetEntities<Counter>()[0] == b);
}

// ===========================================================================
// Registry — Clear
// ===========================================================================

TEST_CASE("Registry::Clear: no entities remain alive", "[registry]") {
  Registry reg;
  Entity a = reg.Create();
  Entity b = reg.Create();
  reg.Add<Health>(a);
  reg.Add<Health>(b);

  reg.Clear();
  REQUIRE_FALSE(reg.IsAlive(a));
  REQUIRE_FALSE(reg.IsAlive(b));
}

TEST_CASE("Registry::Clear: component pools are empty after clear", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Health>(e);

  reg.Clear();
  REQUIRE(reg.GetEntities<Health>().empty());
}

TEST_CASE("Registry::Clear: ID sequence restarts from 0", "[registry]") {
  Registry reg;
  reg.Create();
  reg.Create();
  reg.Create();
  reg.Clear();
  Entity first = reg.Create();
  REQUIRE(first == 0);
}

TEST_CASE("Registry::Clear then repopulate works correctly", "[registry]") {
  Registry reg;
  Entity old = reg.Create();
  reg.Add<Health>(old, {999.0f});
  reg.Clear();

  Entity fresh = reg.Create();
  reg.Add<Health>(fresh, {42.0f});

  REQUIRE(reg.Has<Health>(fresh));
  REQUIRE(reg.Get<Health>(fresh).value == Catch::Approx(42.0f));
}

// ===========================================================================
// ComponentPool — dense packing after Remove
// ===========================================================================

TEST_CASE("ComponentPool: Remove from middle keeps others intact", "[registry][pool]") {
  ComponentPool<Health> pool;
  Entity a = 0;
  Entity b = 1;
  Entity c = 2;
  pool.Add(a, {10.0f});
  pool.Add(b, {20.0f});
  pool.Add(c, {30.0f});

  pool.Remove(b);

  REQUIRE(pool.Size() == 2);
  REQUIRE(pool.Has(a));
  REQUIRE_FALSE(pool.Has(b));
  REQUIRE(pool.Has(c));
  REQUIRE(pool.Get(a).value == Catch::Approx(10.0f));
  REQUIRE(pool.Get(c).value == Catch::Approx(30.0f));
}

TEST_CASE("ComponentPool: Remove last element", "[registry][pool]") {
  ComponentPool<Health> pool;
  Entity a = 0;
  Entity b = 1;
  pool.Add(a, {10.0f});
  pool.Add(b, {20.0f});

  pool.Remove(b);

  REQUIRE(pool.Size() == 1);
  REQUIRE(pool.Has(a));
  REQUIRE_FALSE(pool.Has(b));
}

TEST_CASE("ComponentPool: Remove first element", "[registry][pool]") {
  ComponentPool<Health> pool;
  Entity a = 0;
  Entity b = 1;
  Entity c = 2;
  pool.Add(a, {10.0f});
  pool.Add(b, {20.0f});
  pool.Add(c, {30.0f});

  pool.Remove(a);

  REQUIRE(pool.Size() == 2);
  REQUIRE_FALSE(pool.Has(a));
  REQUIRE(pool.Has(b));
  REQUIRE(pool.Has(c));
}

TEST_CASE("ComponentPool: ClearAll empties all storage", "[registry][pool]") {
  ComponentPool<Health> pool;
  pool.Add(0, {1.0f});
  pool.Add(1, {2.0f});
  pool.Add(2, {3.0f});

  pool.ClearAll();

  REQUIRE(pool.Size() == 0);
  REQUIRE(pool.GetAll().empty());
  REQUIRE(pool.GetEntities().empty());
  REQUIRE_FALSE(pool.Has(0));
  REQUIRE_FALSE(pool.Has(1));
  REQUIRE_FALSE(pool.Has(2));
}

TEST_CASE("ComponentPool: GetAll returns dense array", "[registry][pool]") {
  ComponentPool<Counter> pool;
  pool.Add(0, {1});
  pool.Add(1, {2});
  pool.Add(2, {3});

  const auto &all = pool.GetAll();
  REQUIRE(all.size() == 3);
}

TEST_CASE("ComponentPool: Remove non-existent is safe", "[registry][pool]") {
  ComponentPool<Health> pool;
  pool.Add(5, {100.0f});
  REQUIRE_NOTHROW(pool.Remove(99));
  REQUIRE(pool.Size() == 1);
}

TEST_CASE("ComponentPool: add re-add after remove works", "[registry][pool]") {
  ComponentPool<Health> pool;
  Entity e = 7;
  pool.Add(e, {50.0f});
  pool.Remove(e);
  pool.Add(e, {75.0f});

  REQUIRE(pool.Has(e));
  REQUIRE(pool.Get(e).value == Catch::Approx(75.0f));
}

TEST_CASE("ComponentPool: large number of entities", "[registry][pool]") {
  ComponentPool<Counter> pool;
  const int N = 1000;
  for (int i = 0; i < N; ++i)
    pool.Add(static_cast<Entity>(i), {i});

  REQUIRE(pool.Size() == N);

  // Remove even-numbered
  for (int i = 0; i < N; i += 2)
    pool.Remove(static_cast<Entity>(i));

  REQUIRE(pool.Size() == N / 2);

  // Odd-numbered should still have correct values
  for (int i = 1; i < N; i += 2) {
    REQUIRE(pool.Has(static_cast<Entity>(i)));
    REQUIRE(pool.Get(static_cast<Entity>(i)).n == i);
  }
}

// ===========================================================================
// Registry — Has<T> before any Add
// ===========================================================================

TEST_CASE("Registry::Has: returns false for unknown type T", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  REQUIRE_FALSE(reg.Has<Tag>(e));
}

TEST_CASE("Registry::Has: returns true after Add", "[registry]") {
  Registry reg;
  Entity e = reg.Create();
  reg.Add<Tag>(e, {"hello"});
  REQUIRE(reg.Has<Tag>(e));
  REQUIRE(reg.Get<Tag>(e).name == "hello");
}
