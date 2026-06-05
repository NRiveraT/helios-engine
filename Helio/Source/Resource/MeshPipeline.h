/// @file MeshPipeline.h
/// @brief One-call factory for the canonical instanced-mesh graphics pipeline.
///
/// Wraps `Shaders/Passes/MeshInstanced.slang` with the right push-constant
/// size, depth state, cull mode, and color/depth formats. Game code stays
/// out of pipeline-desc plumbing for the common static-mesh draw.
///
/// Push-constant layout (match `MeshInstancedPushConsts` below):
///   float4x4 ViewProj
///   uint     VertexBufferSlot
///   uint     InstanceBufferSlot
///   float3   LightDirWS
///   uint     _Pad0
///   float4   AlbedoTint
#pragma once

#include <Core/Math/Math.h>

#include <RHI/Public/Formats.h>
#include <RHI/Public/Pipeline.h>

#include <cstdint>

#include "Material.h"

namespace helio::rhi { class Device; }

namespace helio::resource {
        
/// CPU-side mirror of MeshInstanced.slang's push-constant struct. Build one
/// per draw and pass to `CommandList::Push(pc)`.
///
/// Usage stays clean by leaning on the `Mat4Packed` / `Vec3Packed` / `Vec4Packed`
/// types from `Core/Math/Math.h`:
///
///     MeshInstancedPushConsts PC{};
///     PC.ViewProj           = ViewProj;            // float4x4 → packed+transposed
///     PC.VertexBufferSlot   = Mesh.VertexBuffer.BindlessSlot;
///     PC.InstanceBufferSlot = Batch.Buffer().BindlessSlot;
///     PC.LightDirWS         = float3(-0.5, -0.8, -0.3);
///     PC.AlbedoTint         = float4(1, 1, 1, 1);
struct MeshInstancedPushConsts {
    Mat4Packed  ViewProj;            // 64  (offset 0)
    
    Vec4Packed  LightDirWS;          // 16  (offset 80)  — must be normalized

    Vec3Packed Albedo;
    float Roughness;
    
    Vec3Packed  CameraPosWS;
    float Metallic;
    
    uint32_t    VertexBufferSlot;    //  4  (offset 64)
    uint32_t    InstanceBufferSlot;  //  4  (offset 68)
    // Start index of this draw's instances within the (shared) instance
    // buffer. The shader reads from `InstanceBufferSlot[InstanceBase + InstanceID]`
    // so multiple meshes can pack into one buffer and each draw addresses
    // its own slice. Set this to `D.FirstInstance` per draw.
    uint32_t    InstanceBase;        //  4  (offset 72)
};
    
static_assert(sizeof(MeshInstancedPushConsts) == 64 + 16 + 12 + 4 + 12 + 4 + 4 + 4 + 4,
              "MeshInstancedPushConsts must match MeshInstanced.slang's HLSL packing");
static_assert(sizeof(MeshInstancedPushConsts) == 124,
              "Slang's reported block size for MeshInstanced is 112 B");
static_assert(sizeof(MeshInstancedPushConsts) <= 128,
              "push constants must fit Vulkan's 128-byte minimum");

/// Description for `CreateMeshInstancedPipeline`. Most fields default to
/// "draw onto an opaque LH reverse-Z color+depth target".
struct MeshInstancedPipelineDesc {
    rhi::Format ColorFormat{rhi::Format::BGRA8_SRGB};
    rhi::Format DepthFormat{rhi::Format::D32_SFLOAT};
    rhi::CullMode Cull{rhi::CullMode::Back};
    /// Reverse-Z by default: clear depth to 0.0 at far, write 1.0 at near.
    rhi::CompareOp DepthCompare{rhi::CompareOp::Greater};
    bool          DepthTest {true};
    bool          DepthWrite{true};
    /// Default `Clockwise` because `PerspectiveReverseZLH` negates Y to get
    /// world-up = screen-up under Vulkan's Y-down framebuffer. That negation
    /// flips the signed-area sign of every triangle, so what was math-CCW
    /// (the convention for our procedural primitives and glTF) arrives as
    /// Vulkan-CW. Setting front-face = CW restores correct culling.
    rhi::FrontFace Front{rhi::FrontFace::Clockwise};
    const char* DebugName{"MeshInstanced"};
};

/// Build the pipeline. Returns an invalid handle on shader-load failure
/// (check `IsValid()`). Pipeline is owned by Device and must be destroyed
/// via `Device::DestroyPipeline`.
[[nodiscard]] rhi::PipelineHandle CreateMeshInstancedPipeline(
    rhi::Device& Dev,
    const MeshInstancedPipelineDesc& Desc = {});

} // namespace helio::resource
