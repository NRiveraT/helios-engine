#include "VulkanResources.h"

#include <Core/Assert/Assert.h>

namespace helio::rhi::vulkan {

uint64_t VulkanResourcePool::InsertBuffer(VulkanBuffer&& B) {
    uint64_t Id = m_nextId++;
    m_buffers.emplace(Id, std::move(B));
    return Id;
}

uint64_t VulkanResourcePool::InsertTexture(VulkanTexture&& T) {
    uint64_t Id = m_nextId++;
    m_textures.emplace(Id, std::move(T));
    return Id;
}

VulkanBuffer* VulkanResourcePool::GetBuffer(uint64_t Id) {
    auto It = m_buffers.find(Id);
    return It == m_buffers.end() ? nullptr : &It->second;
}

VulkanTexture* VulkanResourcePool::GetTexture(uint64_t Id) {
    auto It = m_textures.find(Id);
    return It == m_textures.end() ? nullptr : &It->second;
}

VulkanBuffer VulkanResourcePool::TakeBuffer(uint64_t Id) {
    auto It = m_buffers.find(Id);
    HELIO_ASSERT(It != m_buffers.end());
    VulkanBuffer Out = std::move(It->second);
    m_buffers.erase(It);
    return Out;
}

VulkanTexture VulkanResourcePool::TakeTexture(uint64_t Id) {
    auto It = m_textures.find(Id);
    HELIO_ASSERT(It != m_textures.end());
    VulkanTexture Out = std::move(It->second);
    m_textures.erase(It);
    return Out;
}

} // namespace helio::rhi::vulkan
