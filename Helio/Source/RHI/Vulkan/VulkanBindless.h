/// @file VulkanBindless.h
/// @brief Single global descriptor set with UPDATE_AFTER_BIND slot management.
///
/// Layout (set 0):
///   binding 0: SAMPLED_IMAGE              [MaxSampledImages]
///   binding 1: STORAGE_IMAGE              [MaxStorageImages]
///   binding 2: STORAGE_BUFFER             [MaxStorageBuffers]
///   binding 3: SAMPLER                    [SamplerCount, static]
///   binding 4: ACCELERATION_STRUCTURE_KHR [1]
///
/// Each non-static binding has a freelist so allocation/free is O(1).
/// Slot writes happen via `WriteSampledImage` etc. — these are valid to call
/// while the set is bound (UPDATE_AFTER_BIND), as long as no in-flight frame
/// is currently *reading* the affected slot.
#pragma once

#include "VulkanCheck.h"
#include <Core/Handles/Handle.h>

#include <array>
#include <volk.h>

namespace helio::rhi::vulkan {

class VulkanBindless {
public:
    // Phase 5 caps. Easy to bump later if we hit them.
    static constexpr uint32_t MaxSampledImages   = 16'384;
    static constexpr uint32_t MaxStorageImages   = 4'096;
    static constexpr uint32_t MaxStorageBuffers  = 4'096;
    static constexpr uint32_t SamplerCount       = 4;

    /// Binding indices (must match `Shaders/Common/Bindless.slang`).
    static constexpr uint32_t BindingSampledImages = 0;
    static constexpr uint32_t BindingStorageImages = 1;
    static constexpr uint32_t BindingStorageBuffers = 2;
    static constexpr uint32_t BindingSamplers = 3;
    static constexpr uint32_t BindingTLAS = 4;

    /// Static sampler indices.
    enum SamplerSlot : uint32_t {
        SamplerLinearClamp = 0,
        SamplerLinearWrap  = 1,
        SamplerPointClamp  = 2,
        SamplerPointWrap   = 3,
    };

    VulkanBindless(VkDevice Device);
    ~VulkanBindless();

    void Shutdown(VkDevice Device);

    /// Allocate / free bindless slots. Returns UINT32_MAX on exhaustion.
    [[nodiscard]] uint32_t AllocateSampledImage();
    [[nodiscard]] uint32_t AllocateStorageImage();
    [[nodiscard]] uint32_t AllocateStorageBuffer();
    void FreeSampledImage(uint32_t Slot);
    void FreeStorageImage(uint32_t Slot);
    void FreeStorageBuffer(uint32_t Slot);

    /// Update descriptor entries. Safe to call between frames; with
    /// UPDATE_AFTER_BIND you can also call while the set is bound, provided
    /// no in-flight frame is reading the affected slot.
    void WriteSampledImage(VkDevice Device, uint32_t Slot, VkImageView View, VkImageLayout Layout);
    void WriteStorageImage(VkDevice Device, uint32_t Slot, VkImageView View);
    void WriteStorageBuffer(VkDevice Device, uint32_t Slot, VkBuffer Buffer, uint64_t Offset, uint64_t Range);
    void WriteTLAS(VkDevice Device, VkAccelerationStructureKHR Tlas);

    [[nodiscard]] VkDescriptorSetLayout GetLayout() const noexcept { return m_layout; }
    [[nodiscard]] VkDescriptorSet GetSet() const noexcept { return m_set; }

    struct Usage {
        uint32_t SampledImagesUsed;
        uint32_t StorageImagesUsed;
        uint32_t StorageBuffersUsed;
    };
    [[nodiscard]] Usage GetUsage() const;

private:
    void CreateSamplers(VkDevice Device);
    void CreateLayout(VkDevice Device);
    void CreatePoolAndSet(VkDevice Device);
    void WriteStaticSamplers(VkDevice Device);

    VkDescriptorSetLayout m_layout{VK_NULL_HANDLE};
    VkDescriptorPool m_pool{VK_NULL_HANDLE};
    VkDescriptorSet m_set{VK_NULL_HANDLE};

    std::array<VkSampler, SamplerCount> m_samplers{};

    core::Freelist m_sampledImageFree;
    core::Freelist m_storageImageFree;
    core::Freelist m_storageBufferFree;
};

} // namespace helio::rhi::vulkan
