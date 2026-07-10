#include "SceneRenderer.h"

#include <Core/Logging/Log.h>
#include <Core/Profile/Profile.h>

#include <Resource/MeshPipeline.h>

#include <Scene/Actors/Camera.h>
#include <Scene/Actors/DirectionalLight.h>
#include <Scene/HelioWorld.h>

#include <cmath>

#include "Actors/SpotLight.h"

namespace helio::scene
{
    static constexpr const uint32_t kMAX_LIGHTS = 16;

    SceneRenderer::SceneRenderer(rhi::Device& RHI, int Width, int Height)
        : m_RHI(&RHI)
          , m_Overlay(RHI, rhi::Format::RGBA8_SRGB)
          , m_DebugDraw(RHI, rhi::Format::RGBA8_SRGB, &m_Overlay)
          , m_InstanceBatch(RHI, kMaxInstances, "SceneInstances")
          , m_LightBufferRing(RHI, kMAX_LIGHTS * sizeof(GPULight))
          , m_FrameConstantsRing(RHI, sizeof(FrameConstants), "FrameConstants")
          , m_Width(Width)
          , m_Height(Height)
    {
        m_ColorTexture = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::RGBA8_SRGB,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
            .DebugName = "SceneColor"
        });

        m_NormalTexture = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::RGBA16F,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
            .DebugName = "SceneNormal"
        });

        m_DepthTexture = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::D32_SFLOAT,
            .Usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled,
            .DebugName = "SceneDepth"
        });

        // Shadow maps

        m_ShadowMapTexture = m_RHI->CreateTexture({
            .Width = kShadowMapResolution,
            .Height = kShadowMapResolution,
            .Fmt = rhi::Format::D32_SFLOAT,
            .Usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled,
            .DebugName = "SunShadowMap"
        });

        m_PostProcessColor = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::RGBA8_SRGB,
            .Usage =
            rhi::TextureUsage::ColorAttachment | // we render into it
            rhi::TextureUsage::Sampled | // (future effects may read it)
            rhi::TextureUsage::TransferSrc, // Present blits it → needs this
            .DebugName = "PostProcessColor"
        });

        // AO
        m_AO = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::R8_UNORM,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
            .DebugName = "SceneAO"
        });

        m_MeshPipeline = resource::CreateMeshInstancedPipeline(
            *m_RHI, {
                .ColorFormat = rhi::Format::RGBA8_SRGB,
                .NormalFormat = rhi::Format::RGBA16F,
                .DepthFormat = rhi::Format::D32_SFLOAT,
                .Cull = rhi::CullMode::Back,
                .DebugName = "Mesh Pipeline"
            });


        // Depth Prepass
        m_DepthPrepassPipeline = m_RHI->CreateGraphicsPipeline({
            .ShaderPath = "Shaders/Passes/DepthPrepass.spv",
            .ColorFormats = {rhi::Format::RGBA16F},
            .ColorAttachmentCount = 1,
            .DepthFormat = rhi::Format::D32_SFLOAT,
            .Cull = rhi::CullMode::Back,
            .Front = rhi::FrontFace::Clockwise,
            .DepthTest = true,
            .DepthWrite = true,
            .DepthCompare = rhi::CompareOp::Greater,
            .DebugName = "Depth Prepass"
        });

        // AO
        m_AmbientOcclusionPipeline = m_RHI->CreateGraphicsPipeline({
            .ShaderPath = "Shaders/Passes/AmbientOcclusion.spv",
            .ColorFormats = {rhi::Format::R8_UNORM},
            .ColorAttachmentCount = 1,
            .DepthFormat = rhi::Format::Undefined,
            .Cull = rhi::CullMode::None,
            .DepthTest = false,
            .DepthWrite = false,
            .DebugName = "Ambient Occlusion"
        });

        m_PostProcessPipeline = m_RHI->CreateGraphicsPipeline({
            .ShaderPath = "Shaders/Passes/PostProcess.spv",
            .ColorFormats = {rhi::Format::RGBA8_SRGB},
            .ColorAttachmentCount = 1,
            .DepthFormat = rhi::Format::Undefined,
            .Cull = rhi::CullMode::None,
            .DepthTest = false,
            .DepthWrite = false,
            .DebugName = "PostProcessing"
        });

        m_DebugViewModePipeline = m_RHI->CreateGraphicsPipeline({
            .ShaderPath = "Shaders/Debug/DebugViewMode.spv",
            .ColorFormats = {rhi::Format::RGBA8_SRGB},
            .ColorAttachmentCount = 1,
            .DepthFormat = rhi::Format::Undefined,
            .Cull = rhi::CullMode::None,
            .DepthTest = false,
            .DepthWrite = false,
            .DebugName = "Debug View Mode Pipeline"
        });

        // Same clip-space conventions as the main pass: reverse-Z ortho with
        // baked Y-flip -> Greater + Clockwise + clear-depth 0, identical to
        // the perspective pipeline. Back-face culling keeps single-sided
        // geometry (ground planes!) casting; acne is handled by the
        // slope-scaled rasterizer bias (negative = away from the light under
        // reverse-Z) plus normal-offset sampling in the mesh shader.
        m_ShadowMapPipeline = m_RHI->CreateGraphicsPipeline({
            .ShaderPath = "Shaders/Passes/ShadowMap.spv",
            .ColorAttachmentCount = 0,
            .DepthFormat = rhi::Format::D32_SFLOAT,
            .Cull = rhi::CullMode::Back,
            .Front = rhi::FrontFace::Clockwise,
            .DepthTest = true,
            .DepthWrite = true,
            .DepthCompare = rhi::CompareOp::Greater,
            .DepthBiasConstant = -2.0f,
            .DepthBiasSlope = -3.0f,
            .DebugName = "ShadowMap Pipeline"
        });

        // Register this renderer's DebugDraw as the global singleton so any
        // gameplay code can call `helio::debug::Line/Box/Sphere/Text2D/Text3D`
        // without holding a renderer reference.
        helio::debug::SetInstance(&m_DebugDraw);
    }

    SceneRenderer::~SceneRenderer()
    {
        if (helio::debug::GetInstance() == &m_DebugDraw)
        {
            helio::debug::SetInstance(nullptr);
        }
        m_RHI->DestroyTexture(m_ColorTexture);
        m_RHI->DestroyTexture(m_NormalTexture);
        m_RHI->DestroyTexture(m_DepthTexture);
        m_RHI->DestroyTexture(m_AO);
        m_RHI->DestroyTexture(m_PostProcessColor);
        m_RHI->DestroyTexture(m_ShadowMapTexture);
        m_RHI->DestroyPipeline(m_DepthPrepassPipeline);
        m_RHI->DestroyPipeline(m_PostProcessPipeline);
        m_RHI->DestroyPipeline(m_MeshPipeline);
        m_RHI->DestroyPipeline(m_ShadowMapPipeline);
        m_RHI->DestroyPipeline(m_DebugViewModePipeline);
    }

    void SceneRenderer::WaitIdle() const
    {
        m_RHI->WaitIdle();
    }

    void SceneRenderer::SubmitMesh(const resource::Mesh& M, const resource::Material& Mat, const Transform& WorldTransform)
    {
        SubmitMesh(M, Mat, WorldTransform.ToMatrix());
    }

    void SceneRenderer::SubmitMesh(const resource::Mesh& M, const resource::Material& Mat, const float4x4& World)
    {
        m_PendingInstances[M.Id].push_back({M, Mat, World});
        if (M.Bounds.IsValid())
        {
            m_CasterBounds.Expand(M.Bounds.Transformed(World));
        }
    }

    void SceneRenderer::BatchMeshInstances()
    {
        m_Draws.reserve(m_PendingInstances.size());

        m_InstanceBatch.Begin();
        for (auto& [MeshId, Instances] : m_PendingInstances)
        {
            HELIO_PROFILE_ZONE("StageMesh")
            if (Instances.empty()) continue;

            // Instances of one mesh can differ in material (per-instance
            // material lives in the push constants, not the instance buffer),
            // so split each mesh group into runs of identical material — one
            // instanced draw per (mesh, material). Instances that share a
            // material still batch into a single DrawIndexed; only distinct
            // materials cost an extra draw. Order-preserving so consecutive
            // same-material submissions coalesce.
            size_t I = 0;
            while (I < Instances.size())
            {
                const resource::Material& Mat = Instances[I].Material;
                const uint32_t First = m_InstanceBatch.StagingSize();
                size_t J = I;
                for (; J < Instances.size() && Instances[J].Material == Mat; ++J)
                {
                    resource::MeshInstance MI{};
                    MI.Transform = Instances[J].World;
                    m_InstanceBatch.Add(MI);
                }
                const uint32_t Count = m_InstanceBatch.StagingSize() - First;
                if (Count > 0)
                {
                    m_Draws.push_back({Instances[I].Mesh, Mat, First, Count});
                }
                I = J;
            }
        }
        m_InstanceBatch.End(); // single upload covering all meshes
    }

    SceneRenderer::ShadowData SceneRenderer::BuildShadowData(const DirectionalLight& Light) const
    {
        ShadowData Out{};
        if (!m_CasterBounds.IsValid())
        {
            return Out; // nothing casts — shadows disabled this frame
        }

        // Bounding-sphere fit: rotation-invariant, so the ortho extent stays
        // constant while the light (or scene content) rotates — a prerequisite
        // for stable texel snapping.
        const float3 Center = m_CasterBounds.Center();
        const float3 Extents = m_CasterBounds.Extents();
        const float Radius =
            std::max(std::sqrt(float(hlslpp::dot(Extents, Extents))), 1e-3f);

        const float4 LightRot = Light.GetWorldRotation();
        const float3 LightDir = QuatRotateVector(LightRot, float3(0.0f, 0.0f, 1.0f));

        // Snap the sphere center to shadow-texel increments in light space —
        // sub-texel translation of the frustum is what causes edge shimmer.
        const float Diameter = 2.0f * Radius;
        const float WorldPerTexel = Diameter / static_cast<float>(kShadowMapResolution);
        float3 CenterLS = QuatUnrotateVector(LightRot, Center);
        CenterLS = float3(
            std::floor(float(CenterLS.x) / WorldPerTexel) * WorldPerTexel,
            std::floor(float(CenterLS.y) / WorldPerTexel) * WorldPerTexel,
            float(CenterLS.z));
        const float3 SnappedCenter = QuatRotateVector(LightRot, CenterLS);

        // Place the eye back along the light direction; pancake the sphere
        // into [NearPad, NearPad + diameter] with margins on both ends.
        const float NearPad = std::max(0.1f * Radius, 0.5f);
        const float3 Eye = SnappedCenter - LightDir * (Radius + NearPad);
        const float NearZ = 0.5f * NearPad;
        const float FarZ = NearPad + Diameter + 0.5f * NearPad;

        const float4x4 View = Transform(Eye, LightRot).ToViewMatrix();
        const float4x4 Proj = math::OrthoReverseZLH(Diameter, Diameter, NearZ, FarZ);

        Out.ViewProj = hlslpp::mul(Proj, View);
        Out.TexelWorldSize = WorldPerTexel;
        Out.DepthRange = FarZ - NearZ;
        Out.Enabled = true;
        return Out;
    }

    void SceneRenderer::Render()
    {
        HELIO_PROFILE_ZONE("Rendering")

        m_StartFrameSec = m_Clock.SecondsSinceStart();

        render::CommandList* Cmd = m_RHI->BeginFrame();
        if (Cmd == nullptr)
        {
            // Swapchain got recreated — drop this frame's submissions.
            m_PendingInstances.clear();
            m_Draws.clear();
            m_CasterBounds = {};
            return;
        }

        render::RenderGraph Rg(*m_RHI, *Cmd);

        // Collect draw submissions from the world.
        if (m_World != nullptr)
        {
            for (const auto& A : m_World->GetActors())
            {
                if (A && !A->IsPendingDestroy())
                {
                    A->OnRender(*this);
                }
            }
        }

        BatchMeshInstances();

        const DirectionalLight* Light = m_World != nullptr ? m_World->GetActorByClass<DirectionalLight>() : nullptr;
        const ShadowData Shadow = Light != nullptr ? BuildShadowData(*Light) : ShadowData{};

        std::vector<GPULight> GPULights;
        for (const auto& a : m_World->GetActors())
        {
            if (GPULights.size() >= kMAX_LIGHTS) continue;
            
            auto* L = dynamic_cast<LightActor*>(a.get());

            if (!L || L->IsPendingDestroy()) continue;
            
            if (L->GetLightType() == LightType::DirectionalLight) continue;

            GPULight light {};
            light.Position[0] = L->GetWorldPosition().x;
            light.Position[1] = L->GetWorldPosition().y;
            light.Position[2] = L->GetWorldPosition().z;

            light.LightType = static_cast<uint32_t>(L->GetLightType());

            light.Color[0] = L->GetColor().r;
            light.Color[1] = L->GetColor().g;
            light.Color[2] = L->GetColor().b;

            light.Intensity = L->GetIntensity();

            light.Direction[0] = L->GetForward().x;
            light.Direction[1] = L->GetForward().y;
            light.Direction[2] = L->GetForward().z;

            light.Range = L->GetRange();

            if (L->GetLightType() == LightType::SpotLight)
            {
                if(SpotLight* S = dynamic_cast<SpotLight*>(L))
                    light.CosOuter = S->GetSpotLightAngleMax();
            }

            GPULights.push_back(light);
        }
        
        if (!GPULights.empty())
        {
            m_LightBufferRing.Write(0, GPULights.data(), GPULights.size() * sizeof(GPULight));
        }

        // ---- Per-frame constants (one bindless SSBO slot, rotates per frame) --
        FrameConstants FC{};
        if (m_Camera != nullptr)
        {
            FC.ViewProj = m_Camera->GetViewProjection();
            const float4x4 Proj = m_Camera->GetProjection();
            const float4x4 InvProj = hlslpp::inverse(Proj);

            FC.Proj = Proj;
            FC.InvProj = InvProj;
            FC.CameraPosWS = float4(m_Camera->GetWorldPosition(), 0.0f);
        }
        else
        {
            FC.ViewProj = math::Identity();
            FC.CameraPosWS = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        FC.LightViewProj = Shadow.ViewProj;
        if (Light != nullptr)
        {
            FC.LightDirWS = float4(hlslpp::normalize(Light->GetDirection()), Light->GetIntensity());
            FC.LightColorAmbient = float4(Light->GetColor(), Light->GetAmbient());
        }
        else
        {
            FC.LightDirWS = float4(0.0f, -1.0f, 0.0f, 0.0f); // intensity 0 — unlit
            FC.LightColorAmbient = float4(1.0f, 1.0f, 1.0f, 0.03f);
        }
        // Receiver-side bias: ~1 texel of world-space depth, converted to the
        // NDC units the shadow map stores (reverse-Z: push refs TOWARD 1).
        const float ReceiverBiasNDC = Shadow.Enabled ? (Shadow.TexelWorldSize / Shadow.DepthRange) : 0.0f;
        FC.ShadowParams = float4(
            1.0f / static_cast<float>(kShadowMapResolution),
            Shadow.Enabled ? Shadow.TexelWorldSize * 1.5f : 0.0f,
            ReceiverBiasNDC,
            Shadow.Enabled ? 1.0f : 0.0f);
        FC.ShadowMapSlot = m_ShadowMapTexture.SampledSlot;

        FC.LightBufferSlot = m_LightBufferRing.Current().BindlessSlot;
        FC.LightCount = static_cast<uint32_t>(GPULights.size());
        
        FC.ViewportAO = float4(static_cast<float>(m_Width), static_cast<float>(m_Height), hlslpp::asfloat(m_AO.SampledSlot), 1.f);
        
        m_FrameConstantsRing.Write(0, &FC, sizeof(FC));
        const uint32_t FrameSlot = m_FrameConstantsRing.Current().BindlessSlot;

        // ---- Shadow depth pre-pass -------------------------------------------
        if (Shadow.Enabled)
        {
            resource::ShadowMapPushConsts ShadowPC{};
            ShadowPC.FrameSlot = FrameSlot;
            ShadowPC.InstanceBufferSlot = m_InstanceBatch.Buffer().BindlessSlot;

            Rg.Graphics("Shadow Map")
              .Depth(m_ShadowMapTexture, 0.0f) // reverse-Z: clear to far
              .Execute([this, ShadowPC](rhi::CommandList& C) mutable
              {
                  C.Bind(m_ShadowMapPipeline);
                  C.SetViewport(kShadowMapResolution, kShadowMapResolution);
                  for (const auto& D : m_Draws)
                  {
                      ShadowPC.VertexBufferSlot = D.Mesh.VertexBuffer.BindlessSlot;
                      ShadowPC.InstanceBase = D.FirstInstance;
                      C.Push(ShadowPC);
                      C.BindIndexBuffer(D.Mesh.IndexBuffer, resource::IndexTypeFor(D.Mesh));
                      C.DrawIndexed(D.Mesh.IndexCount, D.InstanceCount);
                  }
              });
        }

        const bool HasScene = !m_Draws.empty() && m_Camera != nullptr;

        {
            resource::DepthPrepassPushConsts DepthPC{};
            DepthPC.FrameSlot = FrameSlot;
            DepthPC.InstanceBufferSlot = m_InstanceBatch.Buffer().BindlessSlot;

            Rg.Graphics("Depth Prepass")
              .Depth(m_DepthTexture, 0.0f)
              .Color(m_NormalTexture, 0.0f, 0.0f, 0.0f, 1.f)
              .Execute([this, DepthPC, HasScene](rhi::CommandList& C) mutable
              {
                  if (!HasScene)
                  {
                      return;
                  }

                  C.Bind(m_DepthPrepassPipeline);
                  C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
                  for (const auto& D : m_Draws)
                  {
                      DepthPC.VertexBufferSlot = D.Mesh.VertexBuffer.BindlessSlot;
                      DepthPC.InstanceBase = D.FirstInstance;
                      C.Push(DepthPC);
                      C.BindIndexBuffer(D.Mesh.IndexBuffer, resource::IndexTypeFor(D.Mesh));
                      C.DrawIndexed(D.Mesh.IndexCount, D.InstanceCount);
                  }
              });
        }

        // AO
        {
            Rg.Graphics("Ambient Occlusion")
              .Read(m_NormalTexture)
              .Read(m_DepthTexture)
              .Color(m_AO, 0.0f, 0.0f, 0.0f, 1.f)
              .Execute([this, FrameSlot, HasScene](rhi::CommandList& C)
              {
                  if (!HasScene)
                  {
                      return; // clear-only frame
                  }

                  HELIO_PROFILE_ZONE("AO")
                  struct AOPushConsts
                  {
                      uint32_t FrameSlot;
                      uint32_t NormalSamplerSlot;
                      uint32_t DepthSlot;
                      float4x4 View;
                  } pc;

                  pc.FrameSlot = FrameSlot;
                  pc.NormalSamplerSlot = m_NormalTexture.SampledSlot;
                  pc.DepthSlot = m_DepthTexture.SampledSlot;
                  pc.View = hlslpp::transpose(m_Camera->GetView());

                  C.Bind(m_AmbientOcclusionPipeline);
                  C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
                  C.Push(pc);

                  C.Draw(3);
              });
        }

        // ---- Main opaque pass ------------------------------------------------
        // Declared EVERY frame so the color + depth targets are always cleared
        // — even with an empty world or no camera. Otherwise the graph would
        // present uninitialized (first frame) or stale (subsequent frames) VRAM
        // beneath the overlay. When there is nothing to draw the lambda is a
        // no-op and only the BeginRendering clear runs.
        {
            auto Pass = Rg.Graphics("Static Meshes")
                          .Color(m_ColorTexture, 0.1274f, 0.3005f, 0.8469f, 1.f)
                          .Color(m_NormalTexture)
                          .Read(m_AO)
                          .DepthLoad(m_DepthTexture);
            if (Shadow.Enabled && HasScene)
            {
                Pass.Read(m_ShadowMapTexture); // depth -> SHADER_READ_ONLY before sampling
            }
            Pass.Execute([this, FrameSlot, HasScene](rhi::CommandList& C)
            {
                if (!HasScene)
                {
                    return; // clear-only frame
                }
                C.Bind(m_MeshPipeline);
                // Viewport = the TARGET's extent, not the swapchain's. The
                // render graph already set this at BeginRendering; we restate
                // it explicitly so a resize can never leave a stale swapchain
                // extent driving the rasterizer into an undersized attachment.
                C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));

                resource::MeshInstancedPushConsts PC{};
                PC.FrameSlot = FrameSlot;
                PC.InstanceBufferSlot = m_InstanceBatch.Buffer().BindlessSlot;
                PC.AOSamplerSlot = m_AO.SampledSlot;

                for (const auto& D : m_Draws)
                {
                    HELIO_PROFILE_ZONE("DrawMesh")
                    PC.VertexBufferSlot = D.Mesh.VertexBuffer.BindlessSlot;
                    PC.InstanceBase = D.FirstInstance;
                    PC.Albedo = D.Material.AlbedoTint;
                    PC.Roughness = D.Material.Roughness;
                    PC.Emissive = D.Material.EmissiveColor * D.Material.EmissiveIntensity;
                    PC.Metallic = D.Material.Metallic;
                    PC.AlbedoTex = D.Material.AlbedoTex;
                    PC.NormalTex = D.Material.NormalTex;
                    PC.MetalRoughTex = D.Material.MetalRoughTex;
                    PC.EmissiveTex = D.Material.EmissiveTex;
                    PC.OcclusionTex = D.Material.OcclusionTex;

                    C.Push(PC);
                    C.BindIndexBuffer(D.Mesh.IndexBuffer, resource::IndexTypeFor(D.Mesh));
                    C.DrawIndexed(D.Mesh.IndexCount, D.InstanceCount);
                }
            });
        }

        {
            Rg.Graphics("Post-process")
              .Read(m_ColorTexture)
              .Color(m_PostProcessColor, 0.0f, 0.0f, 0.0f, 1.f)
              .Execute([this](rhi::CommandList& C)
              {
                  HELIO_PROFILE_ZONE("PostProcess")
                  C.Bind(m_PostProcessPipeline);
                  C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));

                  resource::PostProcessPushConstants PC{};
                  PC.SourceSlot = m_ColorTexture.SampledSlot;
                  C.Push(PC);
                  C.Draw(3);
              });
        }

        {
            if (m_DebugViewMode != DebugViewMode::Lit)
            {
                rhi::TextureHandle DebugTexture;
                switch (m_DebugViewMode)
                {
                case DebugViewMode::Depth: DebugTexture = m_DepthTexture;
                    break;
                case DebugViewMode::WorldNormals: DebugTexture = m_NormalTexture;
                    break;
                case DebugViewMode::WorldPosition: DebugTexture = m_DepthTexture;
                    break;
                case DebugViewMode::AmbientOcclusion: DebugTexture = m_AO;
                    break;

                default: break;
                }

                if (DebugTexture.IsValid())
                {
                    Rg.Graphics("Debug View Mode")
                      .Read(DebugTexture)
                      .Color(m_PostProcessColor)
                      .Execute([this, DebugTexture, FrameSlot](rhi::CommandList& C) mutable
                      {
                          HELIO_PROFILE_ZONE("DebugView")
                          C.Bind(m_DebugViewModePipeline);
                          C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));

                          resource::DebugViewPushConst pc{};
                          pc.Mode = static_cast<uint32_t>(m_DebugViewMode);
                          pc.NearZ = m_Camera ? m_Camera->GetNearZ() : 0.01f;
                          pc.DebugFar = 100.f;
                          pc.SourceSlot = DebugTexture.SampledSlot;
                          pc.FrameSlot = FrameSlot;
                          C.Push(pc);

                          C.Draw(3);
                      });
                }
            }
        }

        // ---- Debug lines + stats overlay ----------------------------------------
        if (m_Camera != nullptr)
        {
            m_DebugDraw.Render(Rg, m_PostProcessColor, m_Camera->GetViewProjection(), m_Width, m_Height);
        }

        // Stats show the PREVIOUS frame's fully-measured CPU cost (m_LastCpuMs,
        // set at the bottom of the last Render). The render graph is deferred,
        // so the dominant cost — command recording in Rg.Execute() below — has
        // not run yet this frame; measuring here would badly under-report it.
        m_Overlay.DrawStats(m_LastCpuMs, m_RHI->LastFrameGpuMs(), Rg.Passes());
        m_Overlay.Render(Rg, m_PostProcessColor, m_Width, m_Height);

        // ---- Editor / UI hook -----------------------------------------------------
        if
        (m_OverlayHook)
        {
            m_OverlayHook(Rg, m_PostProcessColor, static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
        }

        Rg.Present(m_PostProcessColor);
        Rg.Execute(); // all pass lambdas + barriers + blit record here

        m_RHI->EndFrame();
        m_PendingInstances.clear();
        m_Draws.clear();
        m_CasterBounds = {};

        // Full CPU cost of this Render() call, including deferred recording —
        // consumed by next frame's stats overlay and by the editor.
        m_LastCpuMs = (m_Clock.SecondsSinceStart() - m_StartFrameSec) * 1000.0;
    }

    void SceneRenderer::Resize(int Width, int Height)
    {
        if (Width <= 0 || Height <= 0 || (Width == m_Width && Height == m_Height))
        {
            return;
        }
        m_Width = Width;
        m_Height = Height;

        // Recreate the window-sized offscreen targets so the scene renders at
        // the new resolution and the final blit is 1:1 with the swapchain
        // (no stretch, no aspect distortion). The shadow map is a fixed-size
        // light-space texture and is deliberately NOT resized. DestroyTexture
        // routes through the deletion queue, so releasing the old textures
        // mid-run is safe even if a prior frame still referenced them.
        m_RHI->DestroyTexture(m_ColorTexture);
        m_RHI->DestroyTexture(m_NormalTexture);
        m_RHI->DestroyTexture(m_DepthTexture);
        m_RHI->DestroyTexture(m_AO);
        m_RHI->DestroyTexture(m_PostProcessColor);

        m_ColorTexture = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::RGBA8_SRGB,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled |
            rhi::TextureUsage::TransferSrc,
            .DebugName = "SceneColor"
        });
        m_NormalTexture = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::RGBA16F,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
            .DebugName = "SceneNormal"
        });
        m_DepthTexture = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::D32_SFLOAT,
            .Usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled,
            .DebugName = "SceneDepth"
        });

        // Recreate AO texture after resize
        m_AO = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::R8_UNORM,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
            .DebugName = "SceneAO"
        });

        m_PostProcessColor = m_RHI->CreateTexture({
            .Width = static_cast<uint32_t>(Width),
            .Height = static_cast<uint32_t>(Height),
            .Fmt = rhi::Format::RGBA8_SRGB,
            .Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
            .DebugName = "PostProcessColor"
        });
    }
} // namespace helio::scene
