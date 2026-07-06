#include "VulkanUploader.h"
#include "VulkanCheck.h"

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>

#include <cstring>

namespace helio::rhi::vulkan {

VulkanUploader::VulkanUploader(VkDevice Device, VmaAllocator Allocator, VkQueue Queue, uint32_t QueueFamily)
    : m_device(Device), m_allocator(Allocator), m_queue(Queue) {

    VkCommandPoolCreateInfo PCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    PCI.queueFamilyIndex = QueueFamily;
    PCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(m_device, &PCI, nullptr, &m_pool));

    VkCommandBufferAllocateInfo BAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    BAI.commandPool = m_pool;
    BAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    BAI.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &BAI, &m_cmd));

    VkFenceCreateInfo FCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(m_device, &FCI, nullptr, &m_fence));
}

void VulkanUploader::Shutdown() {
    if (m_fence) { vkDestroyFence(m_device, m_fence, nullptr); m_fence = VK_NULL_HANDLE; }
    if (m_pool)  { vkDestroyCommandPool(m_device, m_pool, nullptr); m_pool = VK_NULL_HANDLE; m_cmd = VK_NULL_HANDLE; }
    if (m_staging) {
        vmaDestroyBuffer(m_allocator, m_staging, m_stagingAlloc);
        m_staging = VK_NULL_HANDLE;
        m_stagingAlloc = VK_NULL_HANDLE;
        m_stagingPtr = nullptr;
        m_stagingSize = 0;
    }
}

void VulkanUploader::EnsureStaging(uint64_t Size) {
    if (Size <= m_stagingSize) return;

    if (m_staging) {
        vmaDestroyBuffer(m_allocator, m_staging, m_stagingAlloc);
        m_staging = VK_NULL_HANDLE;
        m_stagingAlloc = VK_NULL_HANDLE;
        m_stagingPtr = nullptr;
    }

    // Round up to 1MB granularity to amortize reallocation churn.
    uint64_t New = ((Size + (1u << 20) - 1) >> 20) << 20;

    VkBufferCreateInfo BCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    BCI.size = New;
    BCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    BCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ACI{};
    ACI.usage = VMA_MEMORY_USAGE_AUTO;
    ACI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo Info{};
    VK_CHECK(vmaCreateBuffer(m_allocator, &BCI, &ACI, &m_staging, &m_stagingAlloc, &Info));
    m_stagingPtr = Info.pMappedData;
    m_stagingSize = New;
}

VkCommandBuffer VulkanUploader::BeginOneShot() {
    VK_CHECK(vkResetCommandBuffer(m_cmd, 0));
    VkCommandBufferBeginInfo BI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    BI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(m_cmd, &BI));
    return m_cmd;
}

void VulkanUploader::EndAndSubmit(VkCommandBuffer Cmd) {
    VK_CHECK(vkEndCommandBuffer(Cmd));

    VkCommandBufferSubmitInfo CSI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    CSI.commandBuffer = Cmd;
    VkSubmitInfo2 Submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    Submit.commandBufferInfoCount = 1;
    Submit.pCommandBufferInfos = &CSI;

    VK_CHECK(vkResetFences(m_device, 1, &m_fence));
    VK_CHECK(vkQueueSubmit2(m_queue, 1, &Submit, m_fence));
    VK_CHECK(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX));
}

void VulkanUploader::UploadToBuffer(VkBuffer Dst, uint64_t Offset, const void* Data, uint64_t Size) {
    if (Size == 0 || !Data) return;
    EnsureStaging(Size);
    std::memcpy(m_stagingPtr, Data, Size);

    auto Cmd = BeginOneShot();
    VkBufferCopy Region{};
    Region.srcOffset = 0;
    Region.dstOffset = Offset;
    Region.size = Size;
    vkCmdCopyBuffer(Cmd, m_staging, Dst, 1, &Region);
    EndAndSubmit(Cmd);
}

