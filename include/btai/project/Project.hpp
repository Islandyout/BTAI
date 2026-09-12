#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace btai::project {
struct ProjectConfig {
  std::string name;
  std::filesystem::path root;
  std::filesystem::path startupScene;
};
class Project final {
public:
  static Project open(const std::filesystem::path& manifest);
  static Project create(const std::filesystem::path& root,std::string name);
  const ProjectConfig& config() const noexcept { return config_; }
  const std::filesystem::path& root() const noexcept { return config_.root; }
  std::filesystem::path assets() const { return root()/"Assets"; }
  std::filesystem::path scenes() const { return root()/"Scenes"; }
  std::filesystem::path worlds() const { return root()/"Worlds"; }
  std::filesystem::path chunks() const { return root()/"Chunks"; }
  std::filesystem::path prefabs() const { return root()/"Prefabs"; }
  std::filesystem::path materials() const { return root()/"Materials"; }
  std::filesystem::path shaders() const { return root()/"Shaders"; }
  std::filesystem::path scripts() const { return root()/"Scripts"; }
  std::filesystem::path audio() const { return root()/"Audio"; }
  std::filesystem::path animations() const { return root()/"Animations"; }
  std::filesystem::path cache() const { return root()/"Cache"; }
  std::vector<std::filesystem::path> list(const std::filesystem::path& relative={}) const;
  std::filesystem::path resolve(const std::filesystem::path& relative) const;
private:
  explicit Project(ProjectConfig config):config_(std::move(config)){}
  ProjectConfig config_;
};
}
