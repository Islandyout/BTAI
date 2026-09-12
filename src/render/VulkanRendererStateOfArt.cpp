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
constexpr float Pi = 3.14159265358979323846f;

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
  return std::any_of(layers.begin(), layers.end(), [](const auto& layer) { return std::strcmp(layer.layerName, validationLayers[0]) == 0; });
}

bool hasExtension(VkPhysicalDevice device, const char* name) {
  std::uint32_t count = 0;
  if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) return false;
  std::vector<VkExtensionProperties> extensions(count);
  if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
  return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) { return std::strcmp(extension.extensionName, name) == 0; });
}

VkFormat findDepthFormat(VkPhysicalDevice device) {
  constexpr std::array<VkFormat, 2> candidates{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT};
  for (const VkFormat format : candidates) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
  }
  return VK_FORMAT_UNDEFINED;
}

struct Vec3 { float x; float y; float z; };
struct Mat4 { float m[16]{}; };

float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) noexcept { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 v) noexcept {
  const float length = std::sqrt(std::max(dot(v, v), 1.0e-12f));
  return {v.x / length, v.y / length, v.z / length};
}
Mat4 identity() noexcept {
  Mat4 result{};
  result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
  return result;
}
Mat4 multiply(const Mat4& a, const Mat4& b) noexcept {
  Mat4 result{};
  for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) for (int k = 0; k < 4; ++k) result.m[c * 4 + r] += a.m[k * 4 + r] * b.m[c * 4 + k];
  return result;
}
Mat4 translation(float x, float y, float z) noexcept { Mat4 r = identity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
Mat4 scale(float x, float y, float z) noexcept { Mat4 r{}; r.m[0] = x; r.m[5] = y; r.m[10] = z; r.m[15] = 1.0f; return r; }
Mat4 rotationX(float a) noexcept { Mat4 r = identity(); const float c = std::cos(a), s = std::sin(a); r.m[5] = c; r.m[9] = -s; r.m[6] = s; r.m[10] = c; return r; }
Mat4 rotationY(float a) noexcept { Mat4 r = identity(); const float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[8] = s; r.m[2] = -s; r.m[10] = c; return r; }
Mat4 rotationZ(float a) noexcept { Mat4 r = identity(); const float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[4] = -s; r.m[1] = s; r.m[5] = c; return r; }
Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) noexcept {
  const Vec3 forward = normalize({center.x - eye.x, center.y - eye.y, center.z - eye.z});
  const Vec3 side = normalize(cross(forward, up));
  const Vec3 actualUp = cross(side, forward);
  Mat4 r = identity();
  r.m[0] = side.x; r.m[4] = side.y; r.m[8] = side.z;
  r.m[1] = actualUp.x; r.m[5] = actualUp.y; r.m[9] = actualUp.z;
  r.m[2] = -forward.x; r.m[6] = -forward.y; r.m[10] = -forward.z;
  r.m[12] = -dot(side, eye); r.m[13] = -dot(actualUp, eye); r.m[14] = dot(forward, eye);
  return r;
}
Mat4 perspective(float fov, float aspect, float nearPlane, float farPlane) noexcept {
  Mat4 r{}; const float f = 1.0f / std::tan(fov * 0.5f);
  r.m[0] = f / aspect; r.m[5] = -f; r.m[10] = farPlane / (nearPlane - farPlane); r.m[11] = -1.0f; r.m[14] = farPlane * nearPlane / (nearPlane - farPlane);
  return r;
}

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return {};
  const std::streamsize size = file.tellg();
  if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) return {};
  file.seekg(0);
  std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
  return file.read(reinterpret_cast<char*>(code.data()), size) ? code : std::vector<std::uint32_t>{};
}
#ifndef BTAI_SHADER_DIR
#define BTAI_SHADER_DIR "shaders"
#endif
std::filesystem::path shaderPath(const char* name) { return std::filesystem::path(BTAI_SHADER_DIR) / name; }

constexpr std::array<VulkanRenderer::Vertex, 8> cubeVertices{{
    {{-0.5f,-0.5f,-0.5f},{0.25f,0.45f,0.95f}}, {{0.5f,-0.5f,-0.5f},{0.95f,0.25f,0.2f}},
    {{0.5f,0.5f,-0.5f},{0.25f,0.9f,0.35f}}, {{-0.5f,0.5f,-0.5f},{0.95f,0.8f,0.2f}},
    {{-0.5f,-0.5f,0.5f},{0.85f,0.25f,0.8f}}, {{0.5f,-0.5f,0.5f},{0.2f,0.85f,0.9f}},
    {{0.5f,0.5f,0.5f},{0.95f,0.45f,0.2f}}, {{-0.5f,0.5f,0.5f},{0.7f,0.9f,0.25f}}}};
