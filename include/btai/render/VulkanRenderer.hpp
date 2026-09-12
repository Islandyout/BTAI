#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace btai {
class Window;

class VulkanRenderer final {
public:
  explicit VulkanRenderer(Window& window);
  ~VulkanRenderer();

  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(const VulkanRenderer&) = delete;

  bool initialize();
  void draw();
  void shutdown() noexcept;

private:
  bool createInstance();
  bool createSurface();
  bool selectDevice();
  bool createDevice();
  bool createSwapchain();
  void destroySwapchain() noexcept;

  Window& window_;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  VkQueue presentQueue_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
};
} // namespace btai
