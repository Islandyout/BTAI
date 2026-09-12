#include "btai/core/Engine.hpp"
#include "btai/platform/Window.hpp"
#include "btai/render/VulkanRenderer.hpp"

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
  return true;
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
  if (renderer_) renderer_->shutdown();
  renderer_.reset();
  window_.reset();
}

} // namespace btai
