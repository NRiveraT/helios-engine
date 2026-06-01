/// @file VulkanResources.h
/// @brief Internal Buffer / Texture POD + the device-side pools.
///
/// Public `BufferHandle` / `TextureHandle` (in RHI/Public/) carry an opaque ID
/// + the bindless slot(s). Inside the RHI we look up the full backing struct
/// via that ID in these maps.
#pragma once

#include <RHI/Public/Buffer.h>
#include <RHI/Public/Texture.h>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <unordered_map>

namespace helio::rhi::vulkan {

struct VulkanBuffer {
    VkBuffer Buffer{VK_NULL_HANDLE};
    VmaAllocation Allocation{VK_NULL_HANDLE};
    uint64_t Size{0};
    BufferUsage Usage{BufferUsage::None};
    MemoryUsage Memory{MemoryUsage::DeviceLocal};
    uint32_t BindlessSlot{UINT32_MAX};
    void* MappedPtr{nullptr};
};

struct VulkanTexture {
    VkImage Image{VK_NULL_HANDLE};
    VmaAllocation Allocation{VK_NULL_HANDLE};
    VkImageView View{VK_NULL_HANDLE};
    VkFormat Fmt{VK_FORMAT_UNDEFINED};
    VkImageLayout CurrentLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    uint32_t Width{0};
    uint32_t Height{0};
    uint32_t Depth{1};
    uint32_t MipLevels{1};
    uint32_t ArrayLayers{1};
    TextureUsage Usage{TextureUsage::None};
    uint32_t SampledSlot{UINT32_MAX};
    uint32_t StorageSlot{UINT32_MAX};
};

class VulkanResourcePool {
public:
    using BufferMap  = std::unordered_map<uint64_t, VulkanBuffer>;
    using TextureMap = std::unordered_map<uint64_t, VulkanTexture>;

    [[nodiscard]] uint64_t InsertBuffer(VulkanBuffer&& B);
    [[nodiscard]] uint64_t InsertTexture(VulkanTexture&& T);

    [[nodiscard]] VulkanBuffer*  GetBuffer(uint64_t Id);
    [[nodiscard]] VulkanTexture* GetTexture(uint64_t Id);

    [[nodiscard]] VulkanBuffer  TakeBuffer(uint64_t Id);
    [[nodiscard]] VulkanTexture TakeTexture(uint64_t Id);

    /// Shutdown-only escape hatch — direct access to the underlying maps so
    /// `VulkanContext::~VulkanContext` can drain leaked user-created resources
    /// before VMA's destructor asserts on outstanding allocations. Don't use
    /// during normal operation.
    BufferMap&  GetBufferMapForShutdown()  { return m_buffers; }
    TextureMap& GetTextureMapForShutdown() { return m_textures; }

private:
    BufferMap  m_buffers;
    TextureMap m_textures;
    uint64_t   m_nextId{1};
};

} // namespace helio::rhi::vulkan
