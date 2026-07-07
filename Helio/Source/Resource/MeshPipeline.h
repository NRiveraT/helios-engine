/// @file MeshPipeline.h
/// @brief One-call factory for the canonical instanced-mesh graphics pipeline.
///
/// Wraps `Shaders/Passes/MeshInstanced.slang` with the right push-constant
/// size, depth state, cull mode, and color/depth formats. Game code stays
/// out of pipeline-desc plumbing for the common static-mesh draw.
///
/// Per-FRAME data (view-projection, light, shadow matrix + params) lives in
/// the `FrameConstants` bindless storage buffer (see `scene::FrameConstants`
/// and `Shaders/Common/Frame.slang`) — push constants carry only per-DRAW
/// slots and material scalars, so they stay tiny and the 128-byte Vulkan
/// minimum is never a constraint again.
#pragma once

#include <Core/Math/Math.h>

#include <RHI/Public/Formats.h>
#include <RHI/Public/Pipeline.h>

#include <cstdint>

#include "Material.h"

namespace helio::rhi { class Device; }

namespace helio::resource {

/// CPU mirror of the `PC` struct in `Shaders/Passes/MeshInstanced.slang`.
/// Every float3 starts on a 16-byte boundary so the C++ layout matches both
/// HLSL cbuffer packing and std430.
struct MeshInstancedPushConsts {
    uint32_t   FrameSlot;          //  0 — bindless slot of this frame's FrameConstants
    uint32_t   VertexBufferSlot;   //  4
    uint32_t   InstanceBufferSlot; //  8
    /// Start index of this draw's instances within the shared instance
    /// buffer. `SV_InstanceID` restarts at 0 per draw (it ignores
    /// vkCmdDrawIndexed's firstInstance), so the shader indexes
    /// `InstanceBase + SV_InstanceID`.
    uint32_t   InstanceBase;       // 12

    Vec3Packed Albedo;             // 16
    float      Roughness;          // 28

    Vec3Packed Emissive;           // 32 — EmissiveColor * EmissiveIntensity, premultiplied
    float      Metallic;           // 44

    // Material texture bindless slots (Material::kNoTexture = none). All uint,
    // scalar-packed, matching the trailing uints in MeshInstanced.slang's PC.
    uint32_t   AlbedoTex;          // 48
    uint32_t   NormalTex;          // 52
    uint32_t   MetalRoughTex;      // 56
    uint32_t   EmissiveTex;        // 60
    uint32_t   OcclusionTex;       // 64
};
static_assert(sizeof(MeshInstancedPushConsts) == 68, "must match the PC block in Shaders/Passes/MeshInstanced.slang");

struct DepthPrepassPushConsts
{
    uint32_t FrameSlot;
    uint32_t VertexBufferSlot;
    uint32_t InstanceBufferSlot;
    uint32_t InstanceBase;
};
static_assert(sizeof(DepthPrepassPushConsts) == 16, "must match the PC block in Shaders/Passes/DepthPrepass.slang");

struct PostProcessPushConstants
{
    uint32_t SourceSlot;
};

struct DebugViewPushConst
{
    uint32_t Mode; // 1 = depth, 2 = world normal,
    uint32_t SourceSlot;
    float NearZ;
    float DebugFar;
};
    static_assert(sizeof(DebugViewPushConst) == 16, "must match the PC block in Shaders/Debug/DebugViewMode.slang");
    
/// CPU mirror of the `PC` struct in `Shaders/Passes/ShadowMap.slang`.
/// The light's view-projection comes from FrameConstants.
struct ShadowMapPushConsts {
    uint32_t FrameSlot;          //  0
    uint32_t VertexBufferSlot;   //  4
    uint32_t InstanceBufferSlot; //  8
    uint32_t InstanceBase;       // 12
};
static_assert(sizeof(ShadowMapPushConsts) == 16, "must match the PC block in Shaders/Passes/ShadowMap.slang");

/// Description for `CreateMeshInstancedPipeline`. Most fields default to
/// "draw onto an opaque LH reverse-Z color+depth target".
struct MeshInstancedPipelineDesc {
    rhi::Format ColorFormat{rhi::Format::BGRA8_SRGB};
    rhi::Format NormalFormat{rhi::Format::RGBA16F};
    rhi::Format DepthFormat{rhi::Format::D32_SFLOAT};
    rhi::CullMode Cull{rhi::CullMode::Back};
    /// Reverse-Z by default: clear depth to 0.0 at far, write 1.0 at near.
    rhi::CompareOp DepthCompare{rhi::CompareOp::GreaterEq};
    bool          DepthTest {true};
    bool          DepthWrite{false};
    /// Default `Clockwise` because every projection in `Core/Math` negates Y
    /// to get world-up = screen-up under Vulkan's Y-down framebuffer. That
    /// negation flips the signed-area sign of every triangle, so what was
    /// math-CCW (the convention for our procedural primitives and glTF)
    /// arrives as Vulkan-CW. Setting front-face = CW restores correct culling.
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
