/// @file VulkanPipeline.h
/// @brief Internal Pipeline POD + creation helpers.
///
/// Every Helio pipeline uses the same VkPipelineLayout: the bindless set 0
/// + a single push-constant range (default 128 bytes). Pipeline creation
/// uses dynamic rendering (no VkRenderPass) and dynamic viewport/scissor.
#pragma once

#include <RHI/Public/Pipeline.h>

#include <volk.h>

#include <string>
#include <unordered_map>

namespace helio::rhi::vulkan {

class VulkanBindless;
class VulkanShaderCache;

enum class VulkanPipelineKind : uint8_t { Graphics, Compute };

struct VulkanPipeline {
    VkPipeline Pipeline{VK_NULL_HANDLE};
    VkPipelineLayout Layout{VK_NULL_HANDLE};
    VulkanPipelineKind Kind{VulkanPipelineKind::Graphics};
    VkPipelineBindPoint BindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
    uint32_t PushConstantBytes{0};
};

class VulkanPipelineFactory {
public:
    VulkanPipelineFactory(VkDevice Device, VulkanBindless& Bindless, VulkanShaderCache& Cache);
    void Shutdown();

    [[nodiscard]] VulkanPipeline CreateGraphics(const GraphicsPipelineDesc& Desc);
    [[nodiscard]] VulkanPipeline CreateCompute(const ComputePipelineDesc& Desc);

    [[nodiscard]] VkPipelineCache GetCache() const noexcept { return m_psoCache; }

private:
    /// Layouts are de-duplicated by push-constant size. Phase 6 only varies
    /// that dimension; future descriptor-layout customization plugs in here.
    [[nodiscard]] VkPipelineLayout GetOrCreateLayout(uint32_t PushBytes);

    void LoadOrCreatePSOCache();
    void SavePSOCache();

    VkDevice m_device{VK_NULL_HANDLE};
    VulkanBindless* m_bindless{nullptr};
    VulkanShaderCache* m_shaderCache{nullptr};

    std::unordered_map<uint32_t, VkPipelineLayout> m_layouts;
    VkPipelineCache m_psoCache{VK_NULL_HANDLE};
    std::string m_psoCachePath;
};

} // namespace helio::rhi::vulkan
