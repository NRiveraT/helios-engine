/// @file SceneRenderer.h
/// @brief Renders a HelioWorld: batching, shadow pass, mesh pass, overlays.
///
/// Frame flow (all passes share the engine-wide conventions — reverse-Z,
/// Y-flipped projections, `FrontFace::Clockwise`, clear-depth 0.0):
///   1. Actors submit draws via `OnRender(SceneRenderer&)` → `SubmitMesh`.
///      World-space caster bounds accumulate here for shadow fitting.
///   2. `BatchMeshInstances` packs all transforms into one ring-buffered
///      instance buffer (one `MeshDraw` per mesh → one DrawIndexed each).
///   3. Per-frame `` (view-proj, light, shadow matrix + params)
///      upload once into a bindless storage buffer — push constants carry
///      only per-draw slots + material scalars.
///   4. Passes: "Shadow Map" (depth-only, light-fitted ortho) → "Static
///      Meshes" (samples the shadow map, PCF) → debug lines → stats overlay
///      → optional overlay hook (editor UI) → present.
///
/// Directional-shadow fitting: the casters' world AABB is wrapped in a
/// bounding sphere (rotation-invariant, so the ortho extent never wobbles as
/// the light turns), the light-space center is snapped to shadow-texel
/// increments (kills edge shimmer on static scenes), and near/far pancake
/// the sphere. See `BuildShadowData` in the .cpp for the exact math.
#pragma once

#include <Core/Math/Math.h>
#include <Core/Math/Transform.h>
#include <Core/Time/Clock.h>

#include <Renderer/Debug/DebugDraw.h>
#include <Renderer/Overlay/Overlay.h>
#include <Renderer/RenderGraph.h>

#include <Resource/InstanceBatch.h>
#include <Resource/Material.h>
#include <Resource/Mesh.h>

#include <RHI/Public/Device.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include <Logging/Log.h>

namespace helio::scene
{
    class HelioWorld;
    class Camera;
    class DirectionalLight;

    /// One instanced draw: every instance of one mesh, one material.
    struct MeshDraw
    {
        resource::Mesh Mesh;
        resource::Material Material;
        uint32_t FirstInstance;
        uint32_t InstanceCount;
    };

    /// A submitted mesh instance, staged until batching.
    struct PendingInstance
    {
        resource::Mesh Mesh;
        resource::Material Material;
        float4x4 World; // already converted from Transform
    };

    struct GPULight
    {
        float Position[3];
        uint32_t LightType;
        float Color[3];
        float Intensity;
        float Direction[3];
        float Range;
        float CosInner;
        float CosOuter;
        float Pad[2];
    };
    static_assert(sizeof(GPULight) == 64, "GPULight buffer must match shader");

    /// CPU mirror of `Shaders/Common/Frame.slang` — per-frame constants,
    /// uploaded once per frame into a bindless storage buffer.
    struct FrameConstants
    {
        Mat4Packed ViewProj;          // offset 0
        Mat4Packed LightViewProj;     // offset 64
        Vec4Packed CameraPosWS;       // offset 128: xyz, w unused
        Vec4Packed LightDirWS;        // offset 144: xyz = travel dir (normalized), w = intensity
        Vec4Packed LightColorAmbient; // offset 160: rgb = light color, w = ambient
        Vec4Packed ShadowParams;      // offset 176: x = uv texel, y = normal offset (world),
                                      //             z = receiver depth bias (NDC), w = enabled
        uint32_t ShadowMapSlot;       // offset 192
        uint32_t LightBufferSlot;     // offset 196
        uint32_t LightCount;          // offset 200
        uint32_t Pad2;    // offset 196

        Mat4Packed Proj;
        Mat4Packed InvProj;
        Vec4Packed ViewportAO;      
    };
    static_assert(sizeof(FrameConstants) == 352, "must match Shaders/Common/Frame.slang");

    enum class DebugViewMode : uint32_t
    {
        Lit,
        Depth,
        WorldNormals,
        WorldPosition,
        AmbientOcclusion,
        LightingOnly,
        Count
    };

    static constexpr const char* DebugViewModeToString(DebugViewMode Mode)
    {
        switch (Mode)
        {
        case DebugViewMode::Lit:
            return "Default Lit";
        case DebugViewMode::Depth:
            return "Depth";
        case DebugViewMode::WorldNormals:
            return "World Normal";
        case DebugViewMode::WorldPosition:
            return "World Position";
        case DebugViewMode::AmbientOcclusion:
            return "Ambient Occlusion";
        case DebugViewMode::LightingOnly:
            return "Lighting Only";
        }
    }
    
    class SceneRenderer
    {
    public:
        static constexpr uint32_t kShadowMapResolution = 1024;
        static constexpr uint32_t kMaxInstances = 16384;

