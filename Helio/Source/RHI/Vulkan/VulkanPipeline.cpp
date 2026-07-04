#include "VulkanPipeline.h"
#include "VulkanBindless.h"
#include "VulkanShaderCache.h"
#include "VulkanCheck.h"
#include "VulkanFormats.h"

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace helio::rhi::vulkan {

namespace {

VkPolygonMode kFill = VK_POLYGON_MODE_FILL;

VkPrimitiveTopology ToVk(PrimitiveTopology T) {
    switch (T) {
        case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags ToVk(CullMode C) {
    switch (C) {
        case CullMode::None:  return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkCompareOp ToVk(CompareOp C) {
    switch (C) {
        case CompareOp::Never:     return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:      return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:     return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEq:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:   return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEq:     return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterEq: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:    return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

void SetObjectName(VkDevice Dev, VkObjectType Type, uint64_t Handle, const char* Name) {
    if (!Name || !vkSetDebugUtilsObjectNameEXT) return;
    VkDebugUtilsObjectNameInfoEXT N{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    N.objectType = Type;
    N.objectHandle = Handle;
    N.pObjectName = Name;
    vkSetDebugUtilsObjectNameEXT(Dev, &N);
}

} // namespace

VulkanPipelineFactory::VulkanPipelineFactory(VkDevice Device, VulkanBindless& Bindless, VulkanShaderCache& Cache)
    : m_device(Device), m_bindless(&Bindless), m_shaderCache(&Cache) {
    LoadOrCreatePSOCache();
}

void VulkanPipelineFactory::Shutdown() {
    SavePSOCache();
    if (m_psoCache) {
        vkDestroyPipelineCache(m_device, m_psoCache, nullptr);
        m_psoCache = VK_NULL_HANDLE;
    }
    for (auto& [_, L] : m_layouts) {
        if (L) vkDestroyPipelineLayout(m_device, L, nullptr);
    }
    m_layouts.clear();
}

void VulkanPipelineFactory::LoadOrCreatePSOCache() {
    // PSO cache lives next to the binary so a re-launch picks it up.
    m_psoCachePath = "PipelineCache.bin";

    std::vector<char> InitialData;
    std::ifstream In(m_psoCachePath, std::ios::binary | std::ios::ate);
    if (In) {
        auto Size = static_cast<std::streamsize>(In.tellg());
        if (Size > 0) {
            InitialData.resize(static_cast<size_t>(Size));
            In.seekg(0);
            In.read(InitialData.data(), Size);
        }
    }

    VkPipelineCacheCreateInfo CI{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    CI.initialDataSize = InitialData.size();
    CI.pInitialData = InitialData.empty() ? nullptr : InitialData.data();
    VK_CHECK(vkCreatePipelineCache(m_device, &CI, nullptr, &m_psoCache));
    HELIO_LOG_INFO("RHI", "PSO cache loaded ({} bytes from '{}')", InitialData.size(), m_psoCachePath);
}

void VulkanPipelineFactory::SavePSOCache() {
    if (!m_psoCache) return;
    size_t Size = 0;
    vkGetPipelineCacheData(m_device, m_psoCache, &Size, nullptr);
    if (Size == 0) return;
    std::vector<char> Data(Size);
    vkGetPipelineCacheData(m_device, m_psoCache, &Size, Data.data());
    std::ofstream Out(m_psoCachePath, std::ios::binary | std::ios::trunc);
    if (Out) {
        Out.write(Data.data(), static_cast<std::streamsize>(Size));
        HELIO_LOG_INFO("RHI", "PSO cache saved ({} bytes -> '{}')", Size, m_psoCachePath);
    }
}

VkPipelineLayout VulkanPipelineFactory::GetOrCreateLayout(uint32_t PushBytes) {
    if (auto It = m_layouts.find(PushBytes); It != m_layouts.end()) {
        return It->second;
    }

    VkPushConstantRange Range{};
    Range.stageFlags = VK_SHADER_STAGE_ALL;
    Range.offset = 0;
    Range.size = PushBytes;

    VkDescriptorSetLayout DSLayout = m_bindless->GetLayout();
    VkPipelineLayoutCreateInfo CI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    CI.setLayoutCount = 1;
    CI.pSetLayouts = &DSLayout;
    if (PushBytes > 0) {
        CI.pushConstantRangeCount = 1;
        CI.pPushConstantRanges = &Range;
    }

    VkPipelineLayout L = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(m_device, &CI, nullptr, &L));
    m_layouts.emplace(PushBytes, L);
    return L;
}

VulkanPipeline VulkanPipelineFactory::CreateGraphics(const GraphicsPipelineDesc& Desc) {
    HELIO_CHECK(Desc.ShaderPath);
    // At least one attachment (color OR depth) must be bound — zero would
    // produce a no-op pipeline. Depth-only is valid (e.g. shadow maps).
    HELIO_CHECK(Desc.ColorAttachmentCount > 0 || Desc.DepthFormat != Format::Undefined);

    auto Module = m_shaderCache->Load(Desc.ShaderPath);
    HELIO_CHECK(Module != VK_NULL_HANDLE);

    std::array<VkPipelineShaderStageCreateInfo, 2> Stages{};
    Stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = Module;
    Stages[0].pName = Desc.VertexEntry;
    Stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = Module;
    Stages[1].pName = Desc.FragmentEntry;

    VkPipelineVertexInputStateCreateInfo Vtx{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo IA{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    IA.topology = ToVk(Desc.Topology);

    VkPipelineViewportStateCreateInfo VP{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VP.viewportCount = 1;
    VP.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo Rast{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    Rast.polygonMode = kFill;
    Rast.cullMode = ToVk(Desc.Cull);
    Rast.frontFace = (Desc.Front == FrontFace::Clockwise)
                       ? VK_FRONT_FACE_CLOCKWISE
                       : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rast.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo MS{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    MS.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DS{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    DS.depthTestEnable = Desc.DepthTest;
    DS.depthWriteEnable = Desc.DepthWrite;
    DS.depthCompareOp = ToVk(Desc.DepthCompare);

    std::array<VkPipelineColorBlendAttachmentState, 8> Att{};
    for (uint32_t I = 0; I < Desc.ColorAttachmentCount; ++I) {
        Att[I].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo CB{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    CB.attachmentCount = Desc.ColorAttachmentCount;
    CB.pAttachments = Att.data();

    std::array<VkDynamicState, 2> DynStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo Dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    Dyn.dynamicStateCount = static_cast<uint32_t>(DynStates.size());
    Dyn.pDynamicStates = DynStates.data();

    std::array<VkFormat, 8> ColorFmts{};
    for (uint32_t I = 0; I < Desc.ColorAttachmentCount; ++I) {
        ColorFmts[I] = ToVk(Desc.ColorFormats[I]);
    }
    VkPipelineRenderingCreateInfo Render{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    Render.colorAttachmentCount = Desc.ColorAttachmentCount;
    Render.pColorAttachmentFormats = ColorFmts.data();
    Render.depthAttachmentFormat = ToVk(Desc.DepthFormat);

    VulkanPipeline P{};
    P.Layout = GetOrCreateLayout(Desc.PushConstantBytes);
    P.Kind = VulkanPipelineKind::Graphics;
    P.BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    P.PushConstantBytes = Desc.PushConstantBytes;

    VkGraphicsPipelineCreateInfo CI{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    CI.pNext = &Render;
    CI.stageCount = static_cast<uint32_t>(Stages.size());
    CI.pStages = Stages.data();
    CI.pVertexInputState = &Vtx;
    CI.pInputAssemblyState = &IA;
    CI.pViewportState = &VP;
    CI.pRasterizationState = &Rast;
    CI.pMultisampleState = &MS;
    CI.pDepthStencilState = &DS;
    CI.pColorBlendState = &CB;
    CI.pDynamicState = &Dyn;
    CI.layout = P.Layout;

    VK_CHECK(vkCreateGraphicsPipelines(m_device, m_psoCache, 1, &CI, nullptr, &P.Pipeline));
    SetObjectName(m_device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(P.Pipeline), Desc.DebugName);
    HELIO_LOG_INFO("RHI", "Graphics pipeline created from '{}' (entry vs='{}' fs='{}')",
                   Desc.ShaderPath, Desc.VertexEntry, Desc.FragmentEntry);
    return P;
}

VulkanPipeline VulkanPipelineFactory::CreateCompute(const ComputePipelineDesc& Desc) {
    HELIO_CHECK(Desc.ShaderPath);
    auto Module = m_shaderCache->Load(Desc.ShaderPath);
    HELIO_CHECK(Module != VK_NULL_HANDLE);

    VkPipelineShaderStageCreateInfo Stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    Stage.module = Module;
    Stage.pName = Desc.Entry;

    VulkanPipeline P{};
    P.Layout = GetOrCreateLayout(Desc.PushConstantBytes);
    P.Kind = VulkanPipelineKind::Compute;
    P.BindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    P.PushConstantBytes = Desc.PushConstantBytes;

    VkComputePipelineCreateInfo CI{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    CI.stage = Stage;
    CI.layout = P.Layout;

    VK_CHECK(vkCreateComputePipelines(m_device, m_psoCache, 1, &CI, nullptr, &P.Pipeline));
    SetObjectName(m_device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(P.Pipeline), Desc.DebugName);
    HELIO_LOG_INFO("RHI", "Compute pipeline created from '{}' (entry='{}')", Desc.ShaderPath, Desc.Entry);
    return P;
}

} // namespace helio::rhi::vulkan
