#include "btai/render/VulkanRenderer.hpp"
#include "btai/core/Log.hpp"
#include "btai/platform/Window.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
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

VkFormat findDepthFormat(VkPhysicalDevice device) {
  constexpr std::array<VkFormat, 2> candidates{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT};
  for (VkFormat format : candidates) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
  }
  return VK_FORMAT_UNDEFINED;
}

struct VertexData {
  float position[3];
  float color[3];
};

struct Vec3f {
  float x;
  float y;
  float z;
};

float dot(Vec3f a, Vec3f b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3f cross(Vec3f a, Vec3f b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3f normalize(Vec3f v) noexcept {
  const float length = std::sqrt(std::max(dot(v, v), 1.0e-12f));
  return {v.x / length, v.y / length, v.z / length};
}

struct Mat4 {
  float m[16]{};
};

Mat4 identity() noexcept {
  Mat4 result{};
  result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
  return result;
}

Mat4 multiply(const Mat4& a, const Mat4& b) noexcept {
  Mat4 result{};
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      for (int k = 0; k < 4; ++k) result.m[column * 4 + row] += a.m[k * 4 + row] * b.m[column * 4 + k];
    }
  }
  return result;
}

Mat4 translation(float x, float y, float z) noexcept {
  Mat4 result = identity();
  result.m[12] = x;
  result.m[13] = y;
  result.m[14] = z;
  return result;
}

Mat4 scale(float x, float y, float z) noexcept {
  Mat4 result{};
  result.m[0] = x;
  result.m[5] = y;
  result.m[10] = z;
  result.m[15] = 1.0f;
  return result;
}

Mat4 rotationX(float angle) noexcept {
  Mat4 result = identity();
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  result.m[5] = c;
  result.m[9] = -s;
  result.m[6] = s;
  result.m[10] = c;
  return result;
}

Mat4 rotationY(float angle) noexcept {
  Mat4 result = identity();
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  result.m[0] = c;
  result.m[8] = s;
  result.m[2] = -s;
  result.m[10] = c;
  return result;
}

Mat4 rotationZ(float angle) noexcept {
  Mat4 result = identity();
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  result.m[0] = c;
  result.m[4] = -s;
  result.m[1] = s;
  result.m[5] = c;
  return result;
}

Mat4 lookAt(Vec3f eye, Vec3f center, Vec3f up) noexcept {
  const Vec3f forward = normalize({center.x - eye.x, center.y - eye.y, center.z - eye.z});
  const Vec3f side = normalize(cross(forward, up));
  const Vec3f actualUp = cross(side, forward);
  Mat4 result = identity();
  result.m[0] = side.x;
  result.m[4] = side.y;
  result.m[8] = side.z;
  result.m[1] = actualUp.x;
  result.m[5] = actualUp.y;
  result.m[9] = actualUp.z;
  result.m[2] = -forward.x;
  result.m[6] = -forward.y;
  result.m[10] = -forward.z;
  result.m[12] = -dot(side, eye);
  result.m[13] = -dot(actualUp, eye);
  result.m[14] = dot(forward, eye);
  return result;
}

Mat4 perspective(float fovRadians, float aspect, float nearPlane, float farPlane) noexcept {
  Mat4 result{};
  const float f = 1.0f / std::tan(fovRadians * 0.5f);
  result.m[0] = f / aspect;
  result.m[5] = -f;
  result.m[10] = farPlane / (nearPlane - farPlane);
  result.m[11] = -1.0f;
  result.m[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
  return result;
}

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return {};
  const std::streamsize size = file.tellg();
  if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) return {};
  file.seekg(0);
  std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
  if (!file.read(reinterpret_cast<char*>(code.data()), size)) return {};
  return code;
}

#ifndef BTAI_SHADER_DIR
#define BTAI_SHADER_DIR "shaders"
#endif

std::filesystem::path shaderPath(const char* name) {
  return std::filesystem::path(BTAI_SHADER_DIR) / name;
}

