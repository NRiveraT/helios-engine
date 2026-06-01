# Bindless

Helio uses a single global descriptor set (`set 0`) that every shader sees. Resources get a numeric slot at creation time; shaders index into the set via that slot. No per-draw descriptor sets, no descriptor set permutations, no `vkAllocateDescriptorSets` per material.

## Layout

| Binding | Type | Slot count | Purpose |
|---|---|---|---|
| 0 | `SAMPLED_IMAGE` | 16 384 | Textures sampled from shaders |
| 1 | `STORAGE_IMAGE` | 4 096 | Compute writes (write-back targets) |
| 2 | `STORAGE_BUFFER` | 4 096 | Vertex / index / material / scene buffers |
| 3 | `SAMPLER` | 4 static | linear/point × clamp/wrap |
| 4 | `ACCELERATION_STRUCTURE_KHR` | 1 | The scene TLAS (Phase 8) |

All bindings are `UPDATE_AFTER_BIND` + `PARTIALLY_BOUND`, so unused slots are safe and you can write a slot while the set is bound (as long as no in-flight frame is *reading* that slot — typically you make new resources visible in the next frame).

The 4 static sampler slots (matched by `Shaders/Common/Bindless.slang`):

| Index | Name | Filter | Address mode |
|---|---|---|---|
| 0 | `kSamplerLinearClamp` | linear | clamp-to-edge |
| 1 | `kSamplerLinearWrap`  | linear | repeat |
| 2 | `kSamplerPointClamp`  | nearest | clamp-to-edge |
| 3 | `kSamplerPointWrap`   | nearest | repeat |

## Allocating slots

You don't allocate slots directly — they come back on the handle when you create the resource:

```cpp
auto Tex = RHI.CreateTexture({
    .Width  = 256, .Height = 256,
    .Fmt    = Format::RGBA8_UNORM,
    .Usage  = TextureUsage::Sampled,
    .DebugName = "MyTexture",
});
// Tex.SampledSlot is your bindless index. Push it to a shader.

auto Buf = RHI.CreateBuffer({
    .Size  = 64 * 1024,
    .Usage = BufferUsage::Storage,
    .Memory = MemoryUsage::DeviceLocal,
    .DebugName = "MyBuffer",
});
// Buf.BindlessSlot is your storage-buffer slot.
```

If you create a texture with both `Sampled` and `Storage` usage, both `SampledSlot` and `StorageSlot` are populated — same image, two descriptor entries (one as `SAMPLED_IMAGE`, one as `STORAGE_IMAGE`).

`SampledSlot` / `StorageSlot` / `BindlessSlot` are `UINT32_MAX` when the corresponding usage flag wasn't set on creation. Always branch on a "slot != UINT32_MAX" check if you're unsure.

## Reading slots in Slang

```hlsl
import Bindless;

struct PC { uint TextureSlot; uint SamplerSlot; };
[[vk::push_constant]] PC pc;

[shader("fragment")]
float4 PSMain(float2 UV : TEXCOORD0) : SV_Target {
    return GetTexture2D(pc.TextureSlot).Sample(GetSampler(pc.SamplerSlot), UV);
}
```

Available helpers in [`Helio/Shaders/Common/Bindless.slang`](../Shaders/Common/Bindless.slang):

| Helper | Returns |
|---|---|
| `GetTexture2D(slot)` | `Texture2D<float4>` for sampling |
| `GetStorageImage(slot)` | `RWTexture2D<float4>` for compute writes |
| `GetStorageBuffer(slot)` | `ByteAddressBuffer` (use `.Load<T>(byteOffset)` to read) |
| `GetSampler(slot)` | `SamplerState` |
| `GetTLAS()` | `RaytracingAccelerationStructure` |

All non-sampler helpers wrap `NonUniformResourceIndex(slot)` for divergent indexing, so you can index by per-thread data.

## Uploading data

`CreateBuffer` / `CreateTexture` accept an optional `InitialData` + `InitialDataSize` pair. The RHI stages it via an internal upload helper and the resource is ready when the call returns. For DeviceLocal memory the upload uses a transient command buffer + fence wait (synchronous).

For dynamic uploads (per-frame UBOs, streaming geometry), create with `MemoryUsage::HostUpload` and write directly through the mapped pointer (the RHI handles mapping internally — call `UploadToBuffer` and it picks the right path).

```cpp
// Streaming write to a per-frame UBO.
auto Ubo = RHI.CreateBuffer({
    .Size   = sizeof(MyFrameData),
    .Usage  = BufferUsage::Storage,
    .Memory = MemoryUsage::HostUpload,
});
// Per frame:
RHI.UploadToBuffer(Ubo, 0, &frameData, sizeof(frameData));
```

