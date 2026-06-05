#include "MeshPipeline.h"

#include <RHI/Public/Device.h>

#include <Core/Logging/Log.h>

namespace helio::resource {

rhi::PipelineHandle CreateMeshInstancedPipeline(
    rhi::Device& Dev, const MeshInstancedPipelineDesc& Desc) {

    rhi::GraphicsPipelineDesc Pd{};
    Pd.ShaderPath           = "Shaders/Passes/MeshInstanced.spv";
    Pd.VertexEntry          = "VSMain";
    Pd.FragmentEntry        = "PSMain";
    Pd.ColorFormats[0]      = Desc.ColorFormat;
    Pd.ColorAttachmentCount = 1;
    Pd.DepthFormat          = Desc.DepthFormat;
    Pd.Topology             = rhi::PrimitiveTopology::TriangleList;
    Pd.Cull                 = Desc.Cull;
    Pd.Front                = Desc.Front;
    Pd.DepthTest            = Desc.DepthTest;
    Pd.DepthWrite           = Desc.DepthWrite;
    Pd.DepthCompare         = Desc.DepthCompare;
    Pd.PushConstantBytes    = sizeof(MeshInstancedPushConsts);
    Pd.DebugName            = Desc.DebugName;

    rhi::PipelineHandle H = Dev.CreateGraphicsPipeline(Pd);
    if (!H.IsValid()) {
        HELIO_LOG_WARN("Resource", "CreateMeshInstancedPipeline: shader load failed " "(expected '{}')", Pd.ShaderPath);
    }
    return H;
}

} // namespace helio::resource
