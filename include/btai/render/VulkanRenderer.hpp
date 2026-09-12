#pragma once

#include "btai/render/RenderSnapshot.hpp"
#include <array>
#include <cstdint>
#include <vector>
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
  void draw(const render::RenderSnapshot& snapshot);
  void shutdown() noexcept;

private:
  bool createInstance();
  bool createSurface();
  bool selectDevice();
  bool createDevice();
  bool createSwapchain();
  bool createRenderPass();
  bool createFrameResources();
  bool recreateSwapchain();
  void destroySwapchain() noexcept;
  void destroyFrameResources() noexcept;

  static constexpr std::size_t MaxFramesInFlight = 2;

  Window& window_;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  VkQueue presentQueue_ = VK_NULL_HANDLE;
  std::uint32_t graphicsFamily_ = 0;
  std::uint32_t presentFamily_ = 0;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  std::vector<VkImage> swapchainImages_;
  std::vector<VkImageView> swapchainImageViews_;
  std::vector<VkFramebuffer> framebuffers_;
  VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::array<VkCommandBuffer, MaxFramesInFlight> commandBuffers_{};
  std::array<VkSemaphore, MaxFramesInFlight> imageAvailable_{};
  std::array<VkSemaphore, MaxFramesInFlight> renderFinished_{};
  std::array<VkFence, MaxFramesInFlight> inFlight_{};
  std::vector<VkFence> imagesInFlight_;
  std::size_t currentFrame_ = 0;
};
} // namespace btai
