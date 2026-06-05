#include <MeshPipeline.h>
#include <Public/Device.h>
#include <Gameplay/World/HelioRenderer.h>
#include <Logging/Log.h>
#include <Profile/Profile.h>

#include "Actors/DirectionalLight.h"
#include "Gameplay/World/HelioWorld.h"

namespace helio::gameplay
{
    HelioRenderer::HelioRenderer(rhi::Device& RHI, const core::EngineConfig& Config) : m_RHI(&RHI), m_Overlay(*m_RHI, rhi::Format::RGBA8_SRGB), m_DebugDraw(*m_RHI, rhi::Format::RGBA8_SRGB, &m_Overlay), m_InstanceBatch(*m_RHI, 1024)
    {
        m_Width = Config.Width;
        m_Height = Config.Height;

        m_ColorTexture = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Config.Width),
            .Height = static_cast<uint32_t>(Config.Height),
            .Fmt = rhi::Format::RGBA8_SRGB,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
            .DebugName = "ColorTexture"
        });

        m_DepthTexture = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Config.Width),
            .Height = static_cast<uint32_t>(Config.Height),
            .Fmt = rhi::Format::D32_SFLOAT,
            .Usage = rhi::TextureUsage::DepthStencilAttachment,
            .DebugName = "DepthTexture"
        });

        m_MeshPipeline = resource::CreateMeshInstancedPipeline(
            *m_RHI, {
                .ColorFormat = rhi::Format::RGBA8_SRGB,
                .DepthFormat = rhi::Format::D32_SFLOAT,
                .Cull = rhi::CullMode::Back,
                .DepthTest = true,
                .DepthWrite = true,
                .DebugName = "Mesh Pipeline"
            });

        // Register this renderer's DebugDraw as the global singleton so any
        // gameplay code can call `helio::debug::Line/Box/Sphere/Text2D/Text3D`
        // without holding a renderer reference.
        helio::debug::SetInstance(&m_DebugDraw);
    }

    HelioRenderer::~HelioRenderer()
    {
        if (helio::debug::GetInstance() == &m_DebugDraw)
        {
            helio::debug::SetInstance(nullptr);
        }
        m_RHI->DestroyTexture(m_ColorTexture);
        m_RHI->DestroyTexture(m_DepthTexture);
    }

    void HelioRenderer::Render()
    {
        HELIO_PROFILE_ZONE("Rendering")

        m_StartFrameSec = m_World->Engine().EngineClock().SecondsSinceStart();

        if (render::CommandList* Cmd = m_RHI->BeginFrame())
        {
            render::RenderGraph rg(*m_RHI, *Cmd);

            for (auto& A : m_World->GetWorldActors())
            {
                A->OnRender();
            }

            DrawStaticMeshes(&rg);

            DrawDebug(&rg);
            DrawOverlay(&rg);

            rg.Present(m_ColorTexture);
            rg.Execute();

            m_RHI->EndFrame();
            m_PendingInstances.clear();
        }
    }

    void HelioRenderer::WaitIdle() const
    {
        m_RHI->WaitIdle();
    }

    void HelioRenderer::DrawStaticMeshes(render::RenderGraph* rg)
    {
        HELIO_PROFILE_ZONE("RenderScene")

        if (m_PendingInstances.empty() || m_Camera == nullptr)
        {
            return;
        }

        rg->Graphics("Static Meshes")
          .Color(m_ColorTexture, 0.1274, 0.3005, 0.8469, 1.0)
          .Depth(m_DepthTexture, 0.0f)
          .Execute([&](rhi::CommandList& Cmd)
          {
              Cmd.Bind(m_MeshPipeline);
              Cmd.SetViewportFull();

              resource::MeshInstancedPushConsts PC{};
              PC.ViewProj = m_Camera->GetViewProjection();
              PC.CameraPosWS = m_Camera->GetTransform().Position;

              DirectionalLight* DirLight = m_World->GetActorsByClass<DirectionalLight>();
              PC.LightDirWS = float4(DirLight->GetActorForwardVector(), DirLight->Intensity);

              // Pack EVERY mesh's instances into ONE buffer write, tracking
              // each mesh's slice as (firstInstance, instanceCount). If we
              // wrote per-mesh inside the draw loop, each Write() would
              // clobber the buffer at offset 0 before the GPU executed the
              // previous draw — every mesh would end up reading the last
              // group's transforms.
              struct MeshDraw
              {
                  resource::Mesh Mesh;
                  resource::Material Material;

                  uint32_t FirstInstance;
                  uint32_t InstanceCount;
              };

              std::vector<MeshDraw> Draws;
              Draws.reserve(m_PendingInstances.size());

              m_InstanceBatch.Begin();
              for (auto& [meshId, instances] : m_PendingInstances)
              {
                  HELIO_PROFILE_ZONE("StageMesh")
                  if (instances.empty()) continue;

                  const uint32_t First = m_InstanceBatch.StagingSize();
                  for (const auto& instance : instances)
                  {
                      resource::MeshInstance MI{};
                      MI.Transform = instance.World;

                      m_InstanceBatch.Add(MI);
                  }
                  const uint32_t Count = m_InstanceBatch.StagingSize() - First;

                  Draws.push_back({instances.front().Mesh, instances.front().Mesh.m_Material, First, Count});
              }
              m_InstanceBatch.End(); // single upload covering all meshes

              PC.InstanceBufferSlot = m_InstanceBatch.Buffer().BindlessSlot;

              for (const auto& D : Draws)
              {
                  HELIO_PROFILE_ZONE("DrawMesh")
                  PC.VertexBufferSlot = D.Mesh.VertexBuffer.BindlessSlot;
                  PC.InstanceBase = D.FirstInstance;
                  PC.Albedo = D.Material.AlbedoTint;
                  PC.Roughness = D.Material.Roughness;
                  PC.Metallic = D.Material.Metallic;
                  
                  Cmd.Push(PC);
                  Cmd.BindIndexBuffer(D.Mesh.IndexBuffer, resource::IndexTypeFor(D.Mesh));
                  // No firstInstance here — Slang's SV_InstanceID is per-draw
                  // and ignores it. The shader does `InstanceBase + InstanceID`
                  // internally to pick the right transform.
                  Cmd.DrawIndexed(D.Mesh.IndexCount, D.InstanceCount);
              }
          });
    }

    void HelioRenderer::DrawPostProcess(render::RenderGraph* rg)
    {
    }

    void HelioRenderer::DrawDebug(render::RenderGraph* rg)
    {
        m_DebugDraw.Render(*rg, m_ColorTexture, m_Camera->GetViewProjection(), m_Width, m_Height);
    }

    void HelioRenderer::DrawUI(render::RenderGraph* rg)
    {
    }

    void HelioRenderer::DrawOverlay(render::RenderGraph* rg)
    {
        const double NowSec = m_World->Engine().EngineClock().SecondsSinceStart();
        const double CpuMs = (NowSec - m_StartFrameSec) * 1000.0;

        m_Overlay.DrawStats(CpuMs, m_RHI->LastFrameGpuMs(), rg->Passes());
        m_Overlay.Render(*rg, m_ColorTexture, m_Width, m_Height);
    }
}
