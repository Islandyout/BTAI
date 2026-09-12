#include "btai/render/VulkanRenderer.hpp"
#include "btai/core/Log.hpp"
#include "btai/platform/Window.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace btai {
namespace {
constexpr std::array<const char*, 1> validationLayers{"VK_LAYER_KHRONOS_validation"};

struct QueueFamilies {
  std::uint32_t graphics = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t present = std::numeric_limits<std::uint32_t>::max();
  bool complete() const noexcept {
    return graphics != std::numeric_limits<std::uint32_t>::max() && present != std::numeric_limits<std::uint32_t>::max();
  }
};

QueueFamilies findQueues(VkPhysicalDevice device, VkSurfaceKHR surface) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
  QueueFamilies result;
  for (std::uint32_t i = 0; i < count; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) result.graphics = i;
    VkBool32 present = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present) == VK_SUCCESS && present) result.present = i;
    if (result.complete()) break;
  }
  return result;
}

bool validationAvailable() {
  std::uint32_t count = 0;
  if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) return false;
  std::vector<VkLayerProperties> layers(count);
  if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) return false;
  return std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
    return std::strcmp(layer.layerName, validationLayers[0]) == 0;
  });
}

bool hasDeviceExtension(VkPhysicalDevice device, const char* name) {
  std::uint32_t count = 0;
  if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) return false;
  std::vector<VkExtensionProperties> extensions(count);
  if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
  return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
    return std::strcmp(extension.extensionName, name) == 0;
  });
}
}

VulkanRenderer::VulkanRenderer(Window& window) : window_(window) {}
VulkanRenderer::~VulkanRenderer() { shutdown(); }

bool VulkanRenderer::initialize() {
  if (!createInstance() || !createSurface() || !selectDevice() || !createDevice() ||
      !createSwapchain() || !createRenderPass() || !createFrameResources()) {
    shutdown();
    return false;
  }
  return true;
}

bool VulkanRenderer::createInstance() {
  std::uint32_t glfwCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
  if (!glfwExtensions || glfwCount == 0) {
    Log::write(LogLevel::Error, "GLFW returned no Vulkan instance extensions");
    return false;
  }

  std::uint32_t loaderVersion = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&loaderVersion);
  const std::uint32_t apiVersion = loaderVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;
  if (loaderVersion < VK_API_VERSION_1_2) {
    Log::write(LogLevel::Error, "Vulkan 1.2 or newer is required");
    return false;
  }

  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwCount);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "BTAI";
  app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app.pEngineName = "BTAI";
  app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app.apiVersion = apiVersion;

  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  info.ppEnabledExtensionNames = extensions.data();
#if defined(BTAI_ENABLE_VALIDATION)
  if (validationAvailable()) {
    info.enabledLayerCount = static_cast<std::uint32_t>(validationLayers.size());
    info.ppEnabledLayerNames = validationLayers.data();
  }
#endif
  const VkResult result = vkCreateInstance(&info, nullptr, &instance_);
  if (result != VK_SUCCESS) {
    Log::write(LogLevel::Error, "Vulkan instance creation failed");
    return false;
  }
  return true;
}

bool VulkanRenderer::createSurface() {
  if (glfwCreateWindowSurface(instance_, window_.native(), nullptr, &surface_) != VK_SUCCESS) {
    Log::write(LogLevel::Error, "Vulkan surface creation failed");
    return false;
  }
  return true;
}

