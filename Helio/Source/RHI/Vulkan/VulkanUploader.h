/// @file VulkanUploader.h
/// @brief Synchronous staging-buffer transfer helper.
///
/// Owns:
/// - a dedicated command pool for one-shot transfers
/// - a fence to block until the GPU finishes
/// - a reusable host-visible staging buffer that grows on demand
///
/// One uploader per device. Caller is responsible for thread safety —
/// `UploadToBuffer` / `UploadToImage` should be called from the main thread.
#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace helio::rhi::vulkan {

class VulkanUploader {
public:
    VulkanUploader(VkDevice Device, VmaAllocator Allocator, VkQueue Queue, uint32_t QueueFamily);
    ~VulkanUploader() = default;

    void Shutdown();

    /// Copy `Size` bytes from `Data` into `Dst[Offset..Offset+Size)` via staging.
    /// Blocks until the GPU has completed the transfer.
    void UploadToBuffer(VkBuffer Dst, uint64_t Offset, const void* Data, uint64_t Size);

    /// Copy a tightly-packed image (mip 0 / layer 0) into `Dst` at `Extent`,
    /// then generate `MipLevels - 1` lower mips by successive linear-filtered
    /// blits (mip i-1 -> i, halving). `Dst` ends up entirely in `FinalLayout`;
    /// the upload waits on the GPU. When `MipLevels == 1` this is a plain mip-0
    /// upload (no blits). The image MUST have been created with usage
    /// TRANSFER_SRC | TRANSFER_DST when `MipLevels > 1`.
    void UploadToImage(VkImage Dst, VkExtent3D Extent, uint32_t MipLevels,
                       VkImageLayout FinalLayout,
                       VkPipelineStageFlags2 FinalStage, VkAccessFlags2 FinalAccess,
                       const void* Data, uint64_t Size);

private:
    void EnsureStaging(uint64_t Size);
    VkCommandBuffer BeginOneShot();
    void EndAndSubmit(VkCommandBuffer Cmd);

    VkDevice m_device{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};

    VkCommandPool m_pool{VK_NULL_HANDLE};
    VkCommandBuffer m_cmd{VK_NULL_HANDLE};
    VkFence m_fence{VK_NULL_HANDLE};

    VkBuffer m_staging{VK_NULL_HANDLE};
    VmaAllocation m_stagingAlloc{VK_NULL_HANDLE};
    void* m_stagingPtr{nullptr};
    uint64_t m_stagingSize{0};
};

} // namespace helio::rhi::vulkan
