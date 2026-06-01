#include "VulkanFormats.h"

namespace helio::rhi {

uint32_t BytesPerPixel(Format F) noexcept {
    switch (F) {
        case Format::R8_UNORM:           return 1;
        case Format::RG8_UNORM:          return 2;
        case Format::RGBA8_UNORM:
        case Format::RGBA8_SRGB:
        case Format::BGRA8_UNORM:
        case Format::BGRA8_SRGB:         return 4;
        case Format::R16F:               return 2;
        case Format::RG16F:              return 4;
        case Format::RGBA16F:            return 8;
        case Format::R32F:               return 4;
        case Format::RG32F:              return 8;
        case Format::RGB32F:             return 12;
        case Format::RGBA32F:            return 16;
        case Format::D32_SFLOAT:         return 4;
        case Format::D24_UNORM_S8_UINT:  return 4;
        default:                         return 0;
    }
}

} // namespace helio::rhi

namespace helio::rhi::vulkan {

VkFormat ToVk(Format F) noexcept {
    switch (F) {
        case Format::R8_UNORM:           return VK_FORMAT_R8_UNORM;
        case Format::RG8_UNORM:          return VK_FORMAT_R8G8_UNORM;
        case Format::RGBA8_UNORM:        return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB:         return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::BGRA8_UNORM:        return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_SRGB:         return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::R16F:               return VK_FORMAT_R16_SFLOAT;
        case Format::RG16F:              return VK_FORMAT_R16G16_SFLOAT;
        case Format::RGBA16F:            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::R32F:               return VK_FORMAT_R32_SFLOAT;
        case Format::RG32F:              return VK_FORMAT_R32G32_SFLOAT;
        case Format::RGB32F:             return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::RGBA32F:            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::D32_SFLOAT:         return VK_FORMAT_D32_SFLOAT;
        case Format::D24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
        default:                         return VK_FORMAT_UNDEFINED;
    }
}

VkBufferUsageFlags ToVk(BufferUsage U) noexcept {
    VkBufferUsageFlags V = 0;
    if (HasFlag(U, BufferUsage::Vertex))                V |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasFlag(U, BufferUsage::Index))                 V |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasFlag(U, BufferUsage::Storage))               V |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasFlag(U, BufferUsage::Uniform))               V |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasFlag(U, BufferUsage::TransferSrc))           V |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(U, BufferUsage::TransferDst))           V |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (HasFlag(U, BufferUsage::Indirect))              V |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (HasFlag(U, BufferUsage::ShaderDeviceAddress))   V |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (HasFlag(U, BufferUsage::AccelStructureBuild))   V |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (HasFlag(U, BufferUsage::AccelStructureStorage)) V |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (HasFlag(U, BufferUsage::SBT))                   V |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
                                                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    return V;
}

VkImageUsageFlags ToVk(TextureUsage U) noexcept {
    VkImageUsageFlags V = 0;
    if (HasFlag(U, TextureUsage::Sampled))                V |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasFlag(U, TextureUsage::Storage))                V |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasFlag(U, TextureUsage::ColorAttachment))        V |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasFlag(U, TextureUsage::DepthStencilAttachment)) V |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasFlag(U, TextureUsage::TransferSrc))            V |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(U, TextureUsage::TransferDst))            V |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return V;
}

VkImageAspectFlags AspectFor(Format F) noexcept {
    switch (F) {
        case Format::D32_SFLOAT:         return VK_IMAGE_ASPECT_DEPTH_BIT;
        case Format::D24_UNORM_S8_UINT:  return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:                         return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

VmaAllocationCreateFlags MemoryFlags(MemoryUsage M) noexcept {
    switch (M) {
        case MemoryUsage::HostUpload:    return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        case MemoryUsage::HostReadback:  return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        case MemoryUsage::DeviceLocal:
        default:                         return 0;
    }
}

VmaMemoryUsage MemoryHint(MemoryUsage M) noexcept {
    // VMA's "auto" picks heap based on usage flags; we set HOST_ACCESS_* above.
    (void)M;
    return VMA_MEMORY_USAGE_AUTO;
}

} // namespace helio::rhi::vulkan
