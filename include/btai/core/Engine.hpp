#pragma once

#include "btai/ai/CommandInterpreter.hpp"
#include "btai/core/Log.hpp"
#include "btai/ecs/Registry.hpp"
#include "btai/jobs/JobSystem.hpp"
#include "btai/physics/Physics.hpp"
#include "btai/render/RenderSnapshot.hpp"
#include "btai/world/Streaming.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace btai { class Window; class VulkanRenderer;
struct EngineConfig { std::uint32_t width=1280,height=720; const char* title="BTAI"; };
class Engine final {
public:
  explicit Engine(EngineConfig config={}); ~Engine();
  Engine(const Engine&)=delete; Engine& operator=(const Engine&)=delete;
  bool initialize(); int run(); void shutdown() noexcept;
private:
  void simulationLoop(std::stop_token token);
  EngineConfig config_{};
  JobSystem jobs_{};
  ecs::Registry registry_{};
  ai::CommandInterpreter ai_{registry_};
  physics::PhysicsWorld physics_{};
  world::Streamer streamer_{jobs_};
  std::unique_ptr<Window> window_;
  std::unique_ptr<VulkanRenderer> renderer_;
  std::shared_ptr<const render::RenderSnapshot> latestSnapshot_;
  std::jthread simulation_;
  std::atomic_bool running_{false};
};
}