constexpr std::array<VertexData, 8> cubeVertices{{
    {{-0.5f, -0.5f, -0.5f}, {0.15f, 0.25f, 0.85f}},
    {{0.5f, -0.5f, -0.5f}, {0.85f, 0.2f, 0.15f}},
    {{0.5f, 0.5f, -0.5f}, {0.15f, 0.8f, 0.25f}},
    {{-0.5f, 0.5f, -0.5f}, {0.85f, 0.75f, 0.15f}},
    {{-0.5f, -0.5f, 0.5f}, {0.8f, 0.15f, 0.7f}},
    {{0.5f, -0.5f, 0.5f}, {0.15f, 0.75f, 0.8f}},
    {{0.5f, 0.5f, 0.5f}, {0.8f, 0.35f, 0.15f}},
    {{-0.5f, 0.5f, 0.5f}, {0.7f, 0.8f, 0.25f}},
}};

constexpr std::array<std::uint32_t, 36> cubeIndices{{
    0, 1, 2, 2, 3, 0,
    4, 6, 5, 6, 4, 7,
    0, 4, 5, 5, 1, 0,
    3, 2, 6, 6, 7, 3,
    0, 3, 7, 7, 4, 0,
    1, 5, 6, 6, 2, 1,
}};
}

VulkanRenderer::VulkanRenderer(Window& window) : window_(window) {}
VulkanRenderer::~VulkanRenderer() { shutdown(); }

bool VulkanRenderer::initialize() {
  if (!createInstance() || !createSurface() || !selectDevice() || !createDevice() ||
      !createSwapchain() || !createFrameResources() || !createDepthResources() ||
      !createRenderPass() || !createGraphicsPipeline() || !createGeometryBuffers()) {
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
  if (loaderVersion < VK_API_VERSION_1_2) {
    Log::write(LogLevel::Error, "Vulkan 1.2 or newer is required");
    return false;
  }
  const std::uint32_t apiVersion = loaderVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;

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
  if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
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
    const VkFormat depth = findDepthFormat(candidate);
    if (depth == VK_FORMAT_UNDEFINED) continue;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 :
                      properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 500 : 100;
    if (score > bestScore) {
      best = candidate;
      bestScore = score;
      graphicsFamily_ = queues.graphics;
      presentFamily_ = queues.present;
      depthFormat_ = depth;
    }
  }
  physicalDevice_ = best;
  if (!physicalDevice_) Log::write(LogLevel::Error, "No suitable Vulkan device found");
  if (physicalDevice_) vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
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
  imagesInFlight_.assign(swapchainImages_.size(), VK_NULL_HANDLE);

  swapchainImageViews_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
  for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = swapchainImages_[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = swapchainFormat_;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &view, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) return false;
  }
  return true;
}

