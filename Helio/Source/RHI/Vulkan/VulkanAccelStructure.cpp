#include "VulkanAccelStructure.h"
#include "VulkanContext.h"
#include "VulkanFormats.h"

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>

#include <cstring>

namespace helio::rhi::vulkan {

VulkanAccelBuilder::VulkanAccelBuilder(VulkanContext& Ctx) : m_ctx(&Ctx) {
    VkCommandPoolCreateInfo PCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    PCI.queueFamilyIndex = m_ctx->GetGraphicsQueueFamily();
    PCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(m_ctx->GetDevice(), &PCI, nullptr, &m_pool));

    VkCommandBufferAllocateInfo BAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    BAI.commandPool = m_pool;
    BAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    BAI.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(m_ctx->GetDevice(), &BAI, &m_cmd));

    VkFenceCreateInfo FCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(m_ctx->GetDevice(), &FCI, nullptr, &m_fence));
}

void VulkanAccelBuilder::Shutdown() {
    if (m_fence) { vkDestroyFence(m_ctx->GetDevice(), m_fence, nullptr); m_fence = VK_NULL_HANDLE; }
    if (m_pool)  { vkDestroyCommandPool(m_ctx->GetDevice(), m_pool, nullptr); m_pool = VK_NULL_HANDLE; m_cmd = VK_NULL_HANDLE; }
}

