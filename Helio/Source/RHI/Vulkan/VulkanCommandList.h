/// @file VulkanCommandList.h
/// @brief Backend state behind the public CommandList facade.
///
/// Owned and configured per-frame by VulkanContext. The public CommandList's
/// `m_impl` points at one of these (cast to void*); the implementation of every
/// CommandList method is in VulkanCommandList.cpp and dispatches off `m_impl`.
#pragma once

#include <RHI/Public/CommandList.h>
#include "VulkanPipeline.h"

#include <volk.h>

#if HELIO_TRACY_ENABLED
    #include <tracy/TracyVulkan.hpp>
#endif

namespace helio::rhi::vulkan {

class VulkanContext;

struct VulkanCommandListImpl {
    VulkanContext* Ctx{nullptr};
    VkCommandBuffer Cmd{VK_NULL_HANDLE};
    VulkanPipeline Bound{};
    bool InRendering{false};

#if HELIO_TRACY_ENABLED
    TracyVkCtx Tracy{nullptr};
#endif
};

/// Re-bind the public CommandList's impl pointer and reset frame state.
void ResetCommandList(CommandList& Public, VulkanCommandListImpl& Impl);

} // namespace helio::rhi::vulkan