bool VulkanRenderer::createRenderPass() {
  std::array<VkAttachmentDescription, 2> attachments{};
  attachments[0].format = swapchainFormat_;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  attachments[1].format = depthFormat_;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color;
  subpass.pDepthStencilAttachment = &depth;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  pass.attachmentCount = static_cast<std::uint32_t>(attachments.size());
  pass.pAttachments = attachments.data();
  pass.subpassCount = 1;
  pass.pSubpasses = &subpass;
  pass.dependencyCount = 1;
  pass.pDependencies = &dependency;
  if (vkCreateRenderPass(device_, &pass, nullptr, &renderPass_) != VK_SUCCESS) return false;

  framebuffers_.resize(swapchainImageViews_.size(), VK_NULL_HANDLE);
  for (std::size_t i = 0; i < swapchainImageViews_.size(); ++i) {
    const std::array<VkImageView, 2> views{swapchainImageViews_[i], depthView_};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = renderPass_;
    framebuffer.attachmentCount = static_cast<std::uint32_t>(views.size());
    framebuffer.pAttachments = views.data();
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
  return true;
}

bool VulkanRenderer::createDepthResources() {
  if (depthFormat_ == VK_FORMAT_UNDEFINED) return false;
  VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image.imageType = VK_IMAGE_TYPE_2D;
  image.format = depthFormat_;
  image.extent = {extent_.width, extent_.height, 1};
  image.mipLevels = 1;
  image.arrayLayers = 1;
  image.samples = VK_SAMPLE_COUNT_1_BIT;
  image.tiling = VK_IMAGE_TILING_OPTIMAL;
  image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device_, &image, nullptr, &depthImage_) != VK_SUCCESS) return false;

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, depthImage_, &requirements);
  std::uint32_t memoryType = std::numeric_limits<std::uint32_t>::max();
  for (std::uint32_t i = 0; i < memoryProperties_.memoryTypeCount; ++i) {
    if ((requirements.memoryTypeBits & (1u << i)) &&
        (memoryProperties_.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
      memoryType = i;
      break;
    }
  }
  if (memoryType == std::numeric_limits<std::uint32_t>::max()) return false;

  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = memoryType;
  if (vkAllocateMemory(device_, &allocation, nullptr, &depthMemory_) != VK_SUCCESS) return false;
  if (vkBindImageMemory(device_, depthImage_, depthMemory_, 0) != VK_SUCCESS) {
    destroyDepthResources();
    return false;
  }

  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view.image = depthImage_;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = depthFormat_;
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  view.subresourceRange.levelCount = 1;
  view.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device_, &view, nullptr, &depthView_) != VK_SUCCESS) {
    destroyDepthResources();
    return false;
  }
  return true;
}

bool VulkanRenderer::createGraphicsPipeline() {
  const auto vertexCode = readSpirv(shaderPath("basic.vert.spv"));
  const auto fragmentCode = readSpirv(shaderPath("basic.frag.spv"));
  if (vertexCode.empty() || fragmentCode.empty()) {
    Log::write(LogLevel::Error, "BTAI shader binaries could not be loaded");
    return false;
  }
  const VkShaderModule vertexShader = createShaderModule(vertexCode);
  const VkShaderModule fragmentShader = createShaderModule(fragmentCode);
  if (!vertexShader || !fragmentShader) {
    if (vertexShader) vkDestroyShaderModule(device_, vertexShader, nullptr);
    if (fragmentShader) vkDestroyShaderModule(device_, fragmentShader, nullptr);
    return false;
  }

  VkPipelineShaderStageCreateInfo vertexStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertexStage.module = vertexShader;
  vertexStage.pName = "main";
  VkPipelineShaderStageCreateInfo fragmentStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragmentStage.module = fragmentShader;
  fragmentStage.pName = "main";
  const std::array<VkPipelineShaderStageCreateInfo, 2> stages{vertexStage, fragmentStage};

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  std::array<VkVertexInputAttributeDescription, 2> attributes{};
  attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, position))};
  attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, color))};
  VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
  vertexInput.pVertexAttributeDescriptions = attributes.data();

  VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
  dynamic.pDynamicStates = dynamicStates.data();

  VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttachment;

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push.size = sizeof(PushConstants);
  VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.pushConstantRangeCount = 1;
  layout.pPushConstantRanges = &push;
  if (vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_) != VK_SUCCESS) {
    vkDestroyShaderModule(device_, fragmentShader, nullptr);
    vkDestroyShaderModule(device_, vertexShader, nullptr);
    return false;
  }

  VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeline.stageCount = static_cast<std::uint32_t>(stages.size());
  pipeline.pStages = stages.data();
  pipeline.pVertexInputState = &vertexInput;
  pipeline.pInputAssemblyState = &assembly;
  pipeline.pViewportState = &viewport;
  pipeline.pRasterizationState = &rasterization;
  pipeline.pMultisampleState = &multisample;
  pipeline.pDepthStencilState = &depth;
  pipeline.pColorBlendState = &blend;
  pipeline.pDynamicState = &dynamic;
  pipeline.layout = pipelineLayout_;
  pipeline.renderPass = renderPass_;
  pipeline.subpass = 0;

  const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &graphicsPipeline_);
  vkDestroyShaderModule(device_, fragmentShader, nullptr);
  vkDestroyShaderModule(device_, vertexShader, nullptr);
  if (result != VK_SUCCESS) {
    destroyGraphicsPipeline();
    Log::write(LogLevel::Error, "Vulkan graphics pipeline creation failed");
    return false;
  }
  return true;
}

