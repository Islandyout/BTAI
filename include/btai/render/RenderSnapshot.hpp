#pragma once

#include "btai/ecs/Components.hpp"
#include "btai/ecs/Registry.hpp"
#include <cstdint>
#include <vector>

namespace btai::render {

struct RenderInstance {
  ecs::Entity entity{};
  ecs::Vec3 position{};
  ecs::Vec3 rotation{};
  ecs::Vec3 scale{1.0f, 1.0f, 1.0f};
  std::uint32_t mesh = 0;
  std::uint32_t material = 0;
};

struct RenderSnapshot {
  std::uint64_t tick = 0;
  std::vector<RenderInstance> instances;
};

inline void extract(const ecs::Registry& registry, RenderSnapshot& snapshot) {
  snapshot.instances.clear();
  snapshot.instances.reserve(registry.size());
  registry.each<ecs::Transform, ecs::Renderable>([&](ecs::Entity entity, const ecs::Transform& transform, const ecs::Renderable& renderable) {
    if (!renderable.visible) return;
    RenderInstance instance;
    instance.entity = entity;
    instance.position = transform.position;
    instance.mesh = renderable.mesh;
    instance.material = renderable.material;
    if (const auto* rotation = registry.get<ecs::Rotation>(entity)) instance.rotation = rotation->euler;
    if (const auto* scale = registry.get<ecs::Scale>(entity)) instance.scale = scale->value;
    snapshot.instances.push_back(instance);
  });
}

} // namespace btai::render