## Push-constant payload pattern

The standard pattern: push a small struct of bindless indices plus any per-draw scalars. Pipelines default to 128 bytes of push constants — enough for 32 uint slots, or a 4x4 matrix + 16 indices, etc.

```cpp
struct DrawPC {
    uint4x4 World;        // 64 B
    uint AlbedoSlot;
    uint NormalSlot;
    uint MaterialSlot;
    uint VertexBufferSlot;
    uint IndexBufferSlot;
    // ... up to 128 B total
};
Cmd->Push(myDrawPC);
```

Don't push large blobs through push constants — for anything bigger, allocate a small `Storage` buffer and push its `BindlessSlot` instead.

## Deletion + slot reuse

`DestroyTexture` / `DestroyBuffer` queue the destruction against the current frame-in-flight slot. The actual `vkDestroyImage` + slot return-to-freelist runs after the GPU retires that slot (typically ~2 frames later). You never see use-after-free issues from this — but if you destroy a resource and the next frame samples its (now-freed) slot before a new resource grabs it, you'll get whatever the new tenant is. Use the handle, not the raw slot integer.

## Quick "I want to see it" recipe

End-to-end: upload a procedural texture, sample it from a fullscreen-blit pipeline through the bindless slot, display on screen. This is essentially what the CheckerTex demo in your main.cpp does:

```cpp
#include <Core/Logging/Log.h>
#include <Platform/Windows/Window.h>
#include <RHI/Public/Device.h>

#include <vector>
#include <cstdint>

int main() {
    helio::log::Init();

    helio::platform::windows::Window Win({ .Title = "Bindless demo", .Width = 1280, .Height = 720 });
    helio::rhi::Device RHI({ .NativeWindow = Win.Native(),
                             .InitialWidth = Win.Width(), .InitialHeight = Win.Height() });

    // 1. Generate a 64x64 checkerboard procedurally.
    std::vector<uint32_t> Pixels(64 * 64);
    for (uint32_t Y = 0; Y < 64; ++Y)
        for (uint32_t X = 0; X < 64; ++X)
            Pixels[Y * 64 + X] = ((X >> 3) ^ (Y >> 3)) & 1 ? 0xFFFFFFFFu : 0xFF202020u;

    // 2. CreateTexture with `Sampled` → grabs a bindless slot, uploads via staging.
    auto Tex = RHI.CreateTexture({
        .Width = 64, .Height = 64,
        .Fmt   = helio::rhi::Format::RGBA8_UNORM,
        .Usage = helio::rhi::TextureUsage::Sampled | helio::rhi::TextureUsage::TransferDst,
        .DebugName = "Checker",
        .InitialData = Pixels.data(),
        .InitialDataSize = Pixels.size() * sizeof(uint32_t),
    });
    HELIO_LOG_INFO("Game", "Checker landed at bindless slot {}", Tex.SampledSlot);

    // 3. Pipeline that samples via push-constant'd slot indices.
    auto BlitPipe = RHI.CreateGraphicsPipeline({
        .ShaderPath = "Shaders/Passes/FullscreenBlit.spv",  // ships with engine
        .ColorFormats = { helio::rhi::Format::BGRA8_SRGB },
        .ColorAttachmentCount = 1,
        .DebugName = "Blit",
    });

    // 4. Per-frame: push (texture slot, sampler slot), Draw(3).
    while (Win.PumpEvents()) {
        if (auto* Cmd = RHI.BeginFrame()) {
            Cmd->BeginRenderingToSwapchain(0, 0, 0, 1);
            Cmd->Bind(BlitPipe);
            struct PC { uint32_t TexSlot, SamplerSlot; } pc{ Tex.SampledSlot, 1 /* kSamplerLinearWrap */ };
            Cmd->Push(pc);
            Cmd->Draw(3);
            Cmd->EndRendering();
            RHI.EndFrame();
        }
    }

    RHI.WaitIdle();
    helio::log::Shutdown();
}
```

Run it — you'll see the 64×64 checker stretched fullscreen. The texture lives in bindless slot 0, the shader reads it via `GetTexture2D(pc.TexSlot)` (see `FullscreenBlit.slang`), and you never touched a Vulkan descriptor type by hand.

Adding more textures = call `CreateTexture` again; each one gets the next free slot. Pushing a different slot in PC = sampling a different texture from the same pipeline. That's the whole point of bindless — one pipeline, any number of textures, picked per-draw via push constants.
