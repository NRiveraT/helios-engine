/// @file VulkanAccelStructure.h
/// @brief Internal BLAS/TLAS POD + builder using KHR ray-tracing extensions.
///
/// Each accel structure owns:
///  - a VkAccelerationStructureKHR handle
///  - a backing VkBuffer (the AS's "storage" — driver-managed contents)
///  - the buffer's VmaAllocation
///  - the AS's device address (so a TLAS can reference its BLASes, and so
///    shaders can read the TLAS via the descriptor)
///
/// Building is synchronous in V1: the builder allocates a one-shot command
/// buffer, records vkCmdBuildAccelerationStructuresKHR, submits, and waits.
/// Phase 9's render graph will move builds into the frame graph for async.
#pragma once

#include "VulkanCheck.h"
#include <RHI/Public/AccelStructure.h>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace helio::rhi::vulkan {

struct VulkanBLAS {
    VkAccelerationStructureKHR Accel{VK_NULL_HANDLE};
    VkBuffer  Storage{VK_NULL_HANDLE};
    VmaAllocation StorageAlloc{VK_NULL_HANDLE};
    VkDeviceAddress DeviceAddress{0};
    uint64_t  SizeBytes{0};
};

struct VulkanTLAS {
    VkAccelerationStructureKHR Accel{VK_NULL_HANDLE};
    VkBuffer  Storage{VK_NULL_HANDLE};
    VmaAllocation StorageAlloc{VK_NULL_HANDLE};
    VkBuffer  InstanceBuf{VK_NULL_HANDLE};       // VkAccelerationStructureInstanceKHR array
    VmaAllocation InstanceAlloc{VK_NULL_HANDLE};
    VkDeviceAddress DeviceAddress{0};
    uint64_t  SizeBytes{0};
    uint32_t  InstanceCount{0};
};

class VulkanContext;

class VulkanAccelBuilder {
public:
    VulkanAccelBuilder(VulkanContext& Ctx);
    void Shutdown();

    /// Synchronous: allocates, records build, submits, waits.
    [[nodiscard]] VulkanBLAS BuildBLAS(const BLASDesc& Desc);

    /// Synchronous: allocates, stages the per-instance struct array on the GPU,
    /// records build, submits, waits. `BLASAddresses[i]` is the device address
    /// of the BLAS that `Desc.Instances[i].BLAS` refers to.
    [[nodiscard]] VulkanTLAS BuildTLAS(const TLASDesc& Desc,
                                       const std::vector<VkDeviceAddress>& BLASAddresses);

    /// Get the device address of an arbitrary buffer (needed for vertex/index
    /// buffer references inside BLAS geometry descriptors).
    [[nodiscard]] VkDeviceAddress GetBufferDeviceAddress(VkBuffer Buf) const;

private:
    /// Helper: allocate a buffer + sub-allocation for AS storage.
    void AllocateAccelStorage(uint64_t Size, VkBufferUsageFlags Usage,
                              VkBuffer& OutBuf, VmaAllocation& OutAlloc);

    /// Helper: get a scratch buffer for the build. Caller frees via `vmaDestroyBuffer`.
    void AllocateScratch(uint64_t Size, VkBuffer& OutBuf, VmaAllocation& OutAlloc,
                         VkDeviceAddress& OutAddr);

    /// One-shot command buffer submit + fence wait.
    void ImmediateSubmit(VkCommandBuffer& Cmd);

    VulkanContext* m_ctx{nullptr};
    VkCommandPool m_pool{VK_NULL_HANDLE};
    VkCommandBuffer m_cmd{VK_NULL_HANDLE};
    VkFence m_fence{VK_NULL_HANDLE};
};

} // namespace helio::rhi::vulkan
