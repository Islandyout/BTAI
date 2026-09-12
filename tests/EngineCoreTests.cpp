#include "btai/ai/CommandInterpreter.hpp"
#include "btai/ecs/Registry.hpp"
#include "btai/jobs/JobSystem.hpp"
#include <cassert>
#include <atomic>

int main() {
  btai::ecs::Registry registry;
  const auto first=registry.create();
  registry.add<btai::ecs::Transform>(first,btai::ecs::Vec3{1.0f,2.0f,3.0f});
  assert(registry.alive(first)&&registry.size()==1&&registry.has<btai::ecs::Transform>(first));
  assert(registry.get<btai::ecs::Transform>(first)->position.z==3.0f);
  assert(registry.destroy(first)&&!registry.alive(first));
  const auto second=registry.create();
  assert(second.index==first.index&&second.generation!=first.generation&&!registry.alive(first));

  btai::JobSystem jobs(2);
  std::atomic_int total=0;
  auto a=jobs.submit([&]{total.fetch_add(2,std::memory_order_relaxed);});
  auto b=jobs.submit([&]{total.fetch_add(3,std::memory_order_relaxed);});
  a.get();b.get();jobs.waitIdle();assert(total.load(std::memory_order_relaxed)==5);jobs.stop();

  btai::ai::CommandInterpreter ai(registry);
  const auto spawned=ai.execute(R"({"command":"spawn_entity","transform":[1,2,3],"velocity":[0,0,1],"physics":true,"name":"ped"})");
  assert(spawned.ok&&registry.has<btai::ecs::RigidBody>(spawned.entity));
  const auto moved=ai.execute(nlohmann::json{{"command","set_transform"},{"entity",{{"index",spawned.entity.index},{"generation",spawned.entity.generation}}},{"position",{4,5,6}}});
  assert(moved.ok&&registry.get<btai::ecs::Transform>(spawned.entity)->position.x==4.0f);
  const auto state=ai.execute(nlohmann::json{{"command","set_ai_state"},{"entity",{{"index",spawned.entity.index},{"generation",spawned.entity.generation}}},{"state","Running"}});
  assert(state.ok&&registry.get<btai::ecs::AIState>(spawned.entity)->state==btai::ecs::AIState::State::Running);
  assert(!ai.execute("{bad json").ok);
  assert(!ai.execute(nlohmann::json{{"command","set_ai_state"},{"entity",{{"index",spawned.entity.index},{"generation",spawned.entity.generation}}},{"state","Unknown"}}).ok);
  assert(ai.execute(nlohmann::json{{"command","destroy_entity"},{"entity",{{"index",spawned.entity.index},{"generation",spawned.entity.generation}}}}).ok);
  return 0;
}
