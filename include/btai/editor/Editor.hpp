#pragma once
#include "btai/ecs/Registry.hpp"
#include "btai/project/Project.hpp"
#include "btai/editor/ProjectBrowser.hpp"
#include "btai/render/VulkanRenderer.hpp"
#include <atomic>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;
struct ImDrawData;

namespace btai::editor {
class Editor final {
public:
  Editor(GLFWwindow& window, project::Project& project, ecs::Registry& registry, std::atomic_bool& paused);
  ~Editor();
  Editor(const Editor&) = delete;
  Editor& operator=(const Editor&) = delete;
  bool initialize(const VulkanRenderer::ImGuiBackendContext& context);
  void shutdown() noexcept;
  void newFrame();
  void draw();
  void render(VkCommandBuffer commandBuffer);
  bool wantsMouseCapture() const noexcept;
  bool wantsKeyboardCapture() const noexcept;
private:
  void drawMenuBar();
  void drawToolbar();
  void drawHierarchy();
  void drawInspector();
  void drawProject();
  void drawConsole();
  void drawSceneStats();
  bool editVec3(const char* label, ecs::Vec3& value, float speed=0.05f);
  GLFWwindow& window_;
  project::Project& project_;
  ecs::Registry& registry_;
  std::atomic_bool& paused_;
  ProjectBrowser browser_;
  VulkanRenderer::ImGuiBackendContext context_{};
  std::filesystem::path projectPath_;
  std::uint32_t selectedIndex_=std::numeric_limits<std::uint32_t>::max();
  std::uint32_t selectedGeneration_=0;
  bool initialized_=false;
  bool showConsole_=true;
  bool showStats_=true;
};
}