constexpr std::array<std::uint32_t, 36> cubeIndices{{0,1,2,2,3,0,4,6,5,6,4,7,0,4,5,5,1,0,3,2,6,6,7,3,0,3,7,7,4,0,1,5,6,6,2,1}};
} // namespace

VulkanRenderer::VulkanRenderer(Window& window) : window_(window) {}
VulkanRenderer::~VulkanRenderer() { shutdown(); }

bool VulkanRenderer::initialize() {
  if (!createInstance() || !createSurface() || !selectDevice() || !createDevice() || !createSwapchain() ||
      !createFrameResources() || !createDepthResources() || !createRenderPass() || !createGeometryBuffers() ||
      !createInstanceBuffers() || !createDescriptorResources() || !createGraphicsPipeline()) {
    shutdown();
    return false;
  }
  return true;
}

bool VulkanRenderer::createInstance() {
  std::uint32_t glfwCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
  if (!glfwExtensions || glfwCount == 0) return false;
  std::uint32_t loaderVersion = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&loaderVersion);
  if (loaderVersion < VK_API_VERSION_1_2) { Log::write(LogLevel::Error, "Vulkan 1.2 or newer is required"); return false; }
  const std::uint32_t apiVersion = loaderVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;
  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwCount);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "BTAI"; app.applicationVersion = VK_MAKE_VERSION(0,1,0); app.pEngineName = "BTAI"; app.engineVersion = VK_MAKE_VERSION(0,1,0); app.apiVersion = apiVersion;
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app; info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()); info.ppEnabledExtensionNames = extensions.data();
#if defined(BTAI_ENABLE_VALIDATION)
  if (validationAvailable()) { info.enabledLayerCount = static_cast<std::uint32_t>(validationLayers.size()); info.ppEnabledLayerNames = validationLayers.data(); }
#endif
  if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) return false;
  return true;
}

bool VulkanRenderer::createSurface() { return glfwCreateWindowSurface(instance_, window_.native(), nullptr, &surface_) == VK_SUCCESS; }

bool VulkanRenderer::selectDevice() {
  std::uint32_t count = 0;
  if (vkEnumeratePhysicalDevices(instance_, &count, nullptr) != VK_SUCCESS || count == 0) return false;
  std::vector<VkPhysicalDevice> devices(count);
  if (vkEnumeratePhysicalDevices(instance_, &count, devices.data()) != VK_SUCCESS) return false;
  int bestScore = -1;
  for (VkPhysicalDevice candidate : devices) {
    const QueueFamilies queues = findQueues(candidate, surface_);
    if (!queues.complete() || !hasExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
    const VkFormat depth = findDepthFormat(candidate);
    if (depth == VK_FORMAT_UNDEFINED) continue;
    VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(candidate, &properties);
    const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 500 : 100;
    if (score > bestScore) { bestScore = score; physicalDevice_ = candidate; graphicsFamily_ = queues.graphics; presentFamily_ = queues.present; depthFormat_ = depth; }
  }
  if (!physicalDevice_) { Log::write(LogLevel::Error, "No suitable Vulkan device found"); return false; }
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
  return true;
}

bool VulkanRenderer::createDevice() {
  const float priority = 1.0f;
  std::array<VkDeviceQueueCreateInfo, 2> queueInfos{};
  queueInfos[0] = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfos[0].queueFamilyIndex = graphicsFamily_; queueInfos[0].queueCount = 1; queueInfos[0].pQueuePriorities = &priority;
  std::uint32_t queueCount = 1;
  if (presentFamily_ != graphicsFamily_) { queueInfos[1] = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfos[1].queueFamilyIndex = presentFamily_; queueInfos[1].queueCount = 1; queueInfos[1].pQueuePriorities = &priority; queueCount = 2; }
  const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDeviceFeatures features{};
  VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; info.queueCreateInfoCount = queueCount; info.pQueueCreateInfos = queueInfos.data(); info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = extensions; info.pEnabledFeatures = &features;
  if (vkCreateDevice(physicalDevice_, &info, nullptr, &device_) != VK_SUCCESS) return false;
  vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_); vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
  return true;
}

