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

void VulkanUploader::UploadToImage(VkImage Dst, VkExtent3D Extent, VkImageLayout FinalLayout,
                                   VkPipelineStageFlags2 FinalStage, VkAccessFlags2 FinalAccess,
                                   const void* Data, uint64_t Size) {
    if (Size == 0 || !Data) return;
    EnsureStaging(Size);
    std::memcpy(m_stagingPtr, Data, Size);

    auto Cmd = BeginOneShot();

    auto MakeBarrier = [&](VkImageLayout Old, VkImageLayout New,
                           VkPipelineStageFlags2 SrcStage, VkAccessFlags2 SrcAccess,
                           VkPipelineStageFlags2 DstStage, VkAccessFlags2 DstAccess) {
        VkImageMemoryBarrier2 B{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        B.srcStageMask = SrcStage;  B.srcAccessMask = SrcAccess;
        B.dstStageMask = DstStage;  B.dstAccessMask = DstAccess;
        B.oldLayout = Old;          B.newLayout = New;
        B.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.image = Dst;
        B.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        return B;
    };

    auto B1 = MakeBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkDependencyInfo D1{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    D1.imageMemoryBarrierCount = 1;
    D1.pImageMemoryBarriers = &B1;
    vkCmdPipelineBarrier2(Cmd, &D1);

    VkBufferImageCopy Region{};
    Region.bufferOffset = 0;
    Region.bufferRowLength = 0;
    Region.bufferImageHeight = 0;
    Region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    Region.imageOffset = { 0, 0, 0 };
    Region.imageExtent = Extent;
    vkCmdCopyBufferToImage(Cmd, m_staging, Dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

    auto B2 = MakeBarrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, FinalLayout,
                          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          FinalStage, FinalAccess);
    VkDependencyInfo D2{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    D2.imageMemoryBarrierCount = 1;
    D2.pImageMemoryBarriers = &B2;
    vkCmdPipelineBarrier2(Cmd, &D2);

    EndAndSubmit(Cmd);
}

} // namespace helio::rhi::vulkan
