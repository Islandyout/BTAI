#include "btai/assets/AssetManager.hpp"
#include "btai/core/Log.hpp"
#include <tiny_gltf.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace btai::assets {
namespace {

template<class T>
T component(const unsigned char* p) {
  T value{};
  std::memcpy(&value, p, sizeof(T));
  return value;
}

float scalar(const tinygltf::Accessor& accessor, const unsigned char* p) {
  switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: return component<float>(p);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return static_cast<float>(*p);
    case TINYGLTF_COMPONENT_TYPE_BYTE: return static_cast<float>(component<std::int8_t>(p));
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return static_cast<float>(component<std::uint16_t>(p));
    case TINYGLTF_COMPONENT_TYPE_SHORT: return static_cast<float>(component<std::int16_t>(p));
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return static_cast<float>(component<std::uint32_t>(p));
    default: throw std::runtime_error("Unsupported glTF accessor component type");
  }
}

std::size_t componentSize(int type) {
  switch (type) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
    default: throw std::runtime_error("Unsupported glTF component size");
  }
}

std::size_t componentCount(const std::string& type) {
  if (type == TINYGLTF_TYPE_SCALAR) return 1;
  if (type == TINYGLTF_TYPE_VEC2) return 2;
  if (type == TINYGLTF_TYPE_VEC3) return 3;
  if (type == TINYGLTF_TYPE_VEC4) return 4;
  throw std::runtime_error("Unsupported glTF accessor type");
}

const tinygltf::Accessor& accessorFor(const tinygltf::Model& model, int index) {
  if (index < 0 || static_cast<std::size_t>(index) >= model.accessors.size()) throw std::runtime_error("Invalid glTF accessor index");
  return model.accessors[static_cast<std::size_t>(index)];
}

const unsigned char* accessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::size_t element) {
  if (accessor.bufferView < 0 || static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size()) throw std::runtime_error("Invalid glTF buffer view");
  const auto& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
  if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= model.buffers.size()) throw std::runtime_error("Invalid glTF buffer");
  const auto& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
  const std::size_t stride = view.byteStride ? view.byteStride : componentSize(accessor.componentType) * componentCount(accessor.type);
  const std::size_t offset = view.byteOffset + accessor.byteOffset + element * stride;
  const std::size_t required = componentSize(accessor.componentType) * componentCount(accessor.type);
  if (offset > buffer.data.size() || required > buffer.data.size() - offset) throw std::runtime_error("glTF accessor exceeds buffer bounds");
  return buffer.data.data() + offset;
}

std::array<float, 4> readVector(const tinygltf::Model& model, int accessorIndex, std::size_t element, std::size_t expected) {
  const auto& accessor = accessorFor(model, accessorIndex);
  const std::size_t count = componentCount(accessor.type);
  if (count < expected || element >= accessor.count) throw std::runtime_error("Invalid glTF vertex attribute");
  const auto* data = accessorData(model, accessor, element);
  const std::size_t size = componentSize(accessor.componentType);
  std::array<float, 4> result{0, 0, 0, 1};
  for (std::size_t i = 0; i < expected; ++i) {
    result[i] = scalar(accessor, data + i * size);
    if (accessor.normalized) {
      if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) result[i] /= 255.0f;
      else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) result[i] /= 65535.0f;
      else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE) result[i] = std::max(-1.0f, result[i] / 127.0f);
      else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT) result[i] = std::max(-1.0f, result[i] / 32767.0f);
    }
  }
  return result;
}

std::uint32_t readIndex(const tinygltf::Model& model, int accessorIndex, std::size_t element) {
  const auto& accessor = accessorFor(model, accessorIndex);
  if (accessor.type != TINYGLTF_TYPE_SCALAR || element >= accessor.count) throw std::runtime_error("Invalid glTF index accessor");
  const auto* p = accessorData(model, accessor, element);
  switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return *p;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return component<std::uint16_t>(p);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return component<std::uint32_t>(p);
    default: throw std::runtime_error("glTF indices must be unsigned integers");
  }
}