bool VulkanRenderer::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps{}; if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps) != VK_SUCCESS) return false;
  std::uint32_t count = 0; if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, nullptr) != VK_SUCCESS || count == 0) return false;
  std::vector<VkSurfaceFormatKHR> formats(count); if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, formats.data()) != VK_SUCCESS) return false;
  const auto formatIt = std::find_if(formats.begin(), formats.end(), [](const auto& f) { return f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; });
  const VkSurfaceFormatKHR chosen = formatIt != formats.end() ? *formatIt : formats.front(); swapchainFormat_ = chosen.format;
  if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) extent_ = caps.currentExtent;
  else { int w = 0, h = 0; glfwGetFramebufferSize(window_.native(), &w, &h); extent_.width = std::clamp(static_cast<std::uint32_t>(std::max(w,1)), caps.minImageExtent.width, caps.maxImageExtent.width); extent_.height = std::clamp(static_cast<std::uint32_t>(std::max(h,1)), caps.minImageExtent.height, caps.maxImageExtent.height); }
  std::uint32_t imageCount = caps.minImageCount + 1; if (caps.maxImageCount) imageCount = std::min(imageCount, caps.maxImageCount);
  const std::array<std::uint32_t,2> families{graphicsFamily_,presentFamily_};
  VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; info.surface = surface_; info.minImageCount = imageCount; info.imageFormat = swapchainFormat_; info.imageColorSpace = chosen.colorSpace; info.imageExtent = extent_; info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; info.imageSharingMode = graphicsFamily_ == presentFamily_ ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT; info.queueFamilyIndexCount = graphicsFamily_ == presentFamily_ ? 0u : 2u; info.pQueueFamilyIndices = graphicsFamily_ == presentFamily_ ? nullptr : families.data(); info.preTransform = caps.currentTransform; info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE;
  if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) return false;
  std::uint32_t actual = 0; if (vkGetSwapchainImagesKHR(device_, swapchain_, &actual, nullptr) != VK_SUCCESS || actual == 0) return false;
  swapchainImages_.resize(actual); if (vkGetSwapchainImagesKHR(device_, swapchain_, &actual, swapchainImages_.data()) != VK_SUCCESS) return false; imagesInFlight_.assign(actual, VK_NULL_HANDLE);
  swapchainImageViews_.resize(actual, VK_NULL_HANDLE);
  for (std::size_t i=0;i<actual;++i) { VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=swapchainImages_[i]; view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=swapchainFormat_; view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount=1; view.subresourceRange.layerCount=1; if(vkCreateImageView(device_,&view,nullptr,&swapchainImageViews_[i])!=VK_SUCCESS)return false; }
  return true;
}

bool VulkanRenderer::createRenderPass() {
  std::array<VkAttachmentDescription,2> attachments{};
  attachments[0].format=swapchainFormat_; attachments[0].samples=VK_SAMPLE_COUNT_1_BIT; attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE; attachments[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachments[0].finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  attachments[1].format=depthFormat_; attachments[1].samples=VK_SAMPLE_COUNT_1_BIT; attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; attachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}; VkSubpassDescription subpass{}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&color; subpass.pDepthStencilAttachment=&depth;
  VkSubpassDependency dependency{}; dependency.srcSubpass=VK_SUBPASS_EXTERNAL; dependency.dstSubpass=0; dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; dependency.dstStageMask=dependency.srcStageMask; dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; pass.attachmentCount=2; pass.pAttachments=attachments.data(); pass.subpassCount=1; pass.pSubpasses=&subpass; pass.dependencyCount=1; pass.pDependencies=&dependency; if(vkCreateRenderPass(device_,&pass,nullptr,&renderPass_)!=VK_SUCCESS)return false;
  framebuffers_.resize(swapchainImageViews_.size(),VK_NULL_HANDLE); for(std::size_t i=0;i<framebuffers_.size();++i){const std::array<VkImageView,2> views{swapchainImageViews_[i],depthView_};VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=renderPass_;fb.attachmentCount=2;fb.pAttachments=views.data();fb.width=extent_.width;fb.height=extent_.height;fb.layers=1;if(vkCreateFramebuffer(device_,&fb,nullptr,&framebuffers_[i])!=VK_SUCCESS)return false;} return true;
}

bool VulkanRenderer::createFrameResources() {
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex=graphicsFamily_; if(vkCreateCommandPool(device_,&pool,nullptr,&commandPool_)!=VK_SUCCESS)return false;
  VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocation.commandPool=commandPool_; allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount=static_cast<std::uint32_t>(MaxFramesInFlight); if(vkAllocateCommandBuffers(device_,&allocation,commandBuffers_.data())!=VK_SUCCESS)return false;
  VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
  for(std::size_t i=0;i<MaxFramesInFlight;++i) if(vkCreateSemaphore(device_,&semaphore,nullptr,&imageAvailable_[i])!=VK_SUCCESS||vkCreateSemaphore(device_,&semaphore,nullptr,&renderFinished_[i])!=VK_SUCCESS||vkCreateFence(device_,&fence,nullptr,&inFlight_[i])!=VK_SUCCESS)return false;
  return true;
}

