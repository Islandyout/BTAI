#include "btai/editor/ProjectBrowser.hpp"
#include <algorithm>
#include <stdexcept>

namespace btai::editor {
std::vector<AssetEntry> ProjectBrowser::entries(const std::filesystem::path& relative) const {
  const auto directory=project_.resolve(relative);
  std::error_code ec;
  if(!std::filesystem::is_directory(directory,ec)||ec)throw std::runtime_error("Editor folder not found: "+relative.string());
  std::vector<AssetEntry> result;
  for(const auto& entry:std::filesystem::directory_iterator(directory,ec)) {
    if(ec)throw std::runtime_error("Cannot enumerate editor folder: "+directory.string());
    const auto path=std::filesystem::relative(entry.path(),project_.root(),ec);
    if(ec)throw std::runtime_error("Cannot resolve editor asset path: "+entry.path().string());
    result.push_back({path,entry.is_directory(ec)?AssetKind::Folder:AssetKind::File});
    if(ec)throw std::runtime_error("Cannot inspect editor asset: "+entry.path().string());
  }
  std::sort(result.begin(),result.end(),[](const AssetEntry& a,const AssetEntry& b){
    if(a.kind!=b.kind)return a.kind==AssetKind::Folder;
    return a.path.filename()<b.path.filename();
  });
  return result;
}
}
