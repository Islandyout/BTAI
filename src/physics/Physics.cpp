#include "btai/physics/Physics.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace btai::physics {

PhysicsWorld::PhysicsWorld(Config config) : config_(config) {
  if (!(config_.fixedStep > 0.0f)) config_.fixedStep = 1.0f / 60.0f;
  if (!(config_.cellSize > 0.0f)) config_.cellSize = 4.0f;
}

std::size_t PhysicsWorld::CellHash::operator()(Cell c) const noexcept {
  const auto h = [](std::uint32_t x) { return std::hash<std::uint32_t>{}(x); };
  std::size_t r = h(static_cast<std::uint32_t>(c.x));
  r ^= h(static_cast<std::uint32_t>(c.y)) + 0x9e3779b97f4a7c15ULL + (r << 6U) + (r >> 2U);
  r ^= h(static_cast<std::uint32_t>(c.z)) + 0x9e3779b97f4a7c15ULL + (r << 6U) + (r >> 2U);
  return r;
}

ecs::Vec3 PhysicsWorld::add(ecs::Vec3 a, ecs::Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
ecs::Vec3 PhysicsWorld::sub(ecs::Vec3 a, ecs::Vec3 b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
ecs::Vec3 PhysicsWorld::mul(ecs::Vec3 a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
float PhysicsWorld::dot(ecs::Vec3 a, ecs::Vec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
float PhysicsWorld::lengthSq(ecs::Vec3 a) noexcept { return dot(a,a); }
ecs::Vec3 PhysicsWorld::normalize(ecs::Vec3 a) noexcept {
  const float d = lengthSq(a);
  return d > 1e-10f ? mul(a, 1.0f/std::sqrt(d)) : ecs::Vec3{1.0f,0.0f,0.0f};
}
ecs::Vec3 PhysicsWorld::clamp(ecs::Vec3 v, ecs::Vec3 lo, ecs::Vec3 hi) noexcept {
  return {std::clamp(v.x,lo.x,hi.x),std::clamp(v.y,lo.y,hi.y),std::clamp(v.z,lo.z,hi.z)};
}

PhysicsWorld::Cell PhysicsWorld::cell(ecs::Vec3 p) const noexcept {
  return {static_cast<int>(std::floor(p.x/config_.cellSize)),static_cast<int>(std::floor(p.y/config_.cellSize)),static_cast<int>(std::floor(p.z/config_.cellSize))};
}

void PhysicsWorld::integrate(Body& b, float dt) const noexcept {
  if (!b.body->dynamic) return;
  b.velocity->value = add(b.velocity->value, mul(config_.gravity, dt));
  b.transform->position = add(b.transform->position, mul(b.velocity->value, dt));
}

void PhysicsWorld::collide(Body& a, Body& b) {
  if (!a.body->dynamic && !b.body->dynamic) return;
  ecs::Vec3 normal{};
  float penetration = 0.0f;
  if (a.collider->type == ecs::Collider::Type::Sphere && b.collider->type == ecs::Collider::Type::Sphere) {
    const ecs::Vec3 delta = sub(b.transform->position, a.transform->position);
    const float radius = a.collider->radius + b.collider->radius;
    const float d2 = lengthSq(delta);
    if (d2 >= radius*radius) return;
    const float d = std::sqrt(std::max(d2,1e-10f));
    normal = mul(delta,1.0f/d);
    penetration = radius-d;
  } else {
    const ecs::Vec3 ah = a.collider->type == ecs::Collider::Type::AABB ? a.collider->halfExtents : ecs::Vec3{a.collider->radius,a.collider->radius,a.collider->radius};
    const ecs::Vec3 bh = b.collider->type == ecs::Collider::Type::AABB ? b.collider->halfExtents : ecs::Vec3{b.collider->radius,b.collider->radius,b.collider->radius};
    const ecs::Vec3 d = sub(b.transform->position,a.transform->position);
    const float px = ah.x+bh.x-std::abs(d.x), py = ah.y+bh.y-std::abs(d.y), pz = ah.z+bh.z-std::abs(d.z);
    if (px <= 0.0f || py <= 0.0f || pz <= 0.0f) return;
    if (px <= py && px <= pz) { normal={d.x>=0?1.0f:-1.0f,0,0}; penetration=px; }
    else if (py <= pz) { normal={0,d.y>=0?1.0f:-1.0f,0}; penetration=py; }
    else { normal={0,0,d.z>=0?1.0f:-1.0f}; penetration=pz; }
  }
  ++lastContactCount_;
  const float invA = a.body->dynamic ? a.body->inverseMass : 0.0f;
  const float invB = b.body->dynamic ? b.body->inverseMass : 0.0f;
  const float invSum = invA+invB;
  if (invSum <= 0.0f) return;
  const ecs::Vec3 correction = mul(normal, penetration/invSum*0.8f);
  if (a.body->dynamic) a.transform->position = sub(a.transform->position,mul(correction,invA));
  if (b.body->dynamic) b.transform->position = add(b.transform->position,mul(correction,invB));
  const float relative = dot(sub(b.velocity->value,a.velocity->value),normal);
  if (relative >= 0.0f) return;
  const float impulseMagnitude = -(1.0f+config_.restitution)*relative/invSum;
  const ecs::Vec3 impulse = mul(normal,impulseMagnitude);
  if (a.body->dynamic) a.velocity->value = sub(a.velocity->value,mul(impulse,invA));
  if (b.body->dynamic) b.velocity->value = add(b.velocity->value,mul(impulse,invB));
}

void PhysicsWorld::step(ecs::Registry& registry, float dt) {
  if (!(dt > 0.0f)) return;
  bodies_.clear();
  grid_.clear();
  lastPairCount_ = lastContactCount_ = 0;
  registry.each<ecs::Transform,ecs::Velocity,ecs::RigidBody,ecs::Collider>([&](ecs::Entity e, ecs::Transform& t, ecs::Velocity& v, ecs::RigidBody& r, ecs::Collider& c) {
    if (!(r.mass > 0.0f) && r.dynamic) { r.mass=1.0f; r.inverseMass=1.0f; }
    if (r.dynamic) r.inverseMass=1.0f/r.mass;
    bodies_.push_back({e,&t,&v,&r,&c});
  });
  for (Body& b : bodies_) integrate(b,dt);
  for (std::size_t i=0; i<bodies_.size(); ++i) {
    const Cell c = cell(bodies_[i].transform->position);
    grid_[c].push_back(i);
  }
  for (std::size_t i=0; i<bodies_.size(); ++i) {
    const Cell c = cell(bodies_[i].transform->position);
    for (int z=-1; z<=1; ++z) for (int y=-1; y<=1; ++y) for (int x=-1; x<=1; ++x) {
      const auto it = grid_.find({c.x+x,c.y+y,c.z+z});
      if (it == grid_.end()) continue;
      for (std::size_t j : it->second) {
        if (j <= i) continue;
        ++lastPairCount_;
        collide(bodies_[i],bodies_[j]);
      }
    }
  }
}

void PhysicsWorld::clear() {
  bodies_.clear();
  grid_.clear();
  lastPairCount_ = lastContactCount_ = 0;
}

} // namespace btai::physics