        SceneRenderer(rhi::Device& RHI, int Width, int Height);
        ~SceneRenderer();

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;

        void SetWorld(HelioWorld& World) noexcept { m_World = &World; }
        void SetRenderingCamera(Camera* Cam) noexcept { m_Camera = Cam; }
        [[nodiscard]] Camera* GetRenderingCamera() const noexcept { return m_Camera; }

        /// Hook declared after every scene pass and before Present — the
        /// editor UI renders here. Receives the graph, the final color target
        /// and its pixel size. Set an empty function to remove.
        using OverlayHook = std::function<void(render::RenderGraph&, rhi::TextureHandle, uint32_t, uint32_t)>;
        void SetOverlayHook(OverlayHook Hook) { m_OverlayHook = std::move(Hook); }

        void Render();
        void WaitIdle() const;

        /// Recreate the window-sized color/depth targets at a new resolution.
        /// Call from the resize handler (the shadow map is fixed-size and
        /// untouched). No-op for degenerate or unchanged sizes.
        void Resize(int Width, int Height);
        [[nodiscard]] int GetWidth() const noexcept { return m_Width; }
        [[nodiscard]] int GetHeight() const noexcept { return m_Height; }

        DebugViewMode GetDebugViewMode() const noexcept { return m_DebugViewMode; }
        
        /// Queue one instance of `M` for this frame. `WorldTransform` is the
        /// actor's WORLD transform (the scene graph already composed parents).
        void SubmitMesh(const resource::Mesh& M, const resource::Material& Mat, const Transform& WorldTransform);

        /// Matrix overload — used when the caller already has a composed 4x4
        /// (e.g. `actorWorld * sectionLocal` for a placed mesh section), so no
        /// `Transform` round-trip is needed. The `Transform` overload forwards
        /// here after `ToMatrix()`.
        void SubmitMesh(const resource::Mesh& M, const resource::Material& Mat, const float4x4& WorldMatrix);

        [[nodiscard]] void SetDebugViewMode(DebugViewMode Mode) noexcept
        {
            m_DebugViewMode = Mode;
        }
        /// CPU time spent inside the previous `Render()` call, in ms — for
        /// editor stats (GPU time comes from `Device::LastFrameGpuMs`).
        [[nodiscard]] double LastRenderCpuMs() const noexcept { return m_LastCpuMs; }

    private:
        DebugViewMode m_DebugViewMode = DebugViewMode::Lit;
        
        /// Everything the shadow pass + mesh pass need to know about the sun.
        struct ShadowData
        {
            float4x4 ViewProj = math::Identity();
            float TexelWorldSize = 0.0f; // world units per shadow texel
            float DepthRange = 1.0f;     // world units mapped onto NDC [1..0]
            bool Enabled = false;
        };

        [[nodiscard]] ShadowData BuildShadowData(const DirectionalLight& Light) const;
        void BatchMeshInstances();

        [[nodiscard]] void PrepSSAOData();
        
        rhi::Device* m_RHI;

        rhi::TextureHandle m_ColorTexture;
        rhi::TextureHandle m_NormalTexture;
        rhi::TextureHandle m_DepthTexture;
        rhi::TextureHandle m_ShadowMapTexture;

        rhi::TextureHandle m_PostProcessColor;

        // WIP
        rhi::TextureHandle m_AO;
        
        rhi::PipelineHandle m_DepthPrepassPipeline;
        rhi::PipelineHandle m_AmbientOcclusionPipeline;

        rhi::PipelineHandle m_MeshPipeline;
        rhi::PipelineHandle m_ShadowMapPipeline;

        rhi::PipelineHandle m_PostProcessPipeline;

        rhi::PipelineHandle m_DebugViewModePipeline;
        
        render::overlay::Overlay m_Overlay;
        render::debug::DebugDraw m_DebugDraw;

        HelioWorld* m_World = nullptr;
        Camera* m_Camera = nullptr;
        OverlayHook m_OverlayHook;

        // Grouped by mesh ID so all instances of one mesh draw in one call.
        std::unordered_map<uint64_t, std::vector<PendingInstance>> m_PendingInstances;
        std::vector<MeshDraw> m_Draws;
        math::AABB m_CasterBounds; // world-space, this frame's submissions

        resource::InstanceBatch m_InstanceBatch;   // ring-buffered per frame
        rhi::RingUploadBuffer m_LightBufferRing; // one FrameConstants per frame slot
        rhi::RingUploadBuffer m_FrameConstantsRing; // one FrameConstants per frame slot

        core::Clock m_Clock;
        double m_StartFrameSec = 0.0;
        double m_LastCpuMs = 0.0;

        int m_Width = 0;
        int m_Height = 0;
    };
} // namespace helio::scene
