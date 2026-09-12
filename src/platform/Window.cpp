#include "btai/platform/Window.hpp"
#include <GLFW/glfw3.h>
#include "btai/core/Log.hpp"

namespace btai {

Window::Window(std::uint32_t width, std::uint32_t height, const char* title) {
  if (!glfwInit()) {
    Log::write(LogLevel::Error, "GLFW initialization failed");
    return;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  handle_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title, nullptr, nullptr);
  if (!handle_) Log::write(LogLevel::Error, "GLFW window creation failed");
}

Window::~Window() {
  if (handle_) glfwDestroyWindow(handle_);
  glfwTerminate();
}

bool Window::valid() const noexcept { return handle_ != nullptr; }
bool Window::shouldClose() const noexcept { return !handle_ || glfwWindowShouldClose(handle_); }
void Window::poll() noexcept { glfwPollEvents(); }
GLFWwindow* Window::native() const noexcept { return handle_; }

} // namespace btai
