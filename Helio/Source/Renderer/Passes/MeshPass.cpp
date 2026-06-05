#include "MeshPass.h"

namespace helio::render::passes
{
    MeshPass::MeshPass(rhi::Device& RHI, rhi::Format ColorFormat, rhi::Format DepthFormat)
    {
    }

    void MeshPass::Execute(render::RenderGraph& rg,
                           rhi::TextureHandle colorTarget,
                           rhi::TextureHandle depthTarget,
                           const float4x4& ViewProj)
    {
    }
}
