#include "btai/input/Input.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>

namespace btai::input {

bool Input::initialize(GLFWwindow* window) noexcept {
  window_ = window;
  if (!window_) return false;
  glfwGetCursorPos(window_,&lastMouseX_,&lastMouseY_);
  mouseX_=static_cast<float>(lastMouseX_);
  mouseY_=static_cast<float>(lastMouseY_);
  return true;
}

void Input::poll() noexcept {
  if (!window_) return;
  previous_=current_;
  for (int i=0;i<KeyCount;++i) current_[static_cast<std::size_t>(i)]=glfwGetKey(window_,i)==GLFW_PRESS ? 1U : 0U;
  for (int i=0;i<MouseCount;++i) mouse_[static_cast<std::size_t>(i)]=glfwGetMouseButton(window_,i)==GLFW_PRESS ? 1U : 0U;
  double x=0.0,y=0.0;
  glfwGetCursorPos(window_,&x,&y);
  if (firstMouse_) { lastMouseX_=x; lastMouseY_=y; firstMouse_=false; }
  mouseDx_=static_cast<float>(x-lastMouseX_);
  mouseDy_=static_cast<float>(y-lastMouseY_);
  lastMouseX_=x; lastMouseY_=y;
  mouseX_=static_cast<float>(x); mouseY_=static_cast<float>(y);
  quit_=glfwWindowShouldClose(window_)!=0;
}

bool Input::key(int keyCode) const noexcept { return keyCode>=0 && keyCode<KeyCount && current_[static_cast<std::size_t>(keyCode)]!=0; }
bool Input::pressed(int keyCode) const noexcept { return key(keyCode) && !(keyCode>=0 && keyCode<KeyCount && previous_[static_cast<std::size_t>(keyCode)]); }
bool Input::released(int keyCode) const noexcept { return keyCode>=0 && keyCode<KeyCount && !current_[static_cast<std::size_t>(keyCode)] && previous_[static_cast<std::size_t>(keyCode)]!=0; }
bool Input::mouseButton(int button) const noexcept { return button>=0 && button<MouseCount && mouse_[static_cast<std::size_t>(button)]!=0; }

} // namespace btai::input