void quaternionMatrix(const std::vector<double>& q, float* m) {
  const float x=static_cast<float>(q[0]), y=static_cast<float>(q[1]), z=static_cast<float>(q[2]), w=static_cast<float>(q[3]);
  m[0]=1-2*(y*y+z*z); m[1]=2*(x*y+z*w); m[2]=2*(x*z-y*w); m[3]=0;
  m[4]=2*(x*y-z*w); m[5]=1-2*(x*x+z*z); m[6]=2*(y*z+x*w); m[7]=0;
  m[8]=2*(x*z+y*w); m[9]=2*(y*z-x*w); m[10]=1-2*(x*x+y*y); m[11]=0;
}

void makeNodeMatrix(const tinygltf::Node& source, Node& node) {
  if (source.matrix.size() == 16) {
    for (std::size_t i=0;i<16;++i) node.local[i]=static_cast<float>(source.matrix[i]);
    return;
  }
  std::fill(std::begin(node.local), std::end(node.local), 0.0f);
  node.local[0]=node.local[5]=node.local[10]=node.local[15]=1.0f;
  if (source.scale.size()==3) { node.local[0]=static_cast<float>(source.scale[0]); node.local[5]=static_cast<float>(source.scale[1]); node.local[10]=static_cast<float>(source.scale[2]); }
  if (source.rotation.size()==4) {
    float r[16]{}; quaternionMatrix(source.rotation,r);
    float s[3]{node.local[0],node.local[5],node.local[10]};
    node.local[0]=r[0]*s[0];node.local[1]=r[1]*s[0];node.local[2]=r[2]*s[0];
    node.local[4]=r[4]*s[1];node.local[5]=r[5]*s[1];node.local[6]=r[6]*s[1];
    node.local[8]=r[8]*s[2];node.local[9]=r[9]*s[2];node.local[10]=r[10]*s[2];
  }
  if (source.translation.size()==3) { node.local[12]=static_cast<float>(source.translation[0]); node.local[13]=static_cast<float>(source.translation[1]); node.local[14]=static_cast<float>(source.translation[2]); }
}

} // namespace

std::shared_ptr<const Model> AssetManager::load(const std::filesystem::path& path) {
  const auto key = std::filesystem::weakly_canonical(path).lexically_normal().string();
  {
    std::lock_guard lock(mutex_);
    if (auto it=cache_.find(key); it!=cache_.end()) if (auto cached=it->second.lock()) return cached;
  }
  auto model=loadImpl(std::filesystem::path(key));
  {
    std::lock_guard lock(mutex_);
    cache_[key]=model;
  }
  return model;
}

std::shared_future<std::shared_ptr<const Model>> AssetManager::loadAsync(std::filesystem::path path) {
  return jobs_.submit([this, path=std::move(path)] { return load(path); }).share();
}

