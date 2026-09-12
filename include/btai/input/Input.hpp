#pragma once

#include <array>
#include <cstdint>

struct GLFWwindow;

namespace btai::input {

class Input final {
public:
  bool initialize(GLFWwindow* window) noexcept;
  void poll() noexcept;
  bool key(int key) const noexcept;
  bool pressed(int key) const noexcept;
  bool released(int key) const noexcept;
  bool mouseButton(int button) const noexcept;
  float mouseDeltaX() const noexcept { return mouseDx_; }
  float mouseDeltaY() const noexcept { return mouseDy_; }
  float mouseX() const noexcept { return mouseX_; }
  float mouseY() const noexcept { return mouseY_; }
  bool quitRequested() const noexcept { return quit_; }

private:
  static constexpr int KeyCount = 512;
  static constexpr int MouseCount = 8;
  GLFWwindow* window_ = nullptr;
  std::array<std::uint8_t,KeyCount> current_{};
  std::array<std::uint8_t,KeyCount> previous_{};
  std::array<std::uint8_t,MouseCount> mouse_{};
  double lastMouseX_ = 0.0;
  double lastMouseY_ = 0.0;
  float mouseDx_ = 0.0f;
  float mouseDy_ = 0.0f;
  float mouseX_ = 0.0f;
  float mouseY_ = 0.0f;
  bool firstMouse_ = true;
  bool quit_ = false;
};

} // namespace btai::input