bool VulkanRenderer::createImage(VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType=VK_IMAGE_TYPE_2D; info.format=format; info.extent={extent_.width,extent_.height,1}; info.mipLevels=1; info.arrayLayers=1; info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL; info.usage=usage; info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; if(vkCreateImage(device_,&info,nullptr,&image)!=VK_SUCCESS)return false;
  VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device_,image,&requirements); std::uint32_t type=std::numeric_limits<std::uint32_t>::max(); for(std::uint32_t i=0;i<memoryProperties_.memoryTypeCount;++i) if((requirements.memoryTypeBits&(1u<<i))&&(memoryProperties_.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)==VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT){type=i;break;} if(type==std::numeric_limits<std::uint32_t>::max()){vkDestroyImage(device_,image,nullptr);image=VK_NULL_HANDLE;return false;}
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=type; if(vkAllocateMemory(device_,&allocation,nullptr,&memory)!=VK_SUCCESS){vkDestroyImage(device_,image,nullptr);image=VK_NULL_HANDLE;return false;} if(vkBindImageMemory(device_,image,memory,0)!=VK_SUCCESS){vkFreeMemory(device_,memory,nullptr);vkDestroyImage(device_,image,nullptr);memory=VK_NULL_HANDLE;image=VK_NULL_HANDLE;return false;} return true;
}

bool VulkanRenderer::createDepthResources() {
  if(!createImage(depthFormat_,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,depthImage_,depthMemory_))return false;
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=depthImage_;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=depthFormat_;view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;view.subresourceRange.levelCount=1;view.subresourceRange.layerCount=1; if(vkCreateImageView(device_,&view,nullptr,&depthView_)!=VK_SUCCESS){destroyDepthResources();return false;}return true;
}

bool VulkanRenderer::createBuffer(VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags properties,VkBuffer& buffer,VkDeviceMemory& memory){
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=usage;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;if(vkCreateBuffer(device_,&info,nullptr,&buffer)!=VK_SUCCESS)return false;VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device_,buffer,&req);std::uint32_t type=std::numeric_limits<std::uint32_t>::max();for(std::uint32_t i=0;i<memoryProperties_.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(memoryProperties_.memoryTypes[i].propertyFlags&properties)==properties){type=i;break;}if(type==std::numeric_limits<std::uint32_t>::max()){vkDestroyBuffer(device_,buffer,nullptr);buffer=VK_NULL_HANDLE;return false;}VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;if(vkAllocateMemory(device_,&allocation,nullptr,&memory)!=VK_SUCCESS){vkDestroyBuffer(device_,buffer,nullptr);buffer=VK_NULL_HANDLE;return false;}if(vkBindBufferMemory(device_,buffer,memory,0)!=VK_SUCCESS){vkFreeMemory(device_,memory,nullptr);vkDestroyBuffer(device_,buffer,nullptr);buffer=VK_NULL_HANDLE;memory=VK_NULL_HANDLE;return false;}return true;
}

bool VulkanRenderer::copyBuffer(VkBuffer source,VkBuffer destination,VkDeviceSize size){VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandPool=commandPool_;allocation.commandBufferCount=1;VkCommandBuffer command=VK_NULL_HANDLE;if(vkAllocateCommandBuffers(device_,&allocation,&command)!=VK_SUCCESS)return false;VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;bool ok=vkBeginCommandBuffer(command,&begin)==VK_SUCCESS;if(ok){VkBufferCopy copy{};copy.size=size;vkCmdCopyBuffer(command,source,destination,1,&copy);ok=vkEndCommandBuffer(command)==VK_SUCCESS;}if(ok){VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;ok=vkQueueSubmit(graphicsQueue_,1,&submit,VK_NULL_HANDLE)==VK_SUCCESS;if(ok)ok=vkQueueWaitIdle(graphicsQueue_)==VK_SUCCESS;}vkFreeCommandBuffers(device_,commandPool_,1,&command);return ok;}

