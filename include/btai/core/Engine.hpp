#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include "btai/core/Log.hpp"

namespace btai {

class Window;
class VulkanRenderer;

struct EngineConfig {
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  const char* title = "BTAI";
};

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
  EngineConfig config_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<VulkanRenderer> renderer_;
  std::atomic_bool running_{false};
};

} // namespace btai
