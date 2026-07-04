#pragma once
#include <InstanceBatch.h>
#include <Mesh.h>
#include <RenderGraph.h>
#include <Overlay/Overlay.h>
#include <Debug/DebugDraw.h>
#include "Actors/Camera.h"
#include "Types/EngineConfig.h"
#include "Types/PendingInstance.h"

namespace helio::gameplay
{
    struct MeshDraw
    {
        resource::Mesh Mesh;
        resource::Material Material;

        uint32_t FirstInstance;
        uint32_t InstanceCount;
    };

    class HelioWorld;

    class HelioRenderer
    {
    public:
        explicit HelioRenderer(rhi::Device& RHI, const core::EngineConfig& Config);
        ~HelioRenderer();

        void SetWorld(HelioWorld& World) { m_World = &World; }
        void SetRenderingCamera(Camera* cam) { m_Camera = cam; }

        void Render();
        void WaitIdle() const;

        void SubmitMesh(const resource::Mesh& m, const resource::Material& mat, const Transform& T)
        {
            m_PendingInstances[m.Id].emplace_back(m, mat, T.ToMatrix());
        }

        [[nodiscard]] const std::unordered_map<uint64_t, std::vector<core::PendingInstance>>& GetPendingInstances() const noexcept { return m_PendingInstances; }

    private:
        void BatchMeshInstances();
        void DrawStaticMeshes(render::RenderGraph* rg);
        void DrawShadowMaps(render::RenderGraph* rg);
        void DrawPostProcess(render::RenderGraph* rg);
        void DrawDebug(render::RenderGraph* rg);
        void DrawUI(render::RenderGraph* rg);
        void DrawOverlay(render::RenderGraph* rg);

        rhi::Device* m_RHI;

        rhi::TextureHandle m_ColorTexture;
        rhi::TextureHandle m_DepthTexture;

        rhi::TextureHandle m_ShadowMapTexture;
        rhi::TextureHandle m_PostShadow;

        render::overlay::Overlay m_Overlay;
        render::debug::DebugDraw m_DebugDraw;

        HelioWorld* m_World = nullptr;
        Camera* m_Camera = nullptr;

        // Grouped by mesh ID so all instances of the same mesh can be drawn in one DrawIndexed call.
        std::unordered_map<uint64_t, std::vector<core::PendingInstance>> m_PendingInstances;
        std::vector<MeshDraw> Draws;

        // Owned GPU resources
        resource::InstanceBatch m_InstanceBatch; // ring-buffered per-frame

        rhi::PipelineHandle m_MeshPipeline;
        rhi::PipelineHandle m_ShadowMapPipeline;

        rhi::PipelineHandle m_PostShadowPipeline;

        int m_Width{0};
        int m_Height{0};
        double m_StartFrameSec{0};
    };
};