void VulkanUploader::UploadToImage(VkImage Dst, VkExtent3D Extent, uint32_t MipLevels,
                                   VkImageLayout FinalLayout,
                                   VkPipelineStageFlags2 FinalStage, VkAccessFlags2 FinalAccess,
                                   const void* Data, uint64_t Size) {
    if (Size == 0 || !Data) return;
    if (MipLevels == 0) MipLevels = 1;
    EnsureStaging(Size);
    std::memcpy(m_stagingPtr, Data, Size);

    auto Cmd = BeginOneShot();

    // Single-mip barrier helper (targets one mip level).
    auto Barrier = [&](uint32_t Mip, VkImageLayout Old, VkImageLayout New,
                       VkPipelineStageFlags2 SrcStage, VkAccessFlags2 SrcAccess,
                       VkPipelineStageFlags2 DstStage, VkAccessFlags2 DstAccess) {
        VkImageMemoryBarrier2 B{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        B.srcStageMask = SrcStage;  B.srcAccessMask = SrcAccess;
        B.dstStageMask = DstStage;  B.dstAccessMask = DstAccess;
        B.oldLayout = Old;          B.newLayout = New;
        B.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.image = Dst;
        B.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, Mip, 1, 0, 1 };
        VkDependencyInfo D{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        D.imageMemoryBarrierCount = 1;
        D.pImageMemoryBarriers = &B;
        vkCmdPipelineBarrier2(Cmd, &D);
    };

    // 1) All mips UNDEFINED -> TRANSFER_DST (mip 0 for the copy; lower mips are
    //    blit targets and get overwritten before they're read).
    {
        VkImageMemoryBarrier2 B{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        B.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT; B.srcAccessMask = 0;
        B.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;        B.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        B.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;              B.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        B.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;      B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.image = Dst;
        B.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, MipLevels, 0, 1 };
        VkDependencyInfo D{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        D.imageMemoryBarrierCount = 1;
        D.pImageMemoryBarriers = &B;
        vkCmdPipelineBarrier2(Cmd, &D);
    }

    // 2) Copy the source pixels into mip 0.
    VkBufferImageCopy Region{};
    Region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    Region.imageOffset = { 0, 0, 0 };
    Region.imageExtent = Extent;
    vkCmdCopyBufferToImage(Cmd, m_staging, Dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

    // 3) Generate the mip chain: blit each level down to the next, halving.
    int32_t MipW = static_cast<int32_t>(Extent.width);
    int32_t MipH = static_cast<int32_t>(Extent.height);
    for (uint32_t I = 1; I < MipLevels; ++I) {
        // Source mip (I-1): TRANSFER_DST -> TRANSFER_SRC so we can read it.
        Barrier(I - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        const int32_t DstW = MipW > 1 ? MipW / 2 : 1;
        const int32_t DstH = MipH > 1 ? MipH / 2 : 1;

        VkImageBlit Blit{};
        Blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, I - 1, 0, 1 };
        Blit.srcOffsets[0] = { 0, 0, 0 };
        Blit.srcOffsets[1] = { MipW, MipH, 1 };
        Blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, I, 0, 1 };
        Blit.dstOffsets[0] = { 0, 0, 0 };
        Blit.dstOffsets[1] = { DstW, DstH, 1 };
        vkCmdBlitImage(Cmd,
                       Dst, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       Dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &Blit, VK_FILTER_LINEAR);

        // Source mip (I-1) is done — move it straight to the final layout.
        Barrier(I - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, FinalLayout,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                FinalStage, FinalAccess);

        MipW = DstW;
        MipH = DstH;
    }

    // 4) The last mip is still TRANSFER_DST (copy target for the smallest
    //    level, or mip 0 itself when MipLevels == 1) — transition it too.
    Barrier(MipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, FinalLayout,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            FinalStage, FinalAccess);

    EndAndSubmit(Cmd);
}

} // namespace helio::rhi::vulkan
