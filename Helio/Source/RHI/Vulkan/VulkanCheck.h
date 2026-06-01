/// @file VulkanCheck.h
/// @brief VK_CHECK macro — log + assert on a non-success VkResult.
#pragma once

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>

#include <volk.h>

namespace helio::rhi::vulkan {

const char* VkResultToString(VkResult R) noexcept;

} // namespace helio::rhi::vulkan

#define VK_CHECK(Expr)                                                                                       \
    do {                                                                                                     \
        VkResult _vr = (Expr);                                                                               \
        if (_vr != VK_SUCCESS) {                                                                             \
            HELIO_LOG_CRITICAL("RHI", "Vulkan call failed: " #Expr " -> {} ({}:{})",                         \
                               ::helio::rhi::vulkan::VkResultToString(_vr), __FILE__, __LINE__);             \
            HELIO_CHECK(_vr == VK_SUCCESS);                                                                  \
        }                                                                                                    \
    } while (0)
