#include "btai/core/Engine.hpp"
#include <filesystem>

int main(int argc,char** argv) {
  btai::EngineConfig config;
  if(argc>2)return 2;
  if(argc==2)config.projectManifest=std::filesystem::path(argv[1]);
  btai::Engine engine(config);
  return engine.run();
}
