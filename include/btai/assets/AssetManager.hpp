#pragma once

#include "btai/jobs/JobSystem.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace btai::assets {

struct Vertex {
  float position[3]{};
  float normal[3]{0.0f, 1.0f, 0.0f};
  float uv[2]{};
  float tangent[4]{1.0f, 0.0f, 0.0f, 1.0f};
};

struct Primitive {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::int32_t material = -1;
};

struct Material {
  float baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
  float metallic = 0.0f;
  float roughness = 1.0f;
  std::string baseColorTexture;
};

struct Image {
  int width = 0;
  int height = 0;
  int channels = 0;
  std::vector<std::uint8_t> pixels;
};

struct Mesh {
  std::string name;
  std::vector<Primitive> primitives;
};

struct Node {
  std::string name;
  std::int32_t mesh = -1;
  std::int32_t parent = -1;
  float local[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  std::vector<std::int32_t> children;
};

struct Model {
  std::filesystem::path source;
  std::vector<Mesh> meshes;
  std::vector<Material> materials;
  std::vector<Image> images;
  std::vector<Node> nodes;
  std::vector<std::int32_t> roots;
};

class AssetManager final {
public:
  explicit AssetManager(JobSystem& jobs) : jobs_(jobs) {}

  std::shared_future<std::shared_ptr<const Model>> loadAsync(std::filesystem::path path);
  std::shared_ptr<const Model> load(const std::filesystem::path& path);
  void clear();
  std::size_t cachedCount() const;

private:
  std::shared_ptr<const Model> loadImpl(const std::filesystem::path& path);

  JobSystem& jobs_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::weak_ptr<const Model>> cache_;
};

} // namespace btai::assets