bool VulkanRenderer::createGeometryBuffers(){
  const VkDeviceSize vertexSize=sizeof(cubeVertices), indexSize=sizeof(cubeIndices);VkBuffer vertexStaging=VK_NULL_HANDLE,indexStaging=VK_NULL_HANDLE;VkDeviceMemory vertexMemory=VK_NULL_HANDLE,indexMemory=VK_NULL_HANDLE;auto cleanup=[&]{if(vertexStaging)vkDestroyBuffer(device_,vertexStaging,nullptr);if(vertexMemory)vkFreeMemory(device_,vertexMemory,nullptr);if(indexStaging)vkDestroyBuffer(device_,indexStaging,nullptr);if(indexMemory)vkFreeMemory(device_,indexMemory,nullptr);};
  if(!createBuffer(vertexSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,vertexStaging,vertexMemory)||!createBuffer(indexSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,indexStaging,indexMemory)){cleanup();return false;}
  void* mapped=nullptr;if(vkMapMemory(device_,vertexMemory,0,vertexSize,0,&mapped)!=VK_SUCCESS){cleanup();return false;}std::memcpy(mapped,cubeVertices.data(),static_cast<std::size_t>(vertexSize));vkUnmapMemory(device_,vertexMemory);if(vkMapMemory(device_,indexMemory,0,indexSize,0,&mapped)!=VK_SUCCESS){cleanup();return false;}std::memcpy(mapped,cubeIndices.data(),static_cast<std::size_t>(indexSize));vkUnmapMemory(device_,indexMemory);
  const bool created=createBuffer(vertexSize,VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,vertexBuffer_,vertexMemory_)&&createBuffer(indexSize,VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,indexBuffer_,indexMemory_);const bool copied=created&&copyBuffer(vertexStaging,vertexBuffer_,vertexSize)&&copyBuffer(indexStaging,indexBuffer_,indexSize);cleanup();if(!copied){destroyGeometryBuffers();return false;}indexCount_=static_cast<std::uint32_t>(cubeIndices.size());return true;
}

bool VulkanRenderer::createInstanceBuffers(){
  const VkDeviceSize size=sizeof(InstanceData)*MaxInstances;for(std::size_t i=0;i<MaxFramesInFlight;++i){if(!createBuffer(size,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,instanceBuffers_[i],instanceMemories_[i]))return false;if(vkMapMemory(device_,instanceMemories_[i],0,size,0,&instanceMapped_[i])!=VK_SUCCESS)return false;}return true;
}

bool VulkanRenderer::createDescriptorResources(){
  VkDescriptorSetLayoutBinding binding{};binding.binding=0;binding.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;binding.descriptorCount=1;binding.stageFlags=VK_SHADER_STAGE_VERTEX_BIT;VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layout.bindingCount=1;layout.pBindings=&binding;if(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&descriptorSetLayout_)!=VK_SUCCESS)return false;
  VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,static_cast<std::uint32_t>(MaxFramesInFlight)};VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=static_cast<std::uint32_t>(MaxFramesInFlight);pool.poolSizeCount=1;pool.pPoolSizes=&poolSize;if(vkCreateDescriptorPool(device_,&pool,nullptr,&descriptorPool_)!=VK_SUCCESS)return false;
  std::array<VkDescriptorSetLayout,MaxFramesInFlight> layouts{};layouts.fill(descriptorSetLayout_);VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=descriptorPool_;allocation.descriptorSetCount=static_cast<std::uint32_t>(MaxFramesInFlight);allocation.pSetLayouts=layouts.data();if(vkAllocateDescriptorSets(device_,&allocation,descriptorSets_.data())!=VK_SUCCESS)return false;
  for(std::size_t i=0;i<MaxFramesInFlight;++i){VkDescriptorBufferInfo buffer{instanceBuffers_[i],0,sizeof(InstanceData)*MaxInstances};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=descriptorSets_[i];write.dstBinding=0;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffer;vkUpdateDescriptorSets(device_,1,&write,0,nullptr);}return true;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<std::uint32_t>& code) const {VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=code.size()*sizeof(std::uint32_t);info.pCode=code.data();VkShaderModule module=VK_NULL_HANDLE;return vkCreateShaderModule(device_,&info,nullptr,&module)==VK_SUCCESS?module:VK_NULL_HANDLE;}

