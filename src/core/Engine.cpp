#include "btai/core/Engine.hpp"
#include "btai/platform/Window.hpp"
#include "btai/render/VulkanRenderer.hpp"
#include <chrono>

namespace btai {

Engine::Engine(EngineConfig config) : config_(config) {}
Engine::~Engine() { shutdown(); }

bool Engine::initialize() {
  if (running_) return true;
  window_ = std::make_unique<Window>(config_.width, config_.height, config_.title);
  if (!window_->valid()) return false;
  renderer_ = std::make_unique<VulkanRenderer>(*window_);
  if (!renderer_->initialize()) {
    renderer_.reset();
    window_.reset();
    return false;
  }
  running_ = true;
  simulation_ = std::jthread([this](std::stop_token token) { simulationLoop(token); });
  return true;
}

void Engine::simulationLoop(std::stop_token token) {
  using clock = std::chrono::steady_clock;
  constexpr auto step = std::chrono::microseconds(16667);
  auto next = clock::now();
  const auto entity = registry_.create();
  registry_.add<ecs::Transform>(entity, ecs::Vec3{0.0f, 0.0f, 0.0f});
  registry_.add<ecs::Velocity>(entity, ecs::Vec3{0.0f, 0.0f, 0.5f});
  registry_.add<ecs::Rotation>(entity);
  registry_.add<ecs::Scale>(entity);
  registry_.add<ecs::Renderable>(entity);

  while (!token.stop_requested() && running_) {
    next += step;
    registry_.each<ecs::Transform, ecs::Velocity>([](ecs::Entity, ecs::Transform& transform, ecs::Velocity& velocity) {
      transform.position.x += velocity.value.x * (1.0f / 60.0f);
      transform.position.y += velocity.value.y * (1.0f / 60.0f);
      transform.position.z += velocity.value.z * (1.0f / 60.0f);
    });
    std::this_thread::sleep_until(next);
    if (clock::now() > next + step * 4) next = clock::now();
  }
}

int Engine::run() {
  if (!running_ && !initialize()) return 1;
  while (running_ && !window_->shouldClose()) {
    window_->poll();
    renderer_->draw();
  }
  shutdown();
  return 0;
}

void Engine::shutdown() noexcept {
  running_ = false;
  if (simulation_.joinable()) {
    simulation_.request_stop();
    simulation_.join();
  }
  if (renderer_) renderer_->shutdown();
  renderer_.reset();
  window_.reset();
  jobs_.stop();
}

} // namespace btai
