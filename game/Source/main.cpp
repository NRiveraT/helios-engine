// game/Source/main.cpp
//
// Phase 4 verification: open window, bring up Vulkan device + swapchain,
// clear the swapchain blue every frame. Validation layer must be quiet.

#include <ActionMap.h>
#include <Core/Logging/Log.h>
#include <Core/Profile/Profile.h>
#include <Core/Time/Clock.h>
#include <Core/Math/Math.h>
#include <Core/Handles/Handle.h>

#include <Platform/Windows/Window.h>
#include <RHI/Public/Device.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <InstanceBatch.h>
#include <Mesh.h>
#include <MeshPipeline.h>
#include <MeshPrimitives.h>
#include <RenderGraph.h>
#include <vector>
#include <Overlay/Overlay.h>


// int main()
// {
//     helio::log::Init();
//
//     HELIO_PROFILE_ZONE("Startup");
//     helio::core::Clock StartupClock;
//
//     HELIO_LOG_INFO("Game", "Helio Phase 9 - RHI Vulkan device init.");
//
//     // Open the window first (Vulkan-capable surface).
//     helio::platform::windows::Window Win({
//         .Title = "Helio - Phase 9",
//         .Width = 1920,
//         .Height = 1080,
//         .RequestVulkan = true,
//     });
//
//     // Spin up the Vulkan device against that window.
//     helio::rhi::Device RHI({
//         .NativeWindow = Win.Native(),
//         .InitialWidth = Win.Width(),
//         .InitialHeight = Win.Height(),
//         .EnableValidation = true,
//         .EnableRayTracing = true,
//     });
//
//     HELIO_LOG_INFO("Game", "Device up after {:.2f} ms.", StartupClock.SecondsSinceStart() * 1000.0);
//
//     if (RHI.HasRayTracing())
//     {
//         auto Rt = RHI.GetRayTracingProperties();
//         HELIO_LOG_INFO("Game",
//                        "RT supported. maxRecursionDepth={} SBT(handle={}/align={}/base={}) scratchAlign={}",
//                        Rt.MaxRayRecursionDepth, Rt.ShaderGroupHandleSize, Rt.ShaderGroupHandleAlignment,
//                        Rt.ShaderGroupBaseAlignment, Rt.MinAccelStructScratchOffsetAlignment);
//     }
//     else
//     {
//         HELIO_LOG_WARN("Game", "RT not available on this device; engine will run without RT paths.");
//     }
//
//     // Phase 5 smoke: upload a 64x64 checkerboard via staging into a Sampled texture,
//     // and create a DeviceLocal storage buffer with initial data. Both pick bindless slots.
//     std::vector<uint32_t> Checker(64 * 64);
//     for (uint32_t Y = 0; Y < 64; ++Y)
//     {
//         for (uint32_t X = 0; X < 64; ++X)
//         {
//             bool On = ((X >> 3) ^ (Y >> 3)) & 1;
//             Checker[Y * 64 + X] = On ? 0xFFFFFFFFu : 0xFF202020u;
//         }
//     }
//     auto CheckerTex = RHI.CreateTexture({
//         .Width = 64, .Height = 64,
//         .Fmt = helio::rhi::Format::RGBA8_UNORM,
//         .Usage = helio::rhi::TextureUsage::Sampled | helio::rhi::TextureUsage::TransferDst,
//         .DebugName = "CheckerTex",
//         .InitialData = Checker.data(),
//         .InitialDataSize = Checker.size() * sizeof(uint32_t),
//     });
//
//     std::vector<uint32_t> SceneCounters(256);
//     for (uint32_t I = 0; I < SceneCounters.size(); ++I) SceneCounters[I] = I;
//     helio::rhi::BufferHandle SceneBuf = RHI.CreateBuffer({
//         .Size = SceneCounters.size() * sizeof(uint32_t),
//         .Usage = helio::rhi::BufferUsage::Storage | helio::rhi::BufferUsage::TransferDst,
//         .Memory = helio::rhi::MemoryUsage::DeviceLocal,
//         .DebugName = "SceneCounters",
//         .InitialData = SceneCounters.data(),
//         .InitialDataSize = SceneCounters.size() * sizeof(uint32_t),
//     });
//
//     helio::rhi::Device::BindlessUsage Usage = RHI.GetBindlessUsage();
//     HELIO_LOG_INFO("Game", "Bindless after init: sampledImages={}, storageImages={}, storageBuffers={}", Usage.SampledImagesUsed, Usage.StorageImagesUsed, Usage.StorageBuffersUsed);
//     HELIO_LOG_INFO("Game", "CheckerTex.SampledSlot={}  SceneBuf.BindlessSlot={}", CheckerTex.SampledSlot, SceneBuf.BindlessSlot);
//
//     // Phase 6: load the Triangle and FullscreenBlit pipelines.
//     auto TrianglePipe = RHI.CreateGraphicsPipeline({
//         .ShaderPath = "Shaders/Passes/Triangle.spv",
//         .ColorAttachmentCount = 1,
//         .DebugName = "Triangle",
//     });
//     auto BlitPipe = RHI.CreateGraphicsPipeline({
//         .ShaderPath = "Shaders/Passes/FullscreenBlit.spv",
//         .ColorAttachmentCount = 1,
//         .DebugName = "FullscreenBlit",
//     });
//
//     auto Albedo = RHI.CreateTexture({
//         .Width = 1920,
//         .Height = 1080,
//         .Fmt = helio::rhi::Format::RGBA8_SRGB,
//         .Usage = helio::rhi::TextureUsage::ColorAttachment | helio::rhi::TextureUsage::Sampled,
//         .DebugName = "Albedo",
//     });
//
//     auto Normal = RHI.CreateTexture({
//         .Width = 1920,
//         .Height = 1080,
//         .Fmt = helio::rhi::Format::RGBA16F,
//         .Usage = helio::rhi::TextureUsage::ColorAttachment | helio::rhi::TextureUsage::Sampled,
//         .DebugName = "Normal",
//     });
//
//     auto Emissive = RHI.CreateTexture({
//         .Width = 1920,
//         .Height = 1080,
//         .Fmt = helio::rhi::Format::RGBA8_SRGB,
//         .Usage = helio::rhi::TextureUsage::ColorAttachment | helio::rhi::TextureUsage::Sampled,
//         .DebugName = "Emissive",
//     });
//
//     auto GBufferPipe = RHI.CreateGraphicsPipeline({
//         .ShaderPath = "Shaders/Passes/GBuffer.spv", // would need a 3-output shader
//         .ColorFormats = {helio::rhi::Format::RGBA8_SRGB, helio::rhi::Format::RGBA16F, helio::rhi::Format::RGBA8_SRGB},
//         .ColorAttachmentCount = 3,
//         .DebugName = "GBuffer",
//     });
//
//     auto LightingPipe = RHI.CreateGraphicsPipeline({
//         .ShaderPath = "Shaders/Passes/Lighting.spv",
//         .ColorFormats = {helio::rhi::Format::BGRA8_SRGB},
//         .ColorAttachmentCount = 1,
//         .DebugName = "Lighting",
//     });
//
//     HELIO_LOG_INFO("Game", "Press ESC or close to exit.");
//
//     helio::core::Clock FrameClock;
//     uint64_t FrameIndex = 0;
//     while (Win.PumpEvents())
//     {
//         HELIO_PROFILE_FRAME();
//         HELIO_PROFILE_ZONE("Frame");
//
//         if (auto* Cmd = RHI.BeginFrame())
//         {
//             helio::rhi::ColorAttachment ColorTest[] = {
//                 {.Target = Albedo, .Load = helio::rhi::LoadOp::Clear, .ClearColor = {0.0f, 0.0f, 0.0f, 1.0f}},
//                 {.Target = Normal, .Load = helio::rhi::LoadOp::Clear, .ClearColor = {0.5f, 0.5f, 1.0f, 1.0f}},
//                 {.Target = Emissive, .Load = helio::rhi::LoadOp::Clear, .ClearColor = {1.0f, 0.0f, 0.0f, 1.0f}}
//             };
//
//             Cmd->BeginRendering(ColorTest, 3);
//             Cmd->Bind(GBufferPipe);
//             Cmd->Draw(3);
//
//             Cmd->EndRendering();
//
//             Cmd->TransitionForSampling(Albedo);
//             Cmd->TransitionForSampling(Normal);
//             Cmd->TransitionForSampling(Emissive);
//
//             Cmd->BeginRenderingToSwapchain(0.18f, 0.18f, 0.05f, 1.0f);
//             Cmd->Bind(LightingPipe);
//
//             struct LightingPC
//             {
//                 hlslpp::uint AlbedoSlot;
//                 hlslpp::uint NormalSlot;
//                 hlslpp::uint EmissiveSlot;
//                 hlslpp::uint SamplerSlot;
//             };
//
//             LightingPC lighting_pc{Albedo.SampledSlot, Normal.SampledSlot, Emissive.SampledSlot, 2};
//             Cmd->Push(lighting_pc);
//             Cmd->Draw(3);
//             Cmd->EndRendering();
//             
//             // Pulsing blue clear backing the triangle pass.
//             float T = static_cast<float>(StartupClock.SecondsSinceStart());
//             float Pulse = 0.5f + 0.5f * 0.5f * (1.0f + static_cast<float>(SDL_sin(T * 2.0)));
//             Cmd->BeginRenderingToSwapchain(0.05f * Pulse, 0.10f * Pulse, 0.6f * Pulse, 1.0f);
//
//             // First: fullscreen blit of CheckerTex (proves bindless + sampling).
//             struct BlitPC
//             {
//                 uint32_t TextureSlot;
//                 uint32_t SamplerSlot;
//             };
//             BlitPC PC{CheckerTex.SampledSlot, 1 /* kSamplerLinearWrap */};
//             Cmd->Bind(BlitPipe);
//             Cmd->Push(PC);
//             Cmd->Draw(3);
//
//             // Then: the colored triangle on top.
//             Cmd->Bind(TrianglePipe);
//             Cmd->Draw(3);
//
//             Cmd->EndRendering();
//             RHI.EndFrame();
//         }
//
//         ++FrameIndex;
//         (void)FrameClock.Tick();
//     }
//
//     RHI.WaitIdle();
//     HELIO_LOG_INFO("Game", "Main loop exited after {} frames.", FrameIndex);
//     helio::log::Shutdown();
//     return 0;
// }

