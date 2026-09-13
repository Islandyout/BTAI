#include "btai/render/VulkanRenderer.hpp"
#include "btai/editor/Editor.hpp"
#define vkCmdEndRenderPass(commandBuffer) do { if(editor_) editor_->render(commandBuffer); (vkCmdEndRenderPass)(commandBuffer); } while(false)
#include "VulkanRendererStateOfArt.cpp"
#undef vkCmdEndRenderPass
