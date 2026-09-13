#include "btai/render/VulkanRenderer.hpp"
namespace btai {
void VulkanRenderer::setEditor(editor::Editor* editor) noexcept { editor_=editor; }
VulkanRenderer::ImGuiBackendContext VulkanRenderer::imguiContext() const noexcept { return {VK_API_VERSION_1_2,instance_,physicalDevice_,device_,graphicsFamily_,graphicsQueue_,renderPass_,static_cast<std::uint32_t>(swapchainImages_.size())}; }
}
