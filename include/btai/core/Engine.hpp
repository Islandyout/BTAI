#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include "btai/core/Log.hpp"
#include "btai/ecs/Registry.hpp"
#include "btai/jobs/JobSystem.hpp"
#include "btai/physics/Physics.hpp"
#include "btai/world/Streaming.hpp"

namespace btai {
class Window;
class VulkanRenderer;

struct EngineConfig { std::uint32_t width=1280; std::uint32_t height=720; const char* title="BTAI"; };

class Engine final {
public:
  explicit Engine(EngineConfig config = {});
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  bool initialize();
  int run();
  void shutdown() noexcept;
private:
  void simulationLoop(std::stop_token token);
  EngineConfig config_;
  JobSystem jobs_;
  ecs::Registry registry_;
  physics::PhysicsWorld physics_;
  world::Streamer streamer_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<VulkanRenderer> renderer_;
  std::jthread simulation_;
  std::atomic_bool running_{false};
};
} // namespace btai
