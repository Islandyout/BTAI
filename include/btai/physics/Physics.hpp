#pragma once

#include "btai/ecs/Registry.hpp"
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace btai::physics {

struct Config {
  float fixedStep = 1.0f / 60.0f;
  ecs::Vec3 gravity{0.0f, -9.81f, 0.0f};
  float cellSize = 4.0f;
  float restitution = 0.15f;
  float friction = 0.7f;
};

class PhysicsWorld final {
public:
  explicit PhysicsWorld(Config config = {});

  void step(ecs::Registry& registry, float dt);
  void clear();
  std::size_t lastPairCount() const noexcept { return lastPairCount_; }
  std::size_t lastContactCount() const noexcept { return lastContactCount_; }

private:
  struct Cell { int x, y, z; bool operator==(const Cell&) const = default; };
  struct CellHash { std::size_t operator()(Cell c) const noexcept; };
  struct Body { ecs::Entity entity; ecs::Transform* transform; ecs::Velocity* velocity; ecs::RigidBody* body; ecs::Collider* collider; };

  static ecs::Vec3 add(ecs::Vec3 a, ecs::Vec3 b) noexcept;
  static ecs::Vec3 sub(ecs::Vec3 a, ecs::Vec3 b) noexcept;
  static ecs::Vec3 mul(ecs::Vec3 a, float s) noexcept;
  static float dot(ecs::Vec3 a, ecs::Vec3 b) noexcept;
  static float lengthSq(ecs::Vec3 a) noexcept;
  static ecs::Vec3 normalize(ecs::Vec3 a) noexcept;
  static ecs::Vec3 clamp(ecs::Vec3 v, ecs::Vec3 lo, ecs::Vec3 hi) noexcept;

  Cell cell(ecs::Vec3 p) const noexcept;
  void integrate(Body& b, float dt) const noexcept;
  void collide(Body& a, Body& b);

  Config config_;
  std::unordered_map<Cell, std::vector<std::size_t>, CellHash> grid_;
  std::vector<Body> bodies_;
  std::size_t lastPairCount_ = 0;
  std::size_t lastContactCount_ = 0;
};

} // namespace btai::physics
