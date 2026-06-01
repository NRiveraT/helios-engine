/// @file VulkanFormats.h
/// @brief Format / usage conversion between public RHI enums and Vulkan.
#pragma once

#include <RHI/Public/Formats.h>
#include <RHI/Public/Buffer.h>
#include <RHI/Public/Texture.h>

#include <volk.h>
#include <vk_mem_alloc.h>

namespace helio::rhi::vulkan {

[[nodiscard]] VkFormat ToVk(Format F) noexcept;
[[nodiscard]] VkBufferUsageFlags ToVk(BufferUsage U) noexcept;
[[nodiscard]] VkImageUsageFlags ToVk(TextureUsage U) noexcept;
[[nodiscard]] VkImageAspectFlags AspectFor(Format F) noexcept;
[[nodiscard]] VmaAllocationCreateFlags MemoryFlags(MemoryUsage M) noexcept;
[[nodiscard]] VmaMemoryUsage MemoryHint(MemoryUsage M) noexcept;

} // namespace helio::rhi::vulkan
