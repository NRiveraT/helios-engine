#pragma once
#include <RenderGraph.h>
#include <Math/Math.h>
#include <Public/Device.h>
#include <Public/Formats.h>
#include <Time/Clock.h>

namespace helio::render::passes
{
    class MeshPass
    {
    public:
        MeshPass(rhi::Device& RHI, rhi::Format ColorFormat, rhi::Format DepthFormat);
        ~MeshPass() = default;

        void Execute(render::RenderGraph& rg,
            rhi::TextureHandle colorTarget,
            rhi::TextureHandle depthTarget,
            const float4x4& ViewProj);
    };
}
