#include "VulkanBindless.h"

#include <Core/Logging/Log.h>

#include <array>

namespace helio::rhi::vulkan {

VulkanBindless::VulkanBindless(VkDevice Device)
    : m_sampledImageFree(MaxSampledImages)
    , m_storageImageFree(MaxStorageImages)
    , m_storageBufferFree(MaxStorageBuffers) {
    CreateSamplers(Device);
    CreateLayout(Device);
    CreatePoolAndSet(Device);
    WriteStaticSamplers(Device);
    HELIO_LOG_INFO("RHI", "Bindless set ready: sampled={} storageImg={} storageBuf={} samplers={}",
                   MaxSampledImages, MaxStorageImages, MaxStorageBuffers, SamplerCount);
}

VulkanBindless::~VulkanBindless() = default;

void VulkanBindless::Shutdown(VkDevice Device) {
    if (m_pool) {
        vkDestroyDescriptorPool(Device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    if (m_layout) {
        vkDestroyDescriptorSetLayout(Device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
    for (auto& S : m_samplers) {
        if (S) vkDestroySampler(Device, S, nullptr);
        S = VK_NULL_HANDLE;
    }
}

void VulkanBindless::CreateSamplers(VkDevice Device) {
    auto MakeSampler = [&](VkFilter Filter, VkSamplerAddressMode Mode) {
        VkSamplerCreateInfo CI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        CI.magFilter = Filter;
        CI.minFilter = Filter;
        CI.mipmapMode = (Filter == VK_FILTER_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                     : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        CI.addressModeU = Mode;
        CI.addressModeV = Mode;
        CI.addressModeW = Mode;
        CI.minLod = 0.0f;
        CI.maxLod = VK_LOD_CLAMP_NONE;
        CI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        VkSampler S = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSampler(Device, &CI, nullptr, &S));
        return S;
    };
    m_samplers[SamplerLinearClamp] = MakeSampler(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    m_samplers[SamplerLinearWrap]  = MakeSampler(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_REPEAT);
    m_samplers[SamplerPointClamp]  = MakeSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    m_samplers[SamplerPointWrap]   = MakeSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // Shadow comparison sampler: linear filter turns the hardware depth
    // compare into 2x2 PCF per tap. GREATER_OR_EQUAL matches reverse-Z
    // shadow maps (fragment is lit when its light-space depth is >= the
    // stored caster depth). Border black = "far plane" outside the frustum,
    // which the compare resolves to fully lit.
    {
        VkSamplerCreateInfo CI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        CI.magFilter = VK_FILTER_LINEAR;
        CI.minFilter = VK_FILTER_LINEAR;
        CI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        CI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        CI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        CI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        CI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        CI.compareEnable = VK_TRUE;
        CI.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        CI.minLod = 0.0f;
        CI.maxLod = VK_LOD_CLAMP_NONE;
        VK_CHECK(vkCreateSampler(Device, &CI, nullptr, &m_samplers[SamplerShadowLinear]));
    }
}

void VulkanBindless::CreateLayout(VkDevice Device) {
    std::array<VkDescriptorSetLayoutBinding, 5> Bindings{};
    Bindings[0] = { BindingSampledImages, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    MaxSampledImages, VK_SHADER_STAGE_ALL, nullptr };
    Bindings[1] = { BindingStorageImages, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    MaxStorageImages, VK_SHADER_STAGE_ALL, nullptr };
    Bindings[2] = { BindingStorageBuffers, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    MaxStorageBuffers, VK_SHADER_STAGE_ALL, nullptr };
    Bindings[3] = { BindingSamplers, VK_DESCRIPTOR_TYPE_SAMPLER,
                    SamplerCount, VK_SHADER_STAGE_ALL, nullptr };
    Bindings[4] = { BindingTLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                    1, VK_SHADER_STAGE_ALL, nullptr };

    constexpr VkDescriptorBindingFlags Flag =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    std::array<VkDescriptorBindingFlags, 5> Flags{ Flag, Flag, Flag, Flag, Flag };

    VkDescriptorSetLayoutBindingFlagsCreateInfo FlagsCI{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    FlagsCI.bindingCount = static_cast<uint32_t>(Flags.size());
    FlagsCI.pBindingFlags = Flags.data();

    VkDescriptorSetLayoutCreateInfo CI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    CI.pNext = &FlagsCI;
    CI.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    CI.bindingCount = static_cast<uint32_t>(Bindings.size());
    CI.pBindings = Bindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(Device, &CI, nullptr, &m_layout));
}

void VulkanBindless::CreatePoolAndSet(VkDevice Device) {
    std::array<VkDescriptorPoolSize, 5> Sizes{};
    Sizes[0] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,              MaxSampledImages };
    Sizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              MaxStorageImages };
    Sizes[2] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             MaxStorageBuffers };
    Sizes[3] = { VK_DESCRIPTOR_TYPE_SAMPLER,                    SamplerCount };
    Sizes[4] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 };

    VkDescriptorPoolCreateInfo PCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    PCI.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    PCI.maxSets = 1;
    PCI.poolSizeCount = static_cast<uint32_t>(Sizes.size());
    PCI.pPoolSizes = Sizes.data();
    VK_CHECK(vkCreateDescriptorPool(Device, &PCI, nullptr, &m_pool));

    VkDescriptorSetAllocateInfo AI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    AI.descriptorPool = m_pool;
    AI.descriptorSetCount = 1;
    AI.pSetLayouts = &m_layout;
    VK_CHECK(vkAllocateDescriptorSets(Device, &AI, &m_set));
}

void VulkanBindless::WriteStaticSamplers(VkDevice Device) {
    std::array<VkDescriptorImageInfo, SamplerCount> Infos{};
    std::array<VkWriteDescriptorSet, SamplerCount> Writes{};
    for (uint32_t I = 0; I < SamplerCount; ++I) {
        Infos[I].sampler = m_samplers[I];
        Writes[I] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        Writes[I].dstSet = m_set;
        Writes[I].dstBinding = BindingSamplers;
        Writes[I].dstArrayElement = I;
        Writes[I].descriptorCount = 1;
        Writes[I].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        Writes[I].pImageInfo = &Infos[I];
    }
    vkUpdateDescriptorSets(Device, SamplerCount, Writes.data(), 0, nullptr);
}

uint32_t VulkanBindless::AllocateSampledImage()   { return m_sampledImageFree.Allocate(); }
uint32_t VulkanBindless::AllocateStorageImage()   { return m_storageImageFree.Allocate(); }
uint32_t VulkanBindless::AllocateStorageBuffer()  { return m_storageBufferFree.Allocate(); }
void     VulkanBindless::FreeSampledImage(uint32_t S)  { m_sampledImageFree.Free(S); }
void     VulkanBindless::FreeStorageImage(uint32_t S)  { m_storageImageFree.Free(S); }
void     VulkanBindless::FreeStorageBuffer(uint32_t S) { m_storageBufferFree.Free(S); }

void VulkanBindless::WriteSampledImage(VkDevice Device, uint32_t Slot, VkImageView View, VkImageLayout Layout) {
    VkDescriptorImageInfo II{};
    II.imageView = View;
    II.imageLayout = Layout;
    VkWriteDescriptorSet W{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    W.dstSet = m_set;
    W.dstBinding = BindingSampledImages;
    W.dstArrayElement = Slot;
    W.descriptorCount = 1;
    W.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    W.pImageInfo = &II;
    vkUpdateDescriptorSets(Device, 1, &W, 0, nullptr);
}

void VulkanBindless::WriteStorageImage(VkDevice Device, uint32_t Slot, VkImageView View) {
    VkDescriptorImageInfo II{};
    II.imageView = View;
    II.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet W{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    W.dstSet = m_set;
    W.dstBinding = BindingStorageImages;
    W.dstArrayElement = Slot;
    W.descriptorCount = 1;
    W.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    W.pImageInfo = &II;
    vkUpdateDescriptorSets(Device, 1, &W, 0, nullptr);
}

void VulkanBindless::WriteStorageBuffer(VkDevice Device, uint32_t Slot, VkBuffer Buffer, uint64_t Offset, uint64_t Range) {
    VkDescriptorBufferInfo BI{};
    BI.buffer = Buffer;
    BI.offset = Offset;
    BI.range = Range;
    VkWriteDescriptorSet W{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    W.dstSet = m_set;
    W.dstBinding = BindingStorageBuffers;
    W.dstArrayElement = Slot;
    W.descriptorCount = 1;
    W.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    W.pBufferInfo = &BI;
    vkUpdateDescriptorSets(Device, 1, &W, 0, nullptr);
}

void VulkanBindless::WriteTLAS(VkDevice Device, VkAccelerationStructureKHR Tlas) {
    VkWriteDescriptorSetAccelerationStructureKHR AS{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    AS.accelerationStructureCount = 1;
    AS.pAccelerationStructures = &Tlas;

    VkWriteDescriptorSet W{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    W.pNext = &AS;
    W.dstSet = m_set;
    W.dstBinding = BindingTLAS;
    W.dstArrayElement = 0;
    W.descriptorCount = 1;
    W.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(Device, 1, &W, 0, nullptr);
}

VulkanBindless::Usage VulkanBindless::GetUsage() const {
    return {
        m_sampledImageFree.Capacity()  - m_sampledImageFree.FreeCount(),
        m_storageImageFree.Capacity()  - m_storageImageFree.FreeCount(),
        m_storageBufferFree.Capacity() - m_storageBufferFree.FreeCount(),
    };
}

} // namespace helio::rhi::vulkan