bool VulkanRenderer::createGeometryBuffers() {
  const VkDeviceSize vertexSize = sizeof(cubeVertices);
  const VkDeviceSize indexSize = sizeof(cubeIndices);
  VkBuffer vertexStaging = VK_NULL_HANDLE;
  VkDeviceMemory vertexStagingMemory = VK_NULL_HANDLE;
  VkBuffer indexStaging = VK_NULL_HANDLE;
  VkDeviceMemory indexStagingMemory = VK_NULL_HANDLE;
  auto cleanupStaging = [&]() {
    if (vertexStaging) vkDestroyBuffer(device_, vertexStaging, nullptr);
    if (vertexStagingMemory) vkFreeMemory(device_, vertexStagingMemory, nullptr);
    if (indexStaging) vkDestroyBuffer(device_, indexStaging, nullptr);
    if (indexStagingMemory) vkFreeMemory(device_, indexStagingMemory, nullptr);
    vertexStaging = VK_NULL_HANDLE;
    vertexStagingMemory = VK_NULL_HANDLE;
    indexStaging = VK_NULL_HANDLE;
    indexStagingMemory = VK_NULL_HANDLE;
  };

  if (!createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    vertexStaging, vertexStagingMemory) ||
      !createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    indexStaging, indexStagingMemory)) {
    cleanupStaging();
    return false;
  }

  void* mapped = nullptr;
  if (vkMapMemory(device_, vertexStagingMemory, 0, vertexSize, 0, &mapped) != VK_SUCCESS) {
    cleanupStaging();
    return false;
  }
  std::memcpy(mapped, cubeVertices.data(), static_cast<std::size_t>(vertexSize));
  vkUnmapMemory(device_, vertexStagingMemory);
  if (vkMapMemory(device_, indexStagingMemory, 0, indexSize, 0, &mapped) != VK_SUCCESS) {
    cleanupStaging();
    return false;
  }
  std::memcpy(mapped, cubeIndices.data(), static_cast<std::size_t>(indexSize));
  vkUnmapMemory(device_, indexStagingMemory);

  const bool vertexCreated = createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer_, vertexMemory_);
  const bool indexCreated = vertexCreated && createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer_, indexMemory_);
  const bool copied = indexCreated && copyBuffer(vertexStaging, vertexBuffer_, vertexSize) && copyBuffer(indexStaging, indexBuffer_, indexSize);
  cleanupStaging();

  if (!copied) {
    destroyGeometryBuffers();
    return false;
  }
  indexCount_ = static_cast<std::uint32_t>(cubeIndices.size());
  return true;
}

