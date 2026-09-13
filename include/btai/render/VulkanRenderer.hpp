#pragma once

#include "btai/assets/AssetManager.hpp"
#include "btai/render/Camera.hpp"
#include "btai/render/RenderSnapshot.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace btai {
class Window;
namespace editor { class Editor; }

class VulkanRenderer final {
public:
  struct Vertex { float position[3]; float color[3]; };
  struct ImGuiBackendContext {
    std::uint32_t apiVersion=VK_API_VERSION_1_2;
    VkInstance instance=VK_NULL_HANDLE; VkPhysicalDevice physicalDevice=VK_NULL_HANDLE; VkDevice device=VK_NULL_HANDLE;
    std::uint32_t queueFamily=0; VkQueue queue=VK_NULL_HANDLE; VkRenderPass renderPass=VK_NULL_HANDLE;
    std::uint32_t imageCount=2;
  };

  explicit VulkanRenderer(Window& window);
  ~VulkanRenderer();
  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(const VulkanRenderer&) = delete;

  bool initialize();
  void setModel(std::shared_ptr<const assets::Model> model);
  void draw();
  void draw(const render::RenderSnapshot& snapshot);
  void draw(const render::RenderSnapshot& snapshot,const render::Camera& camera);
  void setEditor(editor::Editor* editor) noexcept;
  ImGuiBackendContext imguiContext() const noexcept;
  void shutdown() noexcept;

private:
  struct alignas(16) PushConstants { float viewProjection[16]; };
  struct alignas(16) InstanceData { float model[16]; float color[4]; };
  bool createInstance(); bool createSurface(); bool selectDevice(); bool createDevice(); bool createSwapchain(); bool createRenderPass(); bool createFrameResources(); bool createDepthResources(); bool createGraphicsPipeline(); bool createGeometryBuffers(); bool createInstanceBuffers(); bool createDescriptorResources(); bool recreateSwapchain();
  bool createBuffer(VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags properties,VkBuffer& buffer,VkDeviceMemory& memory);
  bool copyBuffer(VkBuffer source,VkBuffer destination,VkDeviceSize size); bool uploadBuffer(const void* data,VkDeviceSize size,VkBufferUsageFlags usage,VkBuffer& buffer,VkDeviceMemory& memory); bool uploadModel(const assets::Model& model);
  bool createImage(VkFormat format,VkImageUsageFlags usage,VkImage& image,VkDeviceMemory& memory); VkShaderModule createShaderModule(const std::vector<std::uint32_t>& code) const;
  bool recordCommandBuffer(VkCommandBuffer commandBuffer,std::uint32_t imageIndex,const render::RenderSnapshot* snapshot,const render::Camera* camera);
  void drawInternal(const render::RenderSnapshot& snapshot,const render::Camera* camera);
  void destroySwapchain() noexcept; void destroyFrameResources() noexcept; void destroyDepthResources() noexcept; void destroyGraphicsPipeline() noexcept; void destroyGeometryBuffers() noexcept; void destroyInstanceBuffers() noexcept; void destroyDescriptorResources() noexcept;
  static constexpr std::size_t MaxFramesInFlight=2; static constexpr std::size_t MaxInstances=10000;
  Window& window_; VkInstance instance_=VK_NULL_HANDLE; VkSurfaceKHR surface_=VK_NULL_HANDLE; VkPhysicalDevice physicalDevice_=VK_NULL_HANDLE; VkDevice device_=VK_NULL_HANDLE; VkQueue graphicsQueue_=VK_NULL_HANDLE; VkQueue presentQueue_=VK_NULL_HANDLE; std::uint32_t graphicsFamily_=0,presentFamily_=0; VkPhysicalDeviceMemoryProperties memoryProperties_{};
  VkSwapchainKHR swapchain_=VK_NULL_HANDLE; std::vector<VkImage> swapchainImages_; std::vector<VkImageView> swapchainImageViews_; std::vector<VkFramebuffer> framebuffers_; VkFormat swapchainFormat_=VK_FORMAT_UNDEFINED; VkExtent2D extent_{}; VkRenderPass renderPass_=VK_NULL_HANDLE;
  VkImage depthImage_=VK_NULL_HANDLE; VkDeviceMemory depthMemory_=VK_NULL_HANDLE; VkImageView depthView_=VK_NULL_HANDLE; VkFormat depthFormat_=VK_FORMAT_UNDEFINED; VkDescriptorSetLayout descriptorSetLayout_=VK_NULL_HANDLE; VkDescriptorPool descriptorPool_=VK_NULL_HANDLE; std::array<VkDescriptorSet,MaxFramesInFlight> descriptorSets_{};
  std::array<VkBuffer,MaxFramesInFlight> instanceBuffers_{}; std::array<VkDeviceMemory,MaxFramesInFlight> instanceMemories_{}; std::array<void*,MaxFramesInFlight> instanceMapped_{}; VkPipelineLayout pipelineLayout_=VK_NULL_HANDLE; VkPipeline graphicsPipeline_=VK_NULL_HANDLE; VkBuffer vertexBuffer_=VK_NULL_HANDLE; VkDeviceMemory vertexMemory_=VK_NULL_HANDLE; VkBuffer indexBuffer_=VK_NULL_HANDLE; VkDeviceMemory indexMemory_=VK_NULL_HANDLE; std::uint32_t indexCount_=0;
  std::shared_ptr<const assets::Model> model_; VkCommandPool commandPool_=VK_NULL_HANDLE; std::array<VkCommandBuffer,MaxFramesInFlight> commandBuffers_{}; std::array<VkSemaphore,MaxFramesInFlight> imageAvailable_{}; std::array<VkSemaphore,MaxFramesInFlight> renderFinished_{}; std::array<VkFence,MaxFramesInFlight> inFlight_{}; std::vector<VkFence> imagesInFlight_; std::size_t currentFrame_=0; editor::Editor* editor_=nullptr;
};
} // namespace btai
