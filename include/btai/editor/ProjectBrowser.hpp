#pragma once
#include "btai/project/Project.hpp"
#include <filesystem>
#include <vector>

namespace btai::editor {
enum class AssetKind { Folder, File };
struct AssetEntry {
  std::filesystem::path path;
  AssetKind kind=AssetKind::File;
};
class ProjectBrowser final {
public:
  explicit ProjectBrowser(const project::Project& project):project_(project){}
  std::vector<AssetEntry> entries(const std::filesystem::path& relative={}) const;
private:
  const project::Project& project_;
};
}
