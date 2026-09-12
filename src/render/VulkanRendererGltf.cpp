#include "btai/render/VulkanRenderer.hpp"
#include "btai/core/Log.hpp"
#include <cstring>
#include <vector>

namespace btai {

bool VulkanRenderer::uploadBuffer(const void* data,VkDeviceSize size,VkBufferUsageFlags usage,VkBuffer& buffer,VkDeviceMemory& memory){
  if(!data||!size)return false;
  VkBuffer staging=VK_NULL_HANDLE;VkDeviceMemory stagingMemory=VK_NULL_HANDLE;
  if(!createBuffer(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,staging,stagingMemory))return false;
  void* mapped=nullptr;
  if(vkMapMemory(device_,stagingMemory,0,size,0,&mapped)!=VK_SUCCESS){vkDestroyBuffer(device_,staging,nullptr);vkFreeMemory(device_,stagingMemory,nullptr);return false;}
  std::memcpy(mapped,data,static_cast<std::size_t>(size));vkUnmapMemory(device_,stagingMemory);
  const bool created=createBuffer(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT|usage,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,buffer,memory);
  const bool copied=created&&copyBuffer(staging,buffer,size);
  vkDestroyBuffer(device_,staging,nullptr);vkFreeMemory(device_,stagingMemory,nullptr);
  if(!copied){if(buffer)vkDestroyBuffer(device_,buffer,nullptr);if(memory)vkFreeMemory(device_,memory,nullptr);buffer=VK_NULL_HANDLE;memory=VK_NULL_HANDLE;}
  return copied;
}

bool VulkanRenderer::uploadModel(const assets::Model& model){
  std::vector<Vertex> vertices;std::vector<std::uint32_t> indices;
  for(const auto& mesh:model.meshes){
    for(const auto& primitive:mesh.primitives){
      if(primitive.vertices.empty()||primitive.indices.empty())continue;
      const auto base=static_cast<std::uint32_t>(vertices.size());
      float color[3]{.75f,.75f,.75f};
      if(primitive.material>=0&&static_cast<std::size_t>(primitive.material)<model.materials.size()){
        const auto& c=model.materials[static_cast<std::size_t>(primitive.material)].baseColor;
        color[0]=c[0];color[1]=c[1];color[2]=c[2];
      }
      vertices.reserve(vertices.size()+primitive.vertices.size());indices.reserve(indices.size()+primitive.indices.size());
      for(const auto& source:primitive.vertices){Vertex out{};std::memcpy(out.position,source.position,sizeof(out.position));std::memcpy(out.color,color,sizeof(out.color));vertices.push_back(out);}
      for(const auto index:primitive.indices){if(index>=primitive.vertices.size())return false;indices.push_back(base+index);}
    }
  }
  if(vertices.empty()||indices.empty())return false;
  VkBuffer vertex=VK_NULL_HANDLE,index=VK_NULL_HANDLE;VkDeviceMemory vertexMemory=VK_NULL_HANDLE,indexMemory=VK_NULL_HANDLE;
  if(!uploadBuffer(vertices.data(),sizeof(Vertex)*vertices.size(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertex,vertexMemory)||!uploadBuffer(indices.data(),sizeof(std::uint32_t)*indices.size(),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,index,indexMemory)){
    if(vertex)vkDestroyBuffer(device_,vertex,nullptr);if(vertexMemory)vkFreeMemory(device_,vertexMemory,nullptr);if(index)vkDestroyBuffer(device_,index,nullptr);if(indexMemory)vkFreeMemory(device_,indexMemory,nullptr);return false;
  }
  destroyGeometryBuffers();vertexBuffer_=vertex;vertexMemory_=vertexMemory;indexBuffer_=index;indexMemory_=indexMemory;indexCount_=static_cast<std::uint32_t>(indices.size());return true;
}

void VulkanRenderer::setModel(std::shared_ptr<const assets::Model> model){
  if(model_==model)return;
  if(!device_){model_=std::move(model);return;}
  if(vkDeviceWaitIdle(device_)!=VK_SUCCESS)return;
  if(!model){destroyGeometryBuffers();model_.reset();return;}
  if(!uploadModel(*model)){Log::write(LogLevel::Error,"Failed to upload glTF model to GPU");return;}
  model_=std::move(model);
}

} // namespace btai
