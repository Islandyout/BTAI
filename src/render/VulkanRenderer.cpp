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
  bool complete() const noexcept { return graphics != std::numeric_limits<std::uint32_t>::max() && present != std::numeric_limits<std::uint32_t>::max(); }
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
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present);
    if (present) result.present = i;
    if (result.complete()) break;
  }
  return result;
}

bool validationAvailable() {
  std::uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());
  return std::any_of(layers.begin(), layers.end(), [](const auto& l) { return std::strcmp(l.layerName, validationLayers[0]) == 0; });
}
}

VulkanRenderer::VulkanRenderer(Window& window) : window_(window) {}
VulkanRenderer::~VulkanRenderer() { shutdown(); }

bool VulkanRenderer::initialize() {
  if (!createInstance() || !createSurface() || !selectDevice() || !createDevice() || !createSwapchain()) {
    shutdown();
    return false;
  }
  return true;
}

bool VulkanRenderer::createInstance() {
  std::uint32_t glfwCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
  if (!glfwExtensions) return false;

  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwCount);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "BTAI";
  app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app.pEngineName = "BTAI";
  app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app.apiVersion = VK_API_VERSION_1_2;

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
  if (vkEnumeratePhysicalDevices(instance_, &count, nullptr) != VK_SUCCESS || count == 0) return false;
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());
  for (VkPhysicalDevice candidate : devices) {
    QueueFamilies queues = findQueues(candidate, surface_);
    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data());
    const bool swapchain = std::any_of(extensions.begin(), extensions.end(), [](const auto& e) { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; });
    if (queues.complete() && swapchain) {
      physicalDevice_ = candidate;
      return true;
    }
  }
  return false;
}

bool VulkanRenderer::createDevice() {
  QueueFamilies queues = findQueues(physicalDevice_, surface_);
  const float priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queueInfos;
  VkDeviceQueueCreateInfo graphics{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  graphics.queueFamilyIndex = queues.graphics;
  graphics.queueCount = 1;
  graphics.pQueuePriorities = &priority;
  queueInfos.push_back(graphics);
  if (queues.present != queues.graphics) {
    VkDeviceQueueCreateInfo present{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    present.queueFamilyIndex = queues.present;
    present.queueCount = 1;
    present.pQueuePriorities = &priority;
    queueInfos.push_back(present);
  }
  const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDeviceFeatures features{};
  VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  info.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
  info.pQueueCreateInfos = queueInfos.data();
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = extensions;
  info.pEnabledFeatures = &features;
  if (vkCreateDevice(physicalDevice_, &info, nullptr, &device_) != VK_SUCCESS) return false;
  vkGetDeviceQueue(device_, queues.graphics, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, queues.present, 0, &presentQueue_);
  return true;
}

bool VulkanRenderer::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps) != VK_SUCCESS) return false;
  std::uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
  if (!formatCount) return false;
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
  const VkSurfaceFormatKHR chosen = *std::find_if(formats.begin(), formats.end(), [](const auto& f) { return f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; });
  swapchainFormat_ = chosen.format;
  if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) extent_ = caps.currentExtent;
  else extent_ = {1280, 720};
  extent_.width = std::clamp(extent_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  extent_.height = std::clamp(extent_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  std::uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
  QueueFamilies queues = findQueues(physicalDevice_, surface_);
  std::array<std::uint32_t, 2> familyIndices{queues.graphics, queues.present};
  VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  info.surface = surface_;
  info.minImageCount = imageCount;
  info.imageFormat = swapchainFormat_;
  info.imageColorSpace = chosen.colorSpace;
  info.imageExtent = extent_;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  info.imageSharingMode = queues.graphics == queues.present ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  info.queueFamilyIndexCount = queues.graphics == queues.present ? 0u : 2u;
  info.pQueueFamilyIndices = queues.graphics == queues.present ? nullptr : familyIndices.data();
  info.preTransform = caps.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  info.clipped = VK_TRUE;
  if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) return false;
  return true;
}

void VulkanRenderer::draw() {
  if (device_) vkDeviceWaitIdle(device_);
}

void VulkanRenderer::destroySwapchain() noexcept {
  if (device_ && swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
}

void VulkanRenderer::shutdown() noexcept {
  if (device_) vkDeviceWaitIdle(device_);
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
