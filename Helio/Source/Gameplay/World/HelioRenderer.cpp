#include <MeshPipeline.h>
#include <Public/Device.h>
#include <Gameplay/World/HelioRenderer.h>
#include <Logging/Log.h>
#include <Profile/Profile.h>

#include "Gameplay/World/HelioWorld.h"
#include "Interfaces/IRenderable.h"

namespace helio::gameplay
{
    HelioRenderer::HelioRenderer(rhi::Device& RHI, const EngineConfig& Config) : m_rhi(&RHI), m_Overlay(*m_rhi, rhi::Format::RGBA8_SRGB), m_instanceBatch(*m_rhi, 1024)
    {
        m_Width = Config.Width;
        m_Height = Config.Height;

        m_colorTexture = m_rhi->CreateTexture({
            .Width = static_cast<uint32_t>(Config.Width),
            .Height = static_cast<uint32_t>(Config.Height),
            .Fmt = rhi::Format::RGBA8_SRGB,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
            .DebugName = "ColorTexture"
        });

        m_depthTexture = m_rhi->CreateTexture({
            .Width = static_cast<uint32_t>(Config.Width),
            .Height = static_cast<uint32_t>(Config.Height),
            .Fmt = rhi::Format::D32_SFLOAT,
            .Usage = rhi::TextureUsage::DepthStencilAttachment,
            .DebugName = "DepthTexture"
        });

        m_meshPipeline = resource::CreateMeshInstancedPipeline(
            *m_rhi, {
                .ColorFormat = rhi::Format::RGBA8_SRGB,
                .DepthFormat = rhi::Format::D32_SFLOAT,
                .Cull = rhi::CullMode::Back,
                .DepthTest = true,
                .DepthWrite = true,
                .DebugName = "Mesh Pipeline"
            });
    }

    HelioRenderer::~HelioRenderer()
    {
        m_rhi->DestroyTexture(m_colorTexture);
        m_rhi->DestroyTexture(m_depthTexture);
    }

    void HelioRenderer::Render()
    {
        HELIO_PROFILE_ZONE("Rendering")

        if (render::CommandList* Cmd = m_rhi->BeginFrame())
        {
            render::RenderGraph rg(*m_rhi, *Cmd);
            
            for (auto& A : m_world->GetWorldActors())
            {
                if (auto R = dynamic_cast<IRenderable*>(A.get()))
                {
                    R->OnRender();
                }
            }
            
            PreRenderScene(&rg);
            RenderScene(&rg);
            
            RenderOverlay(&rg);

            rg.Present(m_colorTexture);
            rg.Execute();
            
            m_rhi->EndFrame();
            m_pending.clear(); 
        }
    }

    void HelioRenderer::WaitIdle() const
    {
        m_rhi->WaitIdle();
    }

    void HelioRenderer::PreRenderScene(render::RenderGraph* rg)
    {
        HELIO_PROFILE_ZONE("PreRenderScene")
    }

    void HelioRenderer::RenderScene(render::RenderGraph* rg)
    {
        HELIO_PROFILE_ZONE("RenderScene")

        rg->Graphics("Static Meshes")
          .Color(m_colorTexture, 0.2, 0.2, 0.2, 1.0)
          .Depth(m_depthTexture, 0.0f)
          .Execute([&](rhi::CommandList& Cmd)
          {
              Cmd.Bind(m_meshPipeline);
              Cmd.SetViewportFull();

              float4x4 Projection = math::PerspectiveReverseZLH(float1(45.f), static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.01f);
              float4x4 LookAt = math::LookAtLH(float3(0, 0, -10), float3(0, 0, 0), float3(0, 1, 0));

              resource::MeshInstancedPushConsts PC{};
              PC.ViewProj = hlslpp::mul(Projection, LookAt);
              PC.LightDirWS = float3(0.57735f, 0.57735f, 0.57735f);
              PC.AlbedoTint = float4(1.0f, 1.0f, 1.0f, 1.0f);

              for (auto& [meshId, instances] : m_pending)
              {
                  HELIO_PROFILE_ZONE("SubmitMesh")
                  if (instances.empty())
                  {
                      continue;
                  }

                  const resource::Mesh& mesh = instances[0].Mesh;

                  m_instanceBatch.Begin();
                  for (const auto& instance : instances)
                  {
                      resource::MeshInstance MI{};
                      MI.Transform = instance.World;
                      m_instanceBatch.Add(MI);
                  }
                  uint32_t instanceCount = m_instanceBatch.End();

                  PC.VertexBufferSlot = mesh.VertexBuffer.BindlessSlot;
                  PC.InstanceBufferSlot = m_instanceBatch.Buffer().BindlessSlot;
                  Cmd.Push(PC);
                  Cmd.BindIndexBuffer(mesh.IndexBuffer, resource::IndexTypeFor(mesh));
                  Cmd.DrawIndexed(mesh.IndexCount, instanceCount);
              }
          });
    }

    void HelioRenderer::RenderPostProcess(render::RenderGraph* rg)
    {
    }

    void HelioRenderer::RenderUI(render::RenderGraph* rg)
    {
    }

    void HelioRenderer::RenderOverlay(render::RenderGraph* rg)
    {
        m_Overlay.DrawStats(0, m_rhi->LastFrameGpuMs(), rg->Passes());
        m_Overlay.Render(*rg, m_colorTexture, m_Width, m_Height);
    }
}
