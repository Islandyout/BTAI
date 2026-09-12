#pragma once

#include <cstdint>
struct GLFWwindow;

namespace btai {

class Window final {
public:
  Window(std::uint32_t width, std::uint32_t height, const char* title);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool valid() const noexcept;
  bool shouldClose() const noexcept;
  void poll() noexcept;
  GLFWwindow* native() const noexcept;

private:
  GLFWwindow* handle_ = nullptr;
};

} // namespace btai