bool VulkanRenderer::createGraphicsPipeline(){
  const auto vertexCode=readSpirv(shaderPath("basic.vert.spv"));const auto fragmentCode=readSpirv(shaderPath("basic.frag.spv"));if(vertexCode.empty()||fragmentCode.empty())return false;const VkShaderModule vertexShader=createShaderModule(vertexCode),fragmentShader=createShaderModule(fragmentCode);if(!vertexShader||!fragmentShader){if(vertexShader)vkDestroyShaderModule(device_,vertexShader,nullptr);if(fragmentShader)vkDestroyShaderModule(device_,fragmentShader,nullptr);return false;}
  VkPipelineShaderStageCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};vs.stage=VK_SHADER_STAGE_VERTEX_BIT;vs.module=vertexShader;vs.pName="main";VkPipelineShaderStageCreateInfo fs{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};fs.stage=VK_SHADER_STAGE_FRAGMENT_BIT;fs.module=fragmentShader;fs.pName="main";const std::array<VkPipelineShaderStageCreateInfo,2> stages{vs,fs};
  VkVertexInputBindingDescription binding{};binding.stride=sizeof(Vertex);binding.inputRate=VK_VERTEX_INPUT_RATE_VERTEX;std::array<VkVertexInputAttributeDescription,2> attrs{{{0,0,VK_FORMAT_R32G32B32_SFLOAT,static_cast<std::uint32_t>(offsetof(Vertex,position))},{1,0,VK_FORMAT_R32G32B32_SFLOAT,static_cast<std::uint32_t>(offsetof(Vertex,color))}}};VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vertexInput.vertexBindingDescriptionCount=1;vertexInput.pVertexBindingDescriptions=&binding;vertexInput.vertexAttributeDescriptionCount=2;vertexInput.pVertexAttributeDescriptions=attrs.data();
  VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=1;viewport.scissorCount=1;const std::array<VkDynamicState,2> dynamics{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamics.data();
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_BACK_BIT;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1.0f;VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=VK_TRUE;depth.depthCompareOp=VK_COMPARE_OP_LESS;
  VkPipelineColorBlendAttachmentState blendAttachment{};blendAttachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&blendAttachment;
  VkPushConstantRange push{};push.stageFlags=VK_SHADER_STAGE_VERTEX_BIT;push.size=sizeof(PushConstants);VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&descriptorSetLayout_;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;if(vkCreatePipelineLayout(device_,&layout,nullptr,&pipelineLayout_)!=VK_SUCCESS){vkDestroyShaderModule(device_,fragmentShader,nullptr);vkDestroyShaderModule(device_,vertexShader,nullptr);return false;}
  VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pipeline.stageCount=2;pipeline.pStages=stages.data();pipeline.pVertexInputState=&vertexInput;pipeline.pInputAssemblyState=&assembly;pipeline.pViewportState=&viewport;pipeline.pRasterizationState=&raster;pipeline.pMultisampleState=&multisample;pipeline.pDepthStencilState=&depth;pipeline.pColorBlendState=&blend;pipeline.pDynamicState=&dynamic;pipeline.layout=pipelineLayout_;pipeline.renderPass=renderPass_;pipeline.subpass=0;const VkResult result=vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&pipeline,nullptr,&graphicsPipeline_);vkDestroyShaderModule(device_,fragmentShader,nullptr);vkDestroyShaderModule(device_,vertexShader,nullptr);return result==VK_SUCCESS;
}

bool VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer,std::uint32_t imageIndex,const render::RenderSnapshot* snapshot){
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};if(vkBeginCommandBuffer(commandBuffer,&begin)!=VK_SUCCESS)return false;std::array<VkClearValue,2> clears{};clears[0].color={{0.015f,0.025f,0.045f,1.0f}};clears[1].depthStencil={1.0f,0};VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=renderPass_;pass.framebuffer=framebuffers_[imageIndex];pass.renderArea.extent=extent_;pass.clearValueCount=2;pass.pClearValues=clears.data();vkCmdBeginRenderPass(commandBuffer,&pass,VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,graphicsPipeline_);const VkViewport viewport{0,0,static_cast<float>(extent_.width),static_cast<float>(extent_.height),0,1};const VkRect2D scissor{{0,0},extent_};vkCmdSetViewport(commandBuffer,0,1,&viewport);vkCmdSetScissor(commandBuffer,0,1,&scissor);const VkBuffer vertexBuffers[]={vertexBuffer_};const VkDeviceSize offsets[]={0};vkCmdBindVertexBuffers(commandBuffer,0,1,vertexBuffers,offsets);vkCmdBindIndexBuffer(commandBuffer,indexBuffer_,0,VK_INDEX_TYPE_UINT32);vkCmdBindDescriptorSets(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout_,0,1,&descriptorSets_[currentFrame_],0,nullptr);
  const float aspect=static_cast<float>(extent_.width)/std::max(static_cast<float>(extent_.height),1.0f);const Mat4 projection=perspective(70.0f*Pi/180.0f,aspect,0.05f,20000.0f);const Vec3 eye{8.0f,6.0f,10.0f};const Mat4 vp=multiply(projection,lookAt(eye,{0,1,0},{0,1,0}));PushConstants push{};std::memcpy(push.viewProjection,vp.m,sizeof(vp.m));vkCmdPushConstants(commandBuffer,pipelineLayout_,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
  std::size_t count=0;if(snapshot){auto* instances=static_cast<InstanceData*>(instanceMapped_[currentFrame_]);for(const auto& instance:snapshot->instances){if(count>=MaxInstances)break;const Mat4 model=multiply(multiply(multiply(translation(instance.position.x,instance.position.y,instance.position.z),rotationZ(instance.rotation.z)),multiply(rotationY(instance.rotation.y),rotationX(instance.rotation.x))),scale(instance.scale.x,instance.scale.y,instance.scale.z));std::memcpy(instances[count].model,model.m,sizeof(model.m));const float seed=static_cast<float>((instance.entity.index*2654435761u+instance.material*2246822519u)&0xffffu)/65535.0f;instances[count].color[0]=0.25f+0.65f*std::fmod(seed*1.73f,1.0f);instances[count].color[1]=0.25f+0.65f*std::fmod(seed*2.37f,1.0f);instances[count].color[2]=0.25f+0.65f*std::fmod(seed*3.11f,1.0f);instances[count].color[3]=1.0f;++count;}}
  if(count)vkCmdDrawIndexed(commandBuffer,indexCount_,static_cast<std::uint32_t>(count),0,0,0);vkCmdEndRenderPass(commandBuffer);return vkEndCommandBuffer(commandBuffer)==VK_SUCCESS;
}

