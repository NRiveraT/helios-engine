#pragma once
#include <InstanceBatch.h>
#include <Mesh.h>
#include <RenderGraph.h>
#include <Overlay/Overlay.h>
#include "Types/EngineConfig.h"
#include "Types/PendingInstance.h"

namespace helio::gameplay
{
    class HelioWorld;

    class HelioRenderer
    {
    public:
        explicit HelioRenderer(rhi::Device& RHI, const EngineConfig& Config);
        ~HelioRenderer();

        void SetWorld(HelioWorld& World) { m_world = &World; }

        void Render();
        void WaitIdle() const;

        void SubmitMesh(const resource::Mesh& m, const Transform& T)
        {
            m_pending[m.Id].emplace_back(m, T.ToMatrix());
        }

        [[nodiscard]] const std::unordered_map<uint64_t, std::vector<PendingInstance>>& GetPendingInstances() const noexcept { return m_pending; }
        
    private:
        void PreRenderScene(render::RenderGraph* rg);
        void RenderScene(render::RenderGraph* rg);
        void RenderPostProcess(render::RenderGraph* rg);
        void RenderUI(render::RenderGraph* rg);
        void RenderOverlay(render::RenderGraph* rg);

        rhi::Device* m_rhi;
        rhi::TextureHandle m_colorTexture;
        rhi::TextureHandle m_depthTexture;
        render::overlay::Overlay m_Overlay;

        HelioWorld* m_world = nullptr;
        
        // Grouped by mesh ID so all instances of the same mesh can be drawn in one DrawIndexed call.
        std::unordered_map<uint64_t, std::vector<PendingInstance>> m_pending;

        // Owned GPU resources
        resource::InstanceBatch m_instanceBatch; // ring-buffered per-frame
        
        rhi::PipelineHandle m_meshPipeline;
        
        int m_Width;
        int m_Height;
    };
};
