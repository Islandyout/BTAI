#include "btai/project/Project.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace btai::project {
namespace {
constexpr const char* kManifest="Project.btai";
constexpr std::array<const char*,12> kFolders{"Assets","Scenes","Worlds","Chunks","Prefabs","Materials","Shaders","Scripts","Audio","Animations","Cache","Settings"};
std::filesystem::path canonicalRoot(const std::filesystem::path& root) {
  std::error_code ec;
  auto result=std::filesystem::weakly_canonical(root,ec);
  if(ec)throw std::runtime_error("Invalid project root: "+root.string());
  return result;
}
}
Project Project::create(const std::filesystem::path& root,std::string name) {
  if(name.empty())throw std::invalid_argument("Project name cannot be empty");
  const auto projectRoot=canonicalRoot(root);
  std::error_code ec;
  std::filesystem::create_directories(projectRoot,ec);
  if(ec)throw std::runtime_error("Cannot create project root: "+projectRoot.string());
  for(const char* folder:kFolders)std::filesystem::create_directories(projectRoot/folder,ec);
  if(ec)throw std::runtime_error("Cannot create project folders: "+projectRoot.string());
  const auto manifest=projectRoot/kManifest;
  nlohmann::json json={{"format",1},{"name",name},{"startupScene","Scenes/Main.btai"}};
  std::ofstream out(manifest);
  if(!out)throw std::runtime_error("Cannot write project manifest: "+manifest.string());
  out<<json.dump(2)<<'\n';
  return Project(ProjectConfig{std::move(name),projectRoot,"Scenes/Main.btai"});
}
Project Project::open(const std::filesystem::path& manifest) {
  std::ifstream in(manifest);
  if(!in)throw std::runtime_error("Cannot open project manifest: "+manifest.string());
  nlohmann::json json;
  try { in>>json; } catch(const std::exception& e) { throw std::runtime_error("Invalid project manifest: "+std::string(e.what())); }
  if(!json.is_object()||json.value("format",0)!=1)throw std::runtime_error("Unsupported project manifest: "+manifest.string());
  const auto name=json.value("name",std::string{});
  if(name.empty())throw std::runtime_error("Project manifest has no name: "+manifest.string());
  const auto root=canonicalRoot(manifest.parent_path());
  return Project(ProjectConfig{name,root,json.value("startupScene",std::string{"Scenes/Main.btai"})});
}
std::vector<std::filesystem::path> Project::list(const std::filesystem::path& relative) const {
  const auto directory=resolve(relative);
  std::error_code ec;
  if(!std::filesystem::is_directory(directory,ec)||ec)throw std::runtime_error("Project directory not found: "+relative.string());
  std::vector<std::filesystem::path> result;
  for(const auto& entry:std::filesystem::directory_iterator(directory,ec)) {
    if(ec)throw std::runtime_error("Cannot enumerate project directory: "+directory.string());
    result.push_back(std::filesystem::relative(entry.path(),root(),ec));
    if(ec)throw std::runtime_error("Cannot resolve project path: "+entry.path().string());
  }
  std::sort(result.begin(),result.end());
  return result;
}
std::filesystem::path Project::resolve(const std::filesystem::path& relative) const {
  if(relative.empty())return root();
  const auto candidate=std::filesystem::absolute(root()/relative).lexically_normal();
  const auto base=root().lexically_normal();
  const auto baseString=base.generic_string();
  const auto candidateString=candidate.generic_string();
  if(candidateString!=baseString&&candidateString.rfind(baseString+"/",0)!=0)throw std::invalid_argument("Project path escapes project root");
  return candidate;
}
}