bool VulkanRenderer::recreateSwapchain(){int w=0,h=0;glfwGetFramebufferSize(window_.native(),&w,&h);while(w==0||h==0){glfwWaitEvents();glfwGetFramebufferSize(window_.native(),&w,&h);}if(vkDeviceWaitIdle(device_)!=VK_SUCCESS)return false;destroyGraphicsPipeline();destroySwapchain();destroyDepthResources();if(renderPass_)vkDestroyRenderPass(device_,renderPass_,nullptr);renderPass_=VK_NULL_HANDLE;return createSwapchain()&&createDepthResources()&&createRenderPass()&&createGraphicsPipeline();}

void VulkanRenderer::destroySwapchain()noexcept{for(auto fb:framebuffers_)if(fb)vkDestroyFramebuffer(device_,fb,nullptr);framebuffers_.clear();for(auto view:swapchainImageViews_)if(view)vkDestroyImageView(device_,view,nullptr);swapchainImageViews_.clear();if(swapchain_)vkDestroySwapchainKHR(device_,swapchain_,nullptr);swapchain_=VK_NULL_HANDLE;swapchainImages_.clear();imagesInFlight_.clear();}
void VulkanRenderer::destroyFrameResources()noexcept{if(!device_)return;for(std::size_t i=0;i<MaxFramesInFlight;++i){if(imageAvailable_[i])vkDestroySemaphore(device_,imageAvailable_[i],nullptr);if(renderFinished_[i])vkDestroySemaphore(device_,renderFinished_[i],nullptr);if(inFlight_[i])vkDestroyFence(device_,inFlight_[i],nullptr);imageAvailable_[i]=VK_NULL_HANDLE;renderFinished_[i]=VK_NULL_HANDLE;inFlight_[i]=VK_NULL_HANDLE;}if(commandPool_)vkDestroyCommandPool(device_,commandPool_,nullptr);commandPool_=VK_NULL_HANDLE;commandBuffers_.fill(VK_NULL_HANDLE);}
void VulkanRenderer::destroyDepthResources()noexcept{if(!device_)return;if(depthView_)vkDestroyImageView(device_,depthView_,nullptr);if(depthImage_)vkDestroyImage(device_,depthImage_,nullptr);if(depthMemory_)vkFreeMemory(device_,depthMemory_,nullptr);depthView_=VK_NULL_HANDLE;depthImage_=VK_NULL_HANDLE;depthMemory_=VK_NULL_HANDLE;}
void VulkanRenderer::destroyGraphicsPipeline()noexcept{if(!device_)return;if(graphicsPipeline_)vkDestroyPipeline(device_,graphicsPipeline_,nullptr);if(pipelineLayout_)vkDestroyPipelineLayout(device_,pipelineLayout_,nullptr);graphicsPipeline_=VK_NULL_HANDLE;pipelineLayout_=VK_NULL_HANDLE;}
void VulkanRenderer::destroyGeometryBuffers()noexcept{if(!device_)return;if(indexBuffer_)vkDestroyBuffer(device_,indexBuffer_,nullptr);if(indexMemory_)vkFreeMemory(device_,indexMemory_,nullptr);if(vertexBuffer_)vkDestroyBuffer(device_,vertexBuffer_,nullptr);if(vertexMemory_)vkFreeMemory(device_,vertexMemory_,nullptr);indexBuffer_=VK_NULL_HANDLE;indexMemory_=VK_NULL_HANDLE;vertexBuffer_=VK_NULL_HANDLE;vertexMemory_=VK_NULL_HANDLE;indexCount_=0;}
void VulkanRenderer::destroyInstanceBuffers()noexcept{if(!device_)return;const VkDeviceSize size=sizeof(InstanceData)*MaxInstances;for(std::size_t i=0;i<MaxFramesInFlight;++i){if(instanceMapped_[i])vkUnmapMemory(device_,instanceMemories_[i]);if(instanceBuffers_[i])vkDestroyBuffer(device_,instanceBuffers_[i],nullptr);if(instanceMemories_[i])vkFreeMemory(device_,instanceMemories_[i],nullptr);instanceMapped_[i]=nullptr;instanceBuffers_[i]=VK_NULL_HANDLE;instanceMemories_[i]=VK_NULL_HANDLE;}(void)size;}
void VulkanRenderer::destroyDescriptorResources()noexcept{if(!device_)return;if(descriptorPool_)vkDestroyDescriptorPool(device_,descriptorPool_,nullptr);if(descriptorSetLayout_)vkDestroyDescriptorSetLayout(device_,descriptorSetLayout_,nullptr);descriptorPool_=VK_NULL_HANDLE;descriptorSetLayout_=VK_NULL_HANDLE;descriptorSets_.fill(VK_NULL_HANDLE);}

