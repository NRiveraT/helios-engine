/// @file Formats.h
/// @brief Back-end-agnostic Format enum used by Buffer/Texture descriptors.
///
/// Only formats we actually use in V1. Adding a new format = add the enum
/// entry here + the VkFormat mapping in `RHI/Vulkan/VulkanFormats.cpp`.
#pragma once

#include <cstdint>

namespace helio::rhi {

enum class Format : uint8_t {
    Undefined = 0,

    // 8-bit
    R8_UNORM,
    RG8_UNORM,
    RGBA8_UNORM,
    RGBA8_SRGB,
    BGRA8_UNORM,
    BGRA8_SRGB,

    // 16-bit float
    R16F,
    RG16F,
    RGBA16F,

    // 32-bit float
    R32F,
    RG32F,
    RGB32F,
    RGBA32F,

    // Depth
    D32_SFLOAT,
    D24_UNORM_S8_UINT,
};

[[nodiscard]] uint32_t BytesPerPixel(Format F) noexcept;

} // namespace helio::rhi