bool VulkanRenderer::selectDevice() {
  std::uint32_t count = 0;
  if (vkEnumeratePhysicalDevices(instance_, &count, nullptr) != VK_SUCCESS || count == 0) {
    Log::write(LogLevel::Error, "No Vulkan physical devices found");
    return false;
  }
  std::vector<VkPhysicalDevice> devices(count);
  if (vkEnumeratePhysicalDevices(instance_, &count, devices.data()) != VK_SUCCESS) return false;

  VkPhysicalDevice best = VK_NULL_HANDLE;
  int bestScore = -1;
  for (VkPhysicalDevice candidate : devices) {
    const QueueFamilies queues = findQueues(candidate, surface_);
    if (!queues.complete() || !hasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 :
                      properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 500 : 100;
    if (score > bestScore) {
      best = candidate;
      bestScore = score;
      graphicsFamily_ = queues.graphics;
      presentFamily_ = queues.present;
    }
  }
  physicalDevice_ = best;
  if (!physicalDevice_) Log::write(LogLevel::Error, "No suitable Vulkan device found");
  return physicalDevice_ != VK_NULL_HANDLE;
}

bool VulkanRenderer::createDevice() {
  const float priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queues;
  VkDeviceQueueCreateInfo graphics{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  graphics.queueFamilyIndex = graphicsFamily_;
  graphics.queueCount = 1;
  graphics.pQueuePriorities = &priority;
  queues.push_back(graphics);
  if (presentFamily_ != graphicsFamily_) {
    VkDeviceQueueCreateInfo present{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    present.queueFamilyIndex = presentFamily_;
    present.queueCount = 1;
    present.pQueuePriorities = &priority;
    queues.push_back(present);
  }

  const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDeviceFeatures features{};
  VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  info.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
  info.pQueueCreateInfos = queues.data();
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = extensions;
  info.pEnabledFeatures = &features;
  if (vkCreateDevice(physicalDevice_, &info, nullptr, &device_) != VK_SUCCESS) {
    Log::write(LogLevel::Error, "Vulkan logical device creation failed");
    return false;
  }
  vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
  return true;
}

bool VulkanRenderer::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps) != VK_SUCCESS) return false;

  std::uint32_t formatCount = 0;
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr) != VK_SUCCESS || formatCount == 0) return false;
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data()) != VK_SUCCESS) return false;
  auto formatIt = std::find_if(formats.begin(), formats.end(), [](const auto& f) {
    return f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  });
  const VkSurfaceFormatKHR chosen = formatIt != formats.end() ? *formatIt : formats.front();
  swapchainFormat_ = chosen.format;

  if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
    extent_ = caps.currentExtent;
  } else {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_.native(), &width, &height);
    extent_.width = static_cast<std::uint32_t>(std::max(width, 1));
    extent_.height = static_cast<std::uint32_t>(std::max(height, 1));
    extent_.width = std::clamp(extent_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent_.height = std::clamp(extent_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }

  std::uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount != 0) imageCount = std::min(imageCount, caps.maxImageCount);
  const std::array<std::uint32_t, 2> families{graphicsFamily_, presentFamily_};

  VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  info.surface = surface_;
  info.minImageCount = imageCount;
  info.imageFormat = swapchainFormat_;
  info.imageColorSpace = chosen.colorSpace;
  info.imageExtent = extent_;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  info.imageSharingMode = graphicsFamily_ == presentFamily_ ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  info.queueFamilyIndexCount = graphicsFamily_ == presentFamily_ ? 0u : 2u;
  info.pQueueFamilyIndices = graphicsFamily_ == presentFamily_ ? nullptr : families.data();
  info.preTransform = caps.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  info.clipped = VK_TRUE;
  if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) {
    Log::write(LogLevel::Error, "Vulkan swapchain creation failed");
    return false;
  }

  std::uint32_t imageCountActual = 0;
  if (vkGetSwapchainImagesKHR(device_, swapchain_, &imageCountActual, nullptr) != VK_SUCCESS || imageCountActual == 0) return false;
  swapchainImages_.resize(imageCountActual);
  if (vkGetSwapchainImagesKHR(device_, swapchain_, &imageCountActual, swapchainImages_.data()) != VK_SUCCESS) return false;

  swapchainImageViews_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
  for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = swapchainImages_[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = swapchainFormat_;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &view, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) return false;
  }
  return true;
}

bool VulkanRenderer::createRenderPass() {
  VkAttachmentDescription color{};
  color.format = swapchainFormat_;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference reference{};
  reference.attachment = 0;
  reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  pass.attachmentCount = 1;
  pass.pAttachments = &color;
  pass.subpassCount = 1;
  pass.pSubpasses = &subpass;
  pass.dependencyCount = 1;
  pass.pDependencies = &dependency;
  if (vkCreateRenderPass(device_, &pass, nullptr, &renderPass_) != VK_SUCCESS) return false;

  framebuffers_.resize(swapchainImageViews_.size(), VK_NULL_HANDLE);
  for (std::size_t i = 0; i < swapchainImageViews_.size(); ++i) {
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = renderPass_;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &swapchainImageViews_[i];
    framebuffer.width = extent_.width;
    framebuffer.height = extent_.height;
    framebuffer.layers = 1;
    if (vkCreateFramebuffer(device_, &framebuffer, nullptr, &framebuffers_[i]) != VK_SUCCESS) return false;
  }
  return true;
}

bool VulkanRenderer::createFrameResources() {
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool.queueFamilyIndex = graphicsFamily_;
  if (vkCreateCommandPool(device_, &pool, nullptr, &commandPool_) != VK_SUCCESS) return false;

  VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocation.commandPool = commandPool_;
  allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocation.commandBufferCount = static_cast<std::uint32_t>(commandBuffers_.size());
  if (vkAllocateCommandBuffers(device_, &allocation, commandBuffers_.data()) != VK_SUCCESS) return false;

  VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (std::size_t i = 0; i < MaxFramesInFlight; ++i) {
    if (vkCreateSemaphore(device_, &semaphore, nullptr, &imageAvailable_[i]) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphore, nullptr, &renderFinished_[i]) != VK_SUCCESS ||
        vkCreateFence(device_, &fence, nullptr, &inFlight_[i]) != VK_SUCCESS) return false;
  }
  imagesInFlight_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
  return true;
}