std::shared_ptr<const Model> AssetManager::loadImpl(const std::filesystem::path& path) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model source;
  std::string error, warning;
  const std::string filename=path.string();
  const bool ok=path.extension()==".glb" ? loader.LoadBinaryFromFile(&source,&error,&warning,filename) : loader.LoadASCIIFromFile(&source,&error,&warning,filename);
  if (!warning.empty()) Log::write(LogLevel::Warn,"glTF: "+warning);
  if (!ok) throw std::runtime_error("Failed to load glTF '"+filename+"': "+error);

  auto result=std::make_shared<Model>();
  result->source=path;
  result->materials.reserve(source.materials.size());
  for (const auto& material:source.materials) {
    Material out;
    const auto& p=material.pbrMetallicRoughness;
    for (std::size_t i=0;i<4&&i<p.baseColorFactor.size();++i) out.baseColor[i]=static_cast<float>(p.baseColorFactor[i]);
    out.metallic=static_cast<float>(p.metallicFactor); out.roughness=static_cast<float>(p.roughnessFactor);
    if (p.baseColorTexture.index>=0 && static_cast<std::size_t>(p.baseColorTexture.index)<source.textures.size()) {
      const int image=source.textures[static_cast<std::size_t>(p.baseColorTexture.index)].source;
      if (image>=0 && static_cast<std::size_t>(image)<source.images.size()) out.baseColorTexture=source.images[static_cast<std::size_t>(image)].uri;
    }
    result->materials.push_back(std::move(out));
  }
  result->images.reserve(source.images.size());
  for (const auto& image:source.images) result->images.push_back(Image{image.width,image.height,image.component,image.image});

  result->meshes.reserve(source.meshes.size());
  for (const auto& mesh:source.meshes) {
    Mesh out; out.name=mesh.name; out.primitives.reserve(mesh.primitives.size());
    for (const auto& primitive:mesh.primitives) {
      if (primitive.mode!=TINYGLTF_MODE_TRIANGLES && primitive.mode!=TINYGLTF_MODE_UNDEFINED) throw std::runtime_error("BTAI currently requires triangle glTF primitives");
      auto pos=primitive.attributes.find("POSITION"); if (pos==primitive.attributes.end()) throw std::runtime_error("glTF primitive has no POSITION attribute");
      const auto& pa=accessorFor(source,pos->second); Primitive p; p.material=primitive.material; p.vertices.resize(pa.count);
      for (std::size_t i=0;i<pa.count;++i) {
        const auto v=readVector(source,pos->second,i,3); std::copy_n(v.data(),3,p.vertices[i].position);
        if (auto it=primitive.attributes.find("NORMAL");it!=primitive.attributes.end()) { const auto n=readVector(source,it->second,i,3);std::copy_n(n.data(),3,p.vertices[i].normal); }
        if (auto it=primitive.attributes.find("TEXCOORD_0");it!=primitive.attributes.end()) { const auto uv=readVector(source,it->second,i,2);std::copy_n(uv.data(),2,p.vertices[i].uv); }
        if (auto it=primitive.attributes.find("TANGENT");it!=primitive.attributes.end()) { const auto t=readVector(source,it->second,i,4);std::copy_n(t.data(),4,p.vertices[i].tangent); }
      }
      if (primitive.indices>=0) { const auto& ia=accessorFor(source,primitive.indices);p.indices.resize(ia.count);for(std::size_t i=0;i<ia.count;++i)p.indices[i]=readIndex(source,primitive.indices,i); }
      else { p.indices.resize(p.vertices.size());for(std::size_t i=0;i<p.indices.size();++i)p.indices[i]=static_cast<std::uint32_t>(i); }
      out.primitives.push_back(std::move(p));
    }
    result->meshes.push_back(std::move(out));
  }

  result->nodes.reserve(source.nodes.size());
  for (std::size_t i=0;i<source.nodes.size();++i) { Node node;node.name=source.nodes[i].name;node.mesh=source.nodes[i].mesh;makeNodeMatrix(source.nodes[i],node);for(int child:source.nodes[i].children){if(child<0||static_cast<std::size_t>(child)>=source.nodes.size())throw std::runtime_error("Invalid glTF node child");node.children.push_back(child);}result->nodes.push_back(std::move(node)); }
  for (std::size_t i=0;i<result->nodes.size();++i) for(int child:result->nodes[i].children) result->nodes[static_cast<std::size_t>(child)].parent=static_cast<std::int32_t>(i);
  for (std::size_t i=0;i<result->nodes.size();++i) if(result->nodes[i].parent<0) result->roots.push_back(static_cast<std::int32_t>(i));
  return result;
}

void AssetManager::clear() { std::lock_guard lock(mutex_);cache_.clear(); }
std::size_t AssetManager::cachedCount() const { std::lock_guard lock(mutex_);std::size_t n=0;for(auto it=cache_.begin();it!=cache_.end();) {if(it->second.expired()) it=cache_.erase(it);else {++n;++it;}}return n;}

} // namespace btai::assets