bool VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                  VkBuffer& buffer, VkDeviceMemory& memory) {
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device_, &info, nullptr, &buffer) != VK_SUCCESS) return false;

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, buffer, &requirements);
  std::uint32_t memoryType = std::numeric_limits<std::uint32_t>::max();
  for (std::uint32_t i = 0; i < memoryProperties_.memoryTypeCount; ++i) {
    if ((requirements.memoryTypeBits & (1u << i)) &&
        (memoryProperties_.memoryTypes[i].propertyFlags & properties) == properties) {
      memoryType = i;
      break;
    }
  }
  if (memoryType == std::numeric_limits<std::uint32_t>::max()) {
    vkDestroyBuffer(device_, buffer, nullptr);
    buffer = VK_NULL_HANDLE;
    return false;
  }

  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = memoryType;
  if (vkAllocateMemory(device_, &allocation, nullptr, &memory) != VK_SUCCESS) {
    vkDestroyBuffer(device_, buffer, nullptr);
    buffer = VK_NULL_HANDLE;
    return false;
  }
  if (vkBindBufferMemory(device_, buffer, memory, 0) != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyBuffer(device_, buffer, nullptr);
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

bool VulkanRenderer::copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) {
  VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocation.commandPool = commandPool_;
  allocation.commandBufferCount = 1;
  VkCommandBuffer command = VK_NULL_HANDLE;
  if (vkAllocateCommandBuffers(device_, &allocation, &command) != VK_SUCCESS) return false;

  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  bool success = vkBeginCommandBuffer(command, &begin) == VK_SUCCESS;
  if (success) {
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(command, source, destination, 1, &region);
    success = vkEndCommandBuffer(command) == VK_SUCCESS;
  }
  if (success) {
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    success = vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
    if (success) success = vkQueueWaitIdle(graphicsQueue_) == VK_SUCCESS;
  }
  vkFreeCommandBuffers(device_, commandPool_, 1, &command);
  return success;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<std::uint32_t>& code) const {
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = code.size() * sizeof(std::uint32_t);
  info.pCode = code.data();
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
  return module;
}