void VulkanRenderer::draw(){render::RenderSnapshot empty;draw(empty);}
void VulkanRenderer::draw(const render::RenderSnapshot& snapshot){if(!device_||!swapchain_||imagesInFlight_.empty())return;if(vkWaitForFences(device_,1,&inFlight_[currentFrame_],VK_TRUE,UINT64_MAX)!=VK_SUCCESS)return;std::uint32_t imageIndex=0;VkResult result=vkAcquireNextImageKHR(device_,swapchain_,UINT64_MAX,imageAvailable_[currentFrame_],VK_NULL_HANDLE,&imageIndex);if(result==VK_ERROR_OUT_OF_DATE_KHR){if(!recreateSwapchain())shutdown();return;}if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR)return;if(imagesInFlight_[imageIndex]&&vkWaitForFences(device_,1,&imagesInFlight_[imageIndex],VK_TRUE,UINT64_MAX)!=VK_SUCCESS)return;if(vkResetCommandBuffer(commandBuffers_[currentFrame_],0)!=VK_SUCCESS)return;if(!recordCommandBuffer(commandBuffers_[currentFrame_],imageIndex,&snapshot))return;if(vkResetFences(device_,1,&inFlight_[currentFrame_])!=VK_SUCCESS)return;imagesInFlight_[imageIndex]=inFlight_[currentFrame_];const VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&imageAvailable_[currentFrame_];submit.pWaitDstStageMask=&waitStage;submit.commandBufferCount=1;submit.pCommandBuffers=&commandBuffers_[currentFrame_];submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&renderFinished_[currentFrame_];if(vkQueueSubmit(graphicsQueue_,1,&submit,inFlight_[currentFrame_])!=VK_SUCCESS)return;VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&renderFinished_[currentFrame_];present.swapchainCount=1;present.pSwapchains=&swapchain_;present.pImageIndices=&imageIndex;result=vkQueuePresentKHR(presentQueue_,&present);if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR){if(!recreateSwapchain())shutdown();}else if(result!=VK_SUCCESS)Log::write(LogLevel::Error,"Vulkan queue present failed");currentFrame_=(currentFrame_+1)%MaxFramesInFlight;}

void VulkanRenderer::shutdown()noexcept{if(device_)vkDeviceWaitIdle(device_);destroyDescriptorResources();destroyInstanceBuffers();destroyGeometryBuffers();destroyGraphicsPipeline();destroyDepthResources();destroySwapchain();if(renderPass_&&device_)vkDestroyRenderPass(device_,renderPass_,nullptr);renderPass_=VK_NULL_HANDLE;destroyFrameResources();if(device_)vkDestroyDevice(device_,nullptr);device_=VK_NULL_HANDLE;if(surface_&&instance_)vkDestroySurfaceKHR(instance_,surface_,nullptr);surface_=VK_NULL_HANDLE;if(instance_)vkDestroyInstance(instance_,nullptr);instance_=VK_NULL_HANDLE;physicalDevice_=VK_NULL_HANDLE;graphicsQueue_=VK_NULL_HANDLE;presentQueue_=VK_NULL_HANDLE;currentFrame_=0;}
} // namespace btai