VkDeviceAddress VulkanAccelBuilder::GetBufferDeviceAddress(VkBuffer Buf) const {
    VkBufferDeviceAddressInfo I{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    I.buffer = Buf;
    return vkGetBufferDeviceAddress(m_ctx->GetDevice(), &I);
}

void VulkanAccelBuilder::AllocateAccelStorage(uint64_t Size, VkBufferUsageFlags Usage,
                                              VkBuffer& OutBuf, VmaAllocation& OutAlloc) {
    VkBufferCreateInfo BCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    BCI.size = Size;
    BCI.usage = Usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    BCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ACI{};
    ACI.usage = VMA_MEMORY_USAGE_AUTO;
    VK_CHECK(vmaCreateBuffer(m_ctx->GetAllocator(), &BCI, &ACI, &OutBuf, &OutAlloc, nullptr));
}

void VulkanAccelBuilder::AllocateScratch(uint64_t Size, VkBuffer& OutBuf, VmaAllocation& OutAlloc,
                                         VkDeviceAddress& OutAddr) {
    VkBufferCreateInfo BCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    BCI.size = Size;
    BCI.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    BCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ACI{};
    ACI.usage = VMA_MEMORY_USAGE_AUTO;
    VK_CHECK(vmaCreateBuffer(m_ctx->GetAllocator(), &BCI, &ACI, &OutBuf, &OutAlloc, nullptr));
    OutAddr = GetBufferDeviceAddress(OutBuf);
}

void VulkanAccelBuilder::ImmediateSubmit(VkCommandBuffer& Cmd) {
    VK_CHECK(vkEndCommandBuffer(Cmd));
    VkCommandBufferSubmitInfo CSI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    CSI.commandBuffer = Cmd;
    VkSubmitInfo2 Submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    Submit.commandBufferInfoCount = 1;
    Submit.pCommandBufferInfos = &CSI;
    VK_CHECK(vkResetFences(m_ctx->GetDevice(), 1, &m_fence));
    VK_CHECK(vkQueueSubmit2(m_ctx->GetGraphicsQueue(), 1, &Submit, m_fence));
    VK_CHECK(vkWaitForFences(m_ctx->GetDevice(), 1, &m_fence, VK_TRUE, UINT64_MAX));
}

// =============================================================================
// BLAS build
// =============================================================================
VulkanBLAS VulkanAccelBuilder::BuildBLAS(const BLASDesc& Desc) {
    HELIO_CHECK(Desc.GeometryCount > 0 && Desc.Geometries);
    HELIO_CHECK(m_ctx->HasRayTracing());

    auto& Pool = m_ctx->GetResourcePool();

    std::vector<VkAccelerationStructureGeometryKHR> Geoms(Desc.GeometryCount);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> Ranges(Desc.GeometryCount);
    std::vector<uint32_t> MaxPrimCounts(Desc.GeometryCount);

    for (uint32_t I = 0; I < Desc.GeometryCount; ++I) {
        const auto& G = Desc.Geometries[I];
        auto* VBuf = Pool.GetBuffer(G.Vertices.Id);
        HELIO_CHECK(VBuf);
        VkBuffer IBufRaw = VK_NULL_HANDLE;
        if (G.Indices.IsValid()) {
            auto* IBuf = Pool.GetBuffer(G.Indices.Id);
            HELIO_CHECK(IBuf);
            IBufRaw = IBuf->Buffer;
        }

        VkAccelerationStructureGeometryTrianglesDataKHR Tri{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        Tri.vertexFormat = ToVk(G.VertexFormat);
        Tri.vertexData.deviceAddress = GetBufferDeviceAddress(VBuf->Buffer) + G.VertexBufferOffset;
        Tri.vertexStride = G.VertexStride;
        Tri.maxVertex = G.VertexCount - 1;
        if (G.Indices.IsValid()) {
            Tri.indexType = (G.IndexFormat == 0) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
            Tri.indexData.deviceAddress = GetBufferDeviceAddress(IBufRaw) + G.IndexBufferOffset;
        } else {
            Tri.indexType = VK_INDEX_TYPE_NONE_KHR;
        }

        Geoms[I] = VkAccelerationStructureGeometryKHR{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        Geoms[I].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        Geoms[I].flags = G.Opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0u;
        Geoms[I].geometry.triangles = Tri;

        uint32_t Tris = G.Indices.IsValid() ? (G.IndexCount / 3) : (G.VertexCount / 3);
        Ranges[I].primitiveCount = Tris;
        Ranges[I].primitiveOffset = 0;
        Ranges[I].firstVertex = 0;
        Ranges[I].transformOffset = 0;
        MaxPrimCounts[I] = Tris;
    }

    VkAccelerationStructureBuildGeometryInfoKHR BuildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    BuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    BuildInfo.flags = Desc.PreferFastTrace
        ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
        : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    BuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    BuildInfo.geometryCount = Desc.GeometryCount;
    BuildInfo.pGeometries = Geoms.data();

    VkAccelerationStructureBuildSizesInfoKHR Sizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(m_ctx->GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &BuildInfo, MaxPrimCounts.data(), &Sizes);

    VulkanBLAS Out{};
    Out.SizeBytes = Sizes.accelerationStructureSize;
    AllocateAccelStorage(Sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        Out.Storage, Out.StorageAlloc);

    VkAccelerationStructureCreateInfoKHR CI{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    CI.buffer = Out.Storage;
    CI.size = Sizes.accelerationStructureSize;
    CI.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VK_CHECK(vkCreateAccelerationStructureKHR(m_ctx->GetDevice(), &CI, nullptr, &Out.Accel));

    // Scratch
    VkBuffer Scratch = VK_NULL_HANDLE;
    VmaAllocation ScratchAlloc = VK_NULL_HANDLE;
    VkDeviceAddress ScratchAddr = 0;
    AllocateScratch(Sizes.buildScratchSize, Scratch, ScratchAlloc, ScratchAddr);

    BuildInfo.dstAccelerationStructure = Out.Accel;
    BuildInfo.scratchData.deviceAddress = ScratchAddr;

    // Record + submit
    VK_CHECK(vkResetCommandBuffer(m_cmd, 0));
    VkCommandBufferBeginInfo BI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    BI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(m_cmd, &BI));

    const VkAccelerationStructureBuildRangeInfoKHR* RangePtr = Ranges.data();
    vkCmdBuildAccelerationStructuresKHR(m_cmd, 1, &BuildInfo, &RangePtr);

    ImmediateSubmit(m_cmd);

    // Free scratch
    vmaDestroyBuffer(m_ctx->GetAllocator(), Scratch, ScratchAlloc);

    // Cache device address
    VkAccelerationStructureDeviceAddressInfoKHR DA{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    DA.accelerationStructure = Out.Accel;
    Out.DeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_ctx->GetDevice(), &DA);

    if (Desc.DebugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT N{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        N.objectType = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
        N.objectHandle = reinterpret_cast<uint64_t>(Out.Accel);
        N.pObjectName = Desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_ctx->GetDevice(), &N);
    }

    HELIO_LOG_INFO("RHI", "BLAS built: '{}' size={}B addr={:#x}",
                   Desc.DebugName ? Desc.DebugName : "", Out.SizeBytes,
                   static_cast<uint64_t>(Out.DeviceAddress));
    return Out;
}

// =============================================================================
// TLAS build
// =============================================================================
VulkanTLAS VulkanAccelBuilder::BuildTLAS(const TLASDesc& Desc,
                                        const std::vector<VkDeviceAddress>& BLASAddresses) {
    HELIO_CHECK(Desc.InstanceCount > 0 && Desc.Instances);
    HELIO_CHECK(BLASAddresses.size() == Desc.InstanceCount);
    HELIO_CHECK(m_ctx->HasRayTracing());

    // Build the instance-buffer payload on a host-visible buffer.
    std::vector<VkAccelerationStructureInstanceKHR> InstancesCPU(Desc.InstanceCount);
    for (uint32_t I = 0; I < Desc.InstanceCount; ++I) {
        const auto& Src = Desc.Instances[I];
        auto& Dst = InstancesCPU[I];
        std::memcpy(&Dst.transform.matrix[0][0], Src.Transform, 12 * sizeof(float));
        Dst.instanceCustomIndex = Src.CustomIndex & 0xFFFFFFu;
        Dst.mask = Src.Mask & 0xFFu;
        Dst.instanceShaderBindingTableRecordOffset = Src.SBTOffset & 0xFFFFFFu;
        Dst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        Dst.accelerationStructureReference = BLASAddresses[I];
    }
    const VkDeviceSize InstancesBytes = sizeof(VkAccelerationStructureInstanceKHR) * Desc.InstanceCount;

    VulkanTLAS Out{};
    Out.InstanceCount = Desc.InstanceCount;

    // Instance buffer — host-upload memory, then it'll be referenced by the build.
    VkBufferCreateInfo IBCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    IBCI.size = InstancesBytes;
    IBCI.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    IBCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo IACI{};
    IACI.usage = VMA_MEMORY_USAGE_AUTO;
    IACI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
               | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo InstInfo{};
    VK_CHECK(vmaCreateBuffer(m_ctx->GetAllocator(), &IBCI, &IACI,
                              &Out.InstanceBuf, &Out.InstanceAlloc, &InstInfo));
    std::memcpy(InstInfo.pMappedData, InstancesCPU.data(), InstancesBytes);
    VkDeviceAddress InstAddr = GetBufferDeviceAddress(Out.InstanceBuf);

    VkAccelerationStructureGeometryInstancesDataKHR InstGeom{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    InstGeom.arrayOfPointers = VK_FALSE;
    InstGeom.data.deviceAddress = InstAddr;

    VkAccelerationStructureGeometryKHR Geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    Geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    Geom.geometry.instances = InstGeom;

    VkAccelerationStructureBuildGeometryInfoKHR BuildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    BuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    BuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (Desc.AllowUpdate) BuildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    BuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    BuildInfo.geometryCount = 1;
    BuildInfo.pGeometries = &Geom;

    VkAccelerationStructureBuildSizesInfoKHR Sizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    uint32_t Prim = Desc.InstanceCount;
    vkGetAccelerationStructureBuildSizesKHR(m_ctx->GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &BuildInfo, &Prim, &Sizes);

    Out.SizeBytes = Sizes.accelerationStructureSize;
    AllocateAccelStorage(Sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        Out.Storage, Out.StorageAlloc);

    VkAccelerationStructureCreateInfoKHR CI{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    CI.buffer = Out.Storage;
    CI.size = Sizes.accelerationStructureSize;
    CI.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    VK_CHECK(vkCreateAccelerationStructureKHR(m_ctx->GetDevice(), &CI, nullptr, &Out.Accel));

    VkBuffer Scratch = VK_NULL_HANDLE;
    VmaAllocation ScratchAlloc = VK_NULL_HANDLE;
    VkDeviceAddress ScratchAddr = 0;
    AllocateScratch(Sizes.buildScratchSize, Scratch, ScratchAlloc, ScratchAddr);

    BuildInfo.dstAccelerationStructure = Out.Accel;
    BuildInfo.scratchData.deviceAddress = ScratchAddr;

    VK_CHECK(vkResetCommandBuffer(m_cmd, 0));
    VkCommandBufferBeginInfo BI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    BI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(m_cmd, &BI));

    VkAccelerationStructureBuildRangeInfoKHR Range{};
    Range.primitiveCount = Desc.InstanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* RangePtr = &Range;
    vkCmdBuildAccelerationStructuresKHR(m_cmd, 1, &BuildInfo, &RangePtr);

    ImmediateSubmit(m_cmd);

    vmaDestroyBuffer(m_ctx->GetAllocator(), Scratch, ScratchAlloc);

    VkAccelerationStructureDeviceAddressInfoKHR DA{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    DA.accelerationStructure = Out.Accel;
    Out.DeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_ctx->GetDevice(), &DA);

    if (Desc.DebugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT N{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        N.objectType = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
        N.objectHandle = reinterpret_cast<uint64_t>(Out.Accel);
        N.pObjectName = Desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_ctx->GetDevice(), &N);
    }

    HELIO_LOG_INFO("RHI", "TLAS built: '{}' instances={} size={}B",
                   Desc.DebugName ? Desc.DebugName : "", Desc.InstanceCount, Out.SizeBytes);
    return Out;
}

} // namespace helio::rhi::vulkan
