#include <MeshPipeline.h>
#include <Public/Device.h>
#include <Gameplay/World/HelioRenderer.h>
#include <Logging/Log.h>
#include <Profile/Profile.h>

#include "Actors/DirectionalLight.h"
#include "Gameplay/World/HelioWorld.h"

namespace helio::gameplay
{
    constexpr int SHADOWMAP_RESOLUTION = 256;
    
    HelioRenderer::HelioRenderer(rhi::Device& RHI, const core::EngineConfig& Config) : m_RHI(&RHI), m_Overlay(*m_RHI, rhi::Format::RGBA8_SRGB), m_DebugDraw(*m_RHI, rhi::Format::RGBA8_SRGB, &m_Overlay), m_InstanceBatch(*m_RHI, SHADOWMAP_RESOLUTION)
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
            .Usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled,
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

        m_PostShadowPipeline = m_RHI->CreateGraphicsPipeline({
            .ShaderPath = "Shaders/Passes/Lighting.spv",
            .ColorFormats = rhi::Format::RGBA8_SRGB,
            .DepthTest = false,
            .DepthWrite = false,
            .DebugName = "Post Shadow"
        });

        m_ShadowMapTexture = m_RHI->CreateTexture({
            .Width = SHADOWMAP_RESOLUTION,
            .Height = SHADOWMAP_RESOLUTION,
            .Fmt = rhi::Format::D32_SFLOAT,
            .Usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
            .DebugName = "ShadowMapTexture"
        });

        m_ShadowMapPipeline = m_RHI->CreateGraphicsPipeline(
            {
                .ShaderPath = "Shaders/Passes/ShadowMap.spv",
                .ColorAttachmentCount = 0,
                .DepthFormat = rhi::Format::D32_SFLOAT,
                .Cull = rhi::CullMode::Front,
                .Front = rhi::FrontFace::Clockwise,
                .DepthTest = true,
                .DepthWrite = true,
                .DepthCompare = rhi::CompareOp::Less,
                .DebugName = "ShadowMap Pipeline"
            });

        m_PostShadow = m_RHI->CreateTexture({
            .Width = SHADOWMAP_RESOLUTION,
            .Height = SHADOWMAP_RESOLUTION,
            .Fmt = rhi::Format::RGBA8_SRGB,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
            .DebugName = "PostShadow"
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
        m_RHI->DestroyTexture(m_ShadowMapTexture);
        m_RHI->DestroyPipeline(m_MeshPipeline);
        m_RHI->DestroyPipeline(m_ShadowMapPipeline);
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

            BatchMeshInstances();

            DrawShadowMaps(&rg);

            rg.Graphics("Post Shadow")
              .Color(m_PostShadow)
              .Read(m_ShadowMapTexture)

              .Execute([&](rhi::CommandList& Cmd)
              {
                  Cmd.Bind(m_PostShadowPipeline);
                  Cmd.SetViewport(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
                  struct PushConstants
                  {
                      uint32_t Slot, SamplerSlot;
                  } pc;

                  pc.SamplerSlot = 0;
                  pc.Slot = m_ShadowMapTexture.SampledSlot;

                  Cmd.Push(pc);
                  Cmd.Draw(3);
              });

            DrawStaticMeshes(&rg);

            DrawDebug(&rg);
            DrawOverlay(&rg);

            rg.Present(m_ColorTexture);
            rg.Execute();

            m_RHI->EndFrame();
            m_PendingInstances.clear();
            Draws.clear();
        }
    }

    void HelioRenderer::WaitIdle() const
    {
        m_RHI->WaitIdle();
    }

    void HelioRenderer::BatchMeshInstances()
    {
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

            Draws.push_back({instances.front().Mesh, instances.front().Material, First, Count});
        }
        m_InstanceBatch.End(); // single upload covering all meshes
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

              if (DirectionalLight* DirLight = m_World->GetActorByClass<DirectionalLight>())
              {
                  PC.LightDirWS = float4(DirLight->GetActorForwardVector(), DirLight->Intensity);
              }

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
                  Cmd.DrawIndexed(D.Mesh.IndexCount, D.InstanceCount);
              }
          });
    }

    void HelioRenderer::DrawShadowMaps(render::RenderGraph* rg)
    {
        if (DirectionalLight* DirLight = m_World->GetActorByClass<DirectionalLight>())
        {
            rg->Graphics("Shadow Map")
              .Depth(m_ShadowMapTexture, 1.0f)
              .Execute([&](rhi::CommandList& Cmd)
              {
                  Cmd.Bind(m_ShadowMapPipeline);
                  Cmd.SetViewport(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);

                  resource::ShadowMapPushConsts PC{};

                  const float3 F = DirLight->GetActorForwardVector();
                  const float3 U = DirLight->GetActorRightVector();
                  const float3 Eye = -F * 2000.0f;

                  const float4 Q = DirLight->GetTransform().Rotation;
                  HELIO_LOG_INFO("Shadow",
                                 "F=({:.3f}, {:.3f}, {:.3f})  U=({:.3f}, {:.3f}, {:.3f})  Eye=({:.3f}, {:.3f}, {:.3f})",
                                 float(F.x), float(F.y), float(F.z),
                                 float(U.x), float(U.y), float(U.z),
                                 float(Eye.x), float(Eye.y), float(Eye.z));

                  float4x4 LightProjection = math::OrthoLH(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION, 0.1f, 4000.0f);
                  float4x4 LightView = math::LookAtLH(Eye, float3(0.0f, 0.0f, 0.0f), U);
                  float4x4 LightViewProj = mul(LightProjection, LightView);

                  // Print each matrix on CPU
                  alignas(16) float P[16], V[16], VP[16];
                  hlslpp::store(P, LightProjection);
                  hlslpp::store(V, LightView);
                  hlslpp::store(VP, LightViewProj);

                  // HELIO_LOG_INFO("Shadow", "Proj  {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f}",
                  //                P[0], P[1], P[2], P[3], P[4], P[5], P[6], P[7], P[8], P[9], P[10], P[11], P[12], P[13], P[14], P[15]);
                  // HELIO_LOG_INFO("Shadow", "View  {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f}",
                  //                V[0], V[1], V[2], V[3], V[4], V[5], V[6], V[7], V[8], V[9], V[10], V[11], V[12], V[13], V[14], V[15]);
                  // HELIO_LOG_INFO("Shadow", "VP    {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f}",
                  //                VP[0], VP[1], VP[2], VP[3], VP[4], VP[5], VP[6], VP[7], VP[8], VP[9], VP[10], VP[11], VP[12], VP[13], VP[14], VP[15]);

                  PC.LightViewProj = LightViewProj;
                  PC.InstanceBufferSlot = m_InstanceBatch.Buffer().BindlessSlot;

                  for (const auto& D : Draws)
                  {
                      PC.VertexBufferSlot = D.Mesh.VertexBuffer.BindlessSlot;
                      PC.InstanceBase = D.FirstInstance;
                      Cmd.Push(PC);
                      Cmd.BindIndexBuffer(D.Mesh.IndexBuffer, resource::IndexTypeFor(D.Mesh));
                      Cmd.DrawIndexed(D.Mesh.IndexCount, D.InstanceCount);
                  }
              });
        }
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
