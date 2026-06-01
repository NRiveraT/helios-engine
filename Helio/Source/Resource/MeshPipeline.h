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
    uint32_t    VertexBufferSlot;    //  4  (offset 64)
    uint32_t    InstanceBufferSlot;  //  4  (offset 68)
    // HLSL packing forbids a float3 from straddling a 16-byte boundary, so
    // Slang inserts 8 bytes of padding here. We mirror that exactly — leaving
    // it out would cause vkCreateGraphicsPipelines to reject the layout (the
    // SPIR-V block reports a larger size than VkPushConstantRange's range).
    uint32_t    _PadA[2];            //  8  (offset 72)  — Slang-required pad
    Vec3Packed  LightDirWS;          // 12  (offset 80)  — must be normalized
    uint32_t    _Pad0;               //  4  (offset 92)
    Vec4Packed  AlbedoTint;          // 16  (offset 96)
};
static_assert(sizeof(MeshInstancedPushConsts) == 64 + 4 + 4 + 8 + 12 + 4 + 16,
              "MeshInstancedPushConsts must match MeshInstanced.slang's HLSL packing");
static_assert(sizeof(MeshInstancedPushConsts) == 112,
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