using namespace helio;
using namespace helio::rhi;
using namespace helio::resource;
using namespace helio::render;
using namespace helio::platform::windows;
using namespace helio::input;
using namespace math;

int main()
{
    log::Init();

    Window Win({
        .Title = "Helio",
        .Width = 1920,
        .Height = 1080,
    });

    Device RHI({
        .NativeWindow = Win.Native(),
        .InitialWidth = Win.Width(),
        .InitialHeight = Win.Height(),
    });

    overlay::Overlay Hud(RHI, Format::RGBA8_SRGB);

    auto ComputePipe = RHI.CreateComputePipeline({
        .ShaderPath = "Shaders/Compute/Gradient.spv",
        .DebugName = "Gradient Compute",
    });

    TextureHandle Depth = RHI.CreateTexture({.Width = static_cast<uint32_t>(Win.Width()), .Height = static_cast<uint32_t>(Win.Height()), .Fmt = Format::D32_SFLOAT, .Usage = TextureUsage::DepthStencilAttachment, .DebugName = "Depth"});

    auto Raymarch = RHI.CreateComputePipeline({
        .ShaderPath = "Shaders/Compute/RayMarch.spv",
        .DebugName = "RayMarch Compute",
    });


    auto BlitPipe = RHI.CreateGraphicsPipeline({
        .ShaderPath = "Shaders/Passes/FullscreenBlit.spv",
        .ColorFormats = {Format::RGBA8_SRGB},
        .ColorAttachmentCount = 1,
        .DepthFormat = Format::D32_SFLOAT,
        .DepthTest = false,
        .DepthWrite = false,
        .DebugName = "FullscreenBlit",
    });

    auto TrianglePipe = RHI.CreateGraphicsPipeline({
        .ShaderPath = "Shaders/Passes/Triangle.spv",
        .ColorFormats = {Format::RGBA8_SRGB},
        .ColorAttachmentCount = 1,
        .DebugName = "Triangle",
    });

    auto Color = RHI.CreateTexture({
        .Width = static_cast<uint32_t>(Win.Width()), .Height = static_cast<uint32_t>(Win.Height()),
        .Fmt = Format::RGBA8_SRGB,
        .Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSrc,
        .DebugName = "Color",
    });

    auto Out = RHI.CreateTexture({
        .Width = static_cast<uint32_t>(Win.Width()), .Height = static_cast<uint32_t>(Win.Height()),
        .Fmt = Format::RGBA8_UNORM,
        .Usage = TextureUsage::Storage | TextureUsage::Sampled,
        .DebugName = "Gradient",
    });


    HELIO_LOG_INFO("Game", "Press ESC or close to exit.");

    helio::input::ActionMap map;
    map.BindKey("ToggleAdvanced", Key::Tab);
    Win.Dispatcher().SetActionMap(&map);

    Win.Dispatcher().OnActionPressed("ToggleAdvanced", [&]
    {
        Hud.ToggleAdvanced();
    });

    core::Clock FrameClock;
    uint64_t FrameIndex = 0;

    MeshSystem MS(RHI);

    auto CubeData = primitives::Cube(1.f);
    auto CubeMesh = MS.CreateMesh({.Data = &CubeData, .DebugName = "CubeMesh"});

    auto SphereData = primitives::Sphere(1.f, 32, 16);
    auto SphereMesh = MS.CreateMesh({.Data = &SphereData, .DebugName = "SphereMesh"});

    auto MeshPipeline = CreateMeshInstancedPipeline
    (RHI,
     {
         .ColorFormat = Format::RGBA8_SRGB,
         .DepthFormat = Format::D32_SFLOAT,
         .Cull = CullMode::Back,
         .DepthTest = true,
         .DepthWrite = true,
         .DebugName = "MeshPipeline"
     });

    InstanceBatch Batch(RHI, 1, "Cube Instances");

    auto Blur = RHI.CreateComputePipeline({
        .ShaderPath = "Shaders/Compute/BoxBlur.spv",
        .DebugName = "BoxBlur Compute",
    });

    Transform i;

    while (Win.PumpEvents())
    {
        HELIO_PROFILE_FRAME();
        HELIO_PROFILE_ZONE("Frame");
        const double FrameStartSec = FrameClock.SecondsSinceStart();

        if (auto* Cmd = RHI.BeginFrame())
        {
            RenderGraph rg(RHI, *Cmd);

            rg.Compute("Gradient")
              .Write(Out)
              .Execute([&](helio::rhi::CommandList& C)
              {
                  C.Bind(ComputePipe);
                  struct PC
                  {
                      uint32_t Slot;
                  };

                  PC pc{Out.StorageSlot};

                  C.Push(pc);
                  C.Dispatch2D(Win.Width(), Win.Height(), 8, 8);
              });

            // rg.Compute("RayMarch")
            //   .Write(Out)
            //   .Execute([&](helio::rhi::CommandList& C)
            //   {
            //       C.Bind(Raymarch);
            //       struct PC
            //       {
            //           uint32_t Slot;
            //           float Time;
            //           float Width;
            //           float Height;
            //       };
            //
            //       PC pc{Out.StorageSlot, static_cast<float>(FrameClock.SecondsSinceStart()), static_cast<float>(Win.Width()), static_cast<float>(Win.Height())};
            //
            //       C.Push(pc);
            //       C.Dispatch2D(Win.Width(), Win.Height(), 8, 8);
            //   });

            Batch.Begin();
            i.RotateAxis(float3(0, 1, 0), 2.f * FrameClock.Tick());
            Batch.Add(i);

            uint32_t Count = Batch.End();

            rg.Graphics("Color")
              .Color(Color)
              .Read(Out)
              .Depth(Depth, 0.f)
              .Execute([&](helio::rhi::CommandList& C)
              {
                  C.Bind(BlitPipe);
                  struct PC
                  {
                      uint32_t Tex, Sampler;
                  };

                  PC pc{Out.SampledSlot, 1 /* kSamplerLinearWrap */};
                  C.Push(pc);

                  C.Draw(3);
              });

            rg.Graphics("Meshes")
              .ColorLoad(Color)
              .Depth(Depth, 0.f)
              .Execute([&](helio::rhi::CommandList& C)
              {
                  MeshInstancedPushConsts MeshPC{};

                  float1 ra = 45.f;
                  float4x4 pers = math::PerspectiveReverseZLH(hlslpp::radians(ra), (float)Win.Width() / (float)Win.Height(), 0.001f);
                  MeshPC.ViewProj = mul(pers, math::LookAtLH(float3(0, 0, 10), float3(0), float3(0, 1, 0)));
                  MeshPC.VertexBufferSlot = CubeMesh.VertexBuffer.BindlessSlot;
                  MeshPC.InstanceBufferSlot = Batch.Buffer().BindlessSlot;
                  MeshPC.LightDirWS = float3(-0.5f, -0.8f, -0.3f);
                  MeshPC.AlbedoTint = float4(1.f, 0.5f, 0.5f, 1.0f);

                  C.Bind(MeshPipeline);
                  C.SetViewportFull();
                  C.Push(MeshPC);
                  C.BindIndexBuffer(CubeMesh.IndexBuffer, IndexTypeFor(CubeMesh));

                  C.DrawIndexed(CubeMesh.IndexCount, Count);
              });

            rg.Compute("Blur")
              .Read(Color)
              .Write(Out)
              .Execute([&](helio::rhi::CommandList& C)
              {
                  C.TransitionForSampling(Color);
                  C.TransitionForStorageWrite(Out);

                  C.Bind(Blur);
                  struct PC
                  {
                      uint32_t InputSlot, OutputSlot, SamplerSlot;
                      int Radius;
                      float Width;
                      float Height;
                  };

                  PC pc{Color.SampledSlot, Out.StorageSlot, 0/* kSamplerLinearWrap */, 2, static_cast<float>(Win.Width()), static_cast<float>(Win.Height())};
                  C.Push(pc);
                  C.Dispatch2D(Win.Width(), Win.Height(), 8, 8);
              });


            /*struct PC {
                uint  InputSlot;        // SampledSlot of the source texture
                uint  OutputSlot;       // StorageSlot of the destination texture (same size)
                uint  SamplerSlot;      // typically kSamplerLinearClamp
                int   Radius;           // odd half-extent: radius=2 means 5x5 kernel
                float Width;
                float Height;
            };*/

            // Queue overlay text + schedule its pass.
            const double NowSec = FrameClock.SecondsSinceStart();
            const double CpuMs = (NowSec - FrameStartSec) * 1000.0;
            Hud.DrawStats(CpuMs, RHI.LastFrameGpuMs(), rg.Passes());
            Hud.Render(rg, Out, static_cast<uint32_t>(Win.Width()), static_cast<uint32_t>(Win.Height()));

            rg.Present(Out);
            rg.Execute();

            RHI.EndFrame();
        }
    }

    RHI.WaitIdle();
    log::Shutdown();

    return 0;
}