bool VulkanRenderer::recreateSwapchain() {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_.native(), &width, &height);
  while (width == 0 || height == 0) {
    if (window_.shouldClose()) return false;
    glfwWaitEvents();
    glfwGetFramebufferSize(window_.native(), &width, &height);
  }
  if (vkDeviceWaitIdle(device_) != VK_SUCCESS) return false;
  destroySwapchain();
  return createSwapchain() && createRenderPass();
}

void VulkanRenderer::draw() {
  if (!device_ || !swapchain_) return;

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_.native(), &width, &height);
  if (width == 0 || height == 0) return;
  if (extent_.width != static_cast<std::uint32_t>(width) || extent_.height != static_cast<std::uint32_t>(height)) {
    recreateSwapchain();
    return;
  }

  VkFence frameFence = inFlight_[currentFrame_];
  if (vkWaitForFences(device_, 1, &frameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return;

  std::uint32_t imageIndex = 0;
  const VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
  if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain();
    return;
  }
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) return;

  if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE &&
      vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX) != VK_SUCCESS) return;
  imagesInFlight_[imageIndex] = frameFence;

  if (vkResetFences(device_, 1, &frameFence) != VK_SUCCESS) return;
  VkCommandBuffer commandBuffer = commandBuffers_[currentFrame_];
  if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS) return;
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  if (vkBeginCommandBuffer(commandBuffer, &begin) != VK_SUCCESS) return;

  VkClearValue clear{};
  clear.color = {{0.035f, 0.055f, 0.09f, 1.0f}};
  VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  render.renderPass = renderPass_;
  render.framebuffer = framebuffers_[imageIndex];
  render.renderArea.extent = extent_;
  render.clearValueCount = 1;
  render.pClearValues = &clear;
  vkCmdBeginRenderPass(commandBuffer, &render, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdEndRenderPass(commandBuffer);
  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) return;

  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &imageAvailable_[currentFrame_];
  submit.pWaitDstStageMask = &waitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commandBuffer;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &renderFinished_[currentFrame_];
  if (vkQueueSubmit(graphicsQueue_, 1, &submit, frameFence) != VK_SUCCESS) {
    Log::write(LogLevel::Error, "Vulkan queue submission failed");
    return;
  }

  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &renderFinished_[currentFrame_];
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &imageIndex;
  const VkResult result = vkQueuePresentKHR(presentQueue_, &present);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) recreateSwapchain();
  currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
}

void VulkanRenderer::destroySwapchain() noexcept {
  for (VkFramebuffer framebuffer : framebuffers_) if (framebuffer) vkDestroyFramebuffer(device_, framebuffer, nullptr);
  framebuffers_.clear();
  if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
  renderPass_ = VK_NULL_HANDLE;
  for (VkImageView view : swapchainImageViews_) if (view) vkDestroyImageView(device_, view, nullptr);
  swapchainImageViews_.clear();
  swapchainImages_.clear();
  if (device_ && swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
}

void VulkanRenderer::destroyFrameResources() noexcept {
  if (!device_) return;
  for (std::size_t i = 0; i < MaxFramesInFlight; ++i) {
    if (imageAvailable_[i]) vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
    if (renderFinished_[i]) vkDestroySemaphore(device_, renderFinished_[i], nullptr);
    if (inFlight_[i]) vkDestroyFence(device_, inFlight_[i], nullptr);
    imageAvailable_[i] = VK_NULL_HANDLE;
    renderFinished_[i] = VK_NULL_HANDLE;
    inFlight_[i] = VK_NULL_HANDLE;
  }
  if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
  commandPool_ = VK_NULL_HANDLE;
  commandBuffers_.fill(VK_NULL_HANDLE);
  imagesInFlight_.clear();
}

void VulkanRenderer::shutdown() noexcept {
  if (device_) vkDeviceWaitIdle(device_);
  destroyFrameResources();
  destroySwapchain();
  if (device_) vkDestroyDevice(device_);
  device_ = VK_NULL_HANDLE;
  if (surface_ && instance_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  surface_ = VK_NULL_HANDLE;
  if (instance_) vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  graphicsQueue_ = VK_NULL_HANDLE;
  presentQueue_ = VK_NULL_HANDLE;
}

} // namespace btai