bool VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, std::uint32_t imageIndex,
                                         const render::RenderSnapshot* snapshot) {
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  if (vkBeginCommandBuffer(commandBuffer, &begin) != VK_SUCCESS) return false;

  std::array<VkClearValue, 2> clears{};
  clears[0].color = {{0.025f, 0.035f, 0.06f, 1.0f}};
  clears[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass = renderPass_;
  pass.framebuffer = framebuffers_[imageIndex];
  pass.renderArea.extent = extent_;
  pass.clearValueCount = static_cast<std::uint32_t>(clears.size());
  pass.pClearValues = clears.data();
  vkCmdBeginRenderPass(commandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
  const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
  const VkRect2D scissor{{0, 0}, extent_};
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  const VkBuffer vertexBuffers[] = {vertexBuffer_};
  const VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
  vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

  const float aspect = static_cast<float>(extent_.width) / std::max(static_cast<float>(extent_.height), 1.0f);
  const Mat4 projection = perspective(70.0f * 3.14159265358979323846f / 180.0f, aspect, 0.05f, 20000.0f);
  const Mat4 view = lookAt({8.0f, 6.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  const Mat4 viewProjection = multiply(projection, view);

  if (snapshot) {
    for (const render::RenderInstance& instance : snapshot->instances) {
      const Mat4 model = multiply(
          multiply(multiply(translation(instance.position.x, instance.position.y, instance.position.z),
                            rotationZ(instance.rotation.z)),
                   multiply(rotationY(instance.rotation.y), rotationX(instance.rotation.x))),
          scale(instance.scale.x, instance.scale.y, instance.scale.z));
      PushConstants constants{};
      std::memcpy(constants.viewProjection, viewProjection.m, sizeof(viewProjection.m));
      std::memcpy(constants.model, model.m, sizeof(model.m));
      vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(constants), &constants);
      vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
    }
  }

  vkCmdEndRenderPass(commandBuffer);
  return vkEndCommandBuffer(commandBuffer) == VK_SUCCESS;
}

bool VulkanRenderer::recreateSwapchain() {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_.native(), &width, &height);
  while (width == 0 || height == 0) {
    glfwWaitEvents();
    glfwGetFramebufferSize(window_.native(), &width, &height);
  }
  if (vkDeviceWaitIdle(device_) != VK_SUCCESS) return false;

  destroyGraphicsPipeline();
  destroySwapchain();
  destroyDepthResources();
  if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
  renderPass_ = VK_NULL_HANDLE;
  return createSwapchain() && createDepthResources() && createRenderPass() && createGraphicsPipeline();
}

void VulkanRenderer::destroySwapchain() noexcept {
  for (VkFramebuffer framebuffer : framebuffers_) if (framebuffer) vkDestroyFramebuffer(device_, framebuffer, nullptr);
  framebuffers_.clear();
  for (VkImageView view : swapchainImageViews_) if (view) vkDestroyImageView(device_, view, nullptr);
  swapchainImageViews_.clear();
  if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
  swapchainImages_.clear();
  imagesInFlight_.clear();
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
}

void VulkanRenderer::destroyDepthResources() noexcept {
  if (!device_) return;
  if (depthView_) vkDestroyImageView(device_, depthView_, nullptr);
  if (depthImage_) vkDestroyImage(device_, depthImage_, nullptr);
  if (depthMemory_) vkFreeMemory(device_, depthMemory_, nullptr);
  depthView_ = VK_NULL_HANDLE;
  depthImage_ = VK_NULL_HANDLE;
  depthMemory_ = VK_NULL_HANDLE;
}

void VulkanRenderer::destroyGraphicsPipeline() noexcept {
  if (!device_) return;
  if (graphicsPipeline_) vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
  if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
  graphicsPipeline_ = VK_NULL_HANDLE;
  pipelineLayout_ = VK_NULL_HANDLE;
}

void VulkanRenderer::destroyGeometryBuffers() noexcept {
  if (!device_) return;
  if (indexBuffer_) vkDestroyBuffer(device_, indexBuffer_, nullptr);
  if (indexMemory_) vkFreeMemory(device_, indexMemory_, nullptr);
  if (vertexBuffer_) vkDestroyBuffer(device_, vertexBuffer_, nullptr);
  if (vertexMemory_) vkFreeMemory(device_, vertexMemory_, nullptr);
  indexBuffer_ = VK_NULL_HANDLE;
  indexMemory_ = VK_NULL_HANDLE;
  vertexBuffer_ = VK_NULL_HANDLE;
  vertexMemory_ = VK_NULL_HANDLE;
  indexCount_ = 0;
}

void VulkanRenderer::draw() {
  render::RenderSnapshot empty;
  draw(empty);
}

void VulkanRenderer::draw(const render::RenderSnapshot& snapshot) {
  if (!device_ || !swapchain_) return;
  if (vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX) != VK_SUCCESS) return;

  std::uint32_t imageIndex = 0;
  VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain();
    return;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;

  if (imagesInFlight_[imageIndex]) {
    if (vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX) != VK_SUCCESS) return;
  }
  imagesInFlight_[imageIndex] = inFlight_[currentFrame_];
  if (vkResetFences(device_, 1, &inFlight_[currentFrame_]) != VK_SUCCESS) return;
  if (vkResetCommandBuffer(commandBuffers_[currentFrame_], 0) != VK_SUCCESS) return;
  if (!recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, &snapshot)) return;

  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &imageAvailable_[currentFrame_];
  submit.pWaitDstStageMask = &waitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commandBuffers_[currentFrame_];
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &renderFinished_[currentFrame_];
  if (vkQueueSubmit(graphicsQueue_, 1, &submit, inFlight_[currentFrame_]) != VK_SUCCESS) return;

  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &renderFinished_[currentFrame_];
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &imageIndex;
  result = vkQueuePresentKHR(presentQueue_, &present);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) recreateSwapchain();
  else if (result != VK_SUCCESS) Log::write(LogLevel::Error, "Vulkan queue present failed");
  currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
}

void VulkanRenderer::shutdown() noexcept {
  if (device_) vkDeviceWaitIdle(device_);
  destroyGeometryBuffers();
  destroyGraphicsPipeline();
  destroySwapchain();
  destroyDepthResources();
  if (renderPass_ && device_) vkDestroyRenderPass(device_, renderPass_, nullptr);
  renderPass_ = VK_NULL_HANDLE;
  destroyFrameResources();
  if (device_) vkDestroyDevice(device_, nullptr);
  device_ = VK_NULL_HANDLE;
  if (surface_ && instance_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  surface_ = VK_NULL_HANDLE;
  if (instance_) vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  graphicsQueue_ = VK_NULL_HANDLE;
  presentQueue_ = VK_NULL_HANDLE;
  currentFrame_ = 0;
}
