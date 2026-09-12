#include "btai/physics/Physics.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace btai::physics {

PhysicsWorld::PhysicsWorld(Config config) : config_(config) {
  if (!(config_.fixedStep > 0.0f)) config_.fixedStep = 1.0f / 60.0f;
  if (!(config_.cellSize > 0.0f)) config_.cellSize = 4.0f;
  config_.restitution = std::clamp(config_.restitution, 0.0f, 1.0f);
  config_.friction = std::max(0.0f, config_.friction);
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
  const auto aPos = a.transform->position;
  const auto bPos = b.transform->position;

  if (a.collider->type == ecs::Collider::Type::Sphere && b.collider->type == ecs::Collider::Type::Sphere) {
    const ecs::Vec3 delta = sub(bPos, aPos);
    const float radius = std::max(0.0f, a.collider->radius) + std::max(0.0f, b.collider->radius);
    const float d2 = lengthSq(delta);
    if (d2 >= radius*radius) return;
    const float d = std::sqrt(std::max(d2, 1e-10f));
    normal = mul(delta, 1.0f/d);
    penetration = radius-d;
  } else if (a.collider->type == ecs::Collider::Type::AABB && b.collider->type == ecs::Collider::Type::AABB) {
    const ecs::Vec3 ah = a.collider->halfExtents;
    const ecs::Vec3 bh = b.collider->halfExtents;
    const ecs::Vec3 d = sub(bPos, aPos);
    const float px = ah.x+bh.x-std::abs(d.x), py = ah.y+bh.y-std::abs(d.y), pz = ah.z+bh.z-std::abs(d.z);
    if (px <= 0.0f || py <= 0.0f || pz <= 0.0f) return;
    if (px <= py && px <= pz) { normal={d.x>=0.0f?1.0f:-1.0f,0,0}; penetration=px; }
    else if (py <= pz) { normal={0,d.y>=0.0f?1.0f:-1.0f,0}; penetration=py; }
    else { normal={0,0,d.z>=0.0f?1.0f:-1.0f}; penetration=pz; }
  } else {
    Body* sphere = a.collider->type == ecs::Collider::Type::Sphere ? &a : &b;
    Body* box = sphere == &a ? &b : &a;
    const ecs::Vec3 boxHalf = box->collider->halfExtents;
    const float radius = std::max(0.0f, sphere->collider->radius);
    const ecs::Vec3 local = sub(sphere->transform->position, box->transform->position);
    const ecs::Vec3 closest = clamp(local, mul(boxHalf,-1.0f), boxHalf);
    const ecs::Vec3 delta = sub(local, closest);
    const float d2 = lengthSq(delta);
    if (d2 > radius*radius) return;
    if (d2 > 1e-10f) {
      const ecs::Vec3 n = normalize(delta);
      normal = sphere == &a ? mul(n,-1.0f) : n;
      penetration = radius-std::sqrt(d2);
    } else {
      const float dx = boxHalf.x-std::abs(local.x);
      const float dy = boxHalf.y-std::abs(local.y);
      const float dz = boxHalf.z-std::abs(local.z);
      if (dx <= dy && dx <= dz) normal = sphere == &a ? ecs::Vec3{local.x>=0.0f?-1.0f:1.0f,0,0} : ecs::Vec3{local.x>=0.0f?1.0f:-1.0f,0,0}, penetration=radius+dx;
      else if (dy <= dz) normal = sphere == &a ? ecs::Vec3{0,local.y>=0.0f?-1.0f:1.0f,0} : ecs::Vec3{0,local.y>=0.0f?1.0f:-1.0f,0}, penetration=radius+dy;
      else normal = sphere == &a ? ecs::Vec3{0,0,local.z>=0.0f?-1.0f:1.0f} : ecs::Vec3{0,0,local.z>=0.0f?1.0f:-1.0f}, penetration=radius+dz;
    }
  }

  ++lastContactCount_;
  const float invA = a.body->dynamic ? std::max(0.0f, a.body->inverseMass) : 0.0f;
  const float invB = b.body->dynamic ? std::max(0.0f, b.body->inverseMass) : 0.0f;
  const float invSum = invA+invB;
  if (invSum <= std::numeric_limits<float>::epsilon()) return;

  const ecs::Vec3 correction = mul(normal, penetration/invSum*0.8f);
  if (a.body->dynamic) a.transform->position = sub(a.transform->position,mul(correction,invA));
  if (b.body->dynamic) b.transform->position = add(b.transform->position,mul(correction,invB));

  const float relative = dot(sub(b.velocity->value,a.velocity->value),normal);
  if (relative >= 0.0f) return;
  const float impulseMagnitude = -(1.0f+config_.restitution)*relative/invSum;
  const ecs::Vec3 impulse = mul(normal,impulseMagnitude);
  if (a.body->dynamic) a.velocity->value = sub(a.velocity->value,mul(impulse,invA));
  if (b.body->dynamic) b.velocity->value = add(b.velocity->value,mul(impulse,invB));

  const ecs::Vec3 tangentVelocity = sub(sub(b.velocity->value,a.velocity->value),mul(normal,dot(sub(b.velocity->value,a.velocity->value),normal)));
  const float tangentSq = lengthSq(tangentVelocity);
  if (tangentSq > 1e-10f && config_.friction > 0.0f) {
    const ecs::Vec3 tangent = mul(tangentVelocity,1.0f/std::sqrt(tangentSq));
    const float frictionImpulse = std::min(std::abs(impulseMagnitude)*config_.friction, std::sqrt(tangentSq)/invSum);
    const ecs::Vec3 friction = mul(tangent,frictionImpulse);
    if (a.body->dynamic) a.velocity->value = add(a.velocity->value,mul(friction,invA));
    if (b.body->dynamic) b.velocity->value = sub(b.velocity->value,mul(friction,invB));
  }
}

void PhysicsWorld::step(ecs::Registry& registry, float dt) {
  if (!(dt > 0.0f)) return;
  bodies_.clear();
  grid_.clear();
  lastPairCount_ = lastContactCount_ = 0;
  registry.each<ecs::Transform,ecs::Velocity,ecs::RigidBody,ecs::Collider>([&](ecs::Entity e, ecs::Transform& t, ecs::Velocity& v, ecs::RigidBody& r, ecs::Collider& c) {
    if (r.dynamic) {
      if (!(r.mass > 0.0f)) r.mass = 1.0f;
      r.inverseMass = 1.0f/r.mass;
    } else r.inverseMass = 0.0f;
    bodies_.push_back({e,&t,&v,&r,&c});
  });
  for (Body& b : bodies_) integrate(b,dt);

  for (std::size_t i=0; i<bodies_.size(); ++i) {
    const Body& b = bodies_[i];
    const ecs::Vec3 p = b.transform->position;
    ecs::Vec3 half = b.collider->type == ecs::Collider::Type::AABB ? b.collider->halfExtents : ecs::Vec3{b.collider->radius,b.collider->radius,b.collider->radius};
    const Cell lo = cell(sub(p,half));
    const Cell hi = cell(add(p,half));
    for (int z=lo.z; z<=hi.z; ++z) for (int y=lo.y; y<=hi.y; ++y) for (int x=lo.x; x<=hi.x; ++x) grid_[{x,y,z}].push_back(i);
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
