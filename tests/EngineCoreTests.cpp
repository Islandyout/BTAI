#include "btai/ecs/Registry.hpp"
#include "btai/jobs/JobSystem.hpp"
#include <cassert>
#include <atomic>

int main() {
  btai::ecs::Registry registry;
  const auto first = registry.create();
  registry.add<btai::ecs::Transform>(first, btai::ecs::Vec3{1.0f, 2.0f, 3.0f});
  assert(registry.alive(first));
  assert(registry.size() == 1);
  assert(registry.has<btai::ecs::Transform>(first));
  assert(registry.get<btai::ecs::Transform>(first)->position.z == 3.0f);
  assert(registry.destroy(first));
  assert(!registry.alive(first));
  const auto second = registry.create();
  assert(second.index == first.index);
  assert(second.generation != first.generation);
  assert(!registry.alive(first));

  btai::JobSystem jobs(2);
  std::atomic_int total = 0;
  auto a = jobs.submit([&] { total.fetch_add(2, std::memory_order_relaxed); });
  auto b = jobs.submit([&] { total.fetch_add(3, std::memory_order_relaxed); });
  a.get();
  b.get();
  jobs.waitIdle();
  assert(total.load(std::memory_order_relaxed) == 5);
  jobs.stop();
  return 0;
}
