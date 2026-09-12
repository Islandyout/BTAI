#pragma once

#include <cstdint>
#include <string>

namespace btai::ecs {

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct Transform { Vec3 position{}; };
struct Rotation { Vec3 euler{}; };
struct Scale { Vec3 value{1.0f, 1.0f, 1.0f}; };
struct Velocity { Vec3 value{}; };
struct Acceleration { Vec3 value{}; };
struct RigidBody { float mass = 1.0f; float inverseMass = 1.0f; bool dynamic = true; };
struct Collider { enum class Type : std::uint8_t { AABB, Sphere }; Type type = Type::AABB; Vec3 halfExtents{0.5f, 0.5f, 0.5f}; float radius = 0.5f; };
struct Health { float current = 100.0f; float maximum = 100.0f; };
struct AIState { enum class State : std::uint8_t { Idle, Walking, Running, Driving, Fleeing, Chasing, Dead }; State state = State::Idle; };
struct Pedestrian { std::uint32_t archetype = 0; };
struct Vehicle { std::uint32_t archetype = 0; };
struct AnimationState { std::uint32_t clip = 0; float time = 0.0f; bool looping = true; };
struct Renderable { std::uint32_t mesh = 0; std::uint32_t material = 0; bool visible = true; };
struct Name { std::string value; };

} // namespace btai::ecs
