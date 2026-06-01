# Compute

Helio has had compute pipelines since Phase 6. This doc walks through what's wired, the three canonical compute "shapes" engines build, and includes runnable example shaders that ship with the engine.

## What's available

| Layer | API |
|---|---|
| Pipeline | `RHI.CreateComputePipeline({ .ShaderPath = "...", .Entry = "CSMain" })` |
| Bind | `Cmd->Bind(computePipe)` (same call as graphics) |
| Push constants | `Cmd->Push(myStruct)` — ≤ 128 B |
| Dispatch | `Cmd->Dispatch(x, y, z)` or `Cmd->Dispatch2D(SizeX, SizeY, GroupX, GroupY)` |
| Storage image (write) | `CreateTexture({ .Usage = Storage \| Sampled })` → `tex.StorageSlot` |
| Storage buffer (R/W) | `CreateBuffer({ .Usage = Storage })` → `buf.BindlessSlot` |
| Pre-write transition | `Cmd->TransitionForStorageWrite(tex)` (color → `GENERAL`) |
| Pre-sample transition | `Cmd->TransitionForSampling(tex)` (`GENERAL` → `SHADER_READ_ONLY_OPTIMAL`) |
| Slang-side read | `GetStorageImage(slot)`, `GetStorageBuffer(slot)`, `GetTexture2D(slot)` |

All routed through the same bindless descriptor set as graphics — no separate compute layout to manage. Compute pipelines share the same push-constant range too.

## The three compute shapes you'll typically build

### 1. Write to a storage image — post-process, generation, RT output

The most common shape. Compute fills an image; a later graphics pass samples it. See [`Compute/Gradient.slang`](../Shaders/Compute/Gradient.slang) for the minimum-viable version, [`Compute/RayMarch.slang`](../Shaders/Compute/RayMarch.slang) for an actually-interesting one.

```cpp
// Once:
auto Pipe = RHI.CreateComputePipeline({
    .ShaderPath = "Shaders/Compute/Gradient.spv",
    .DebugName  = "Gradient",
});
auto Img = RHI.CreateTexture({
    .Width = 1920, .Height = 1080,
    .Fmt   = Format::RGBA8_UNORM,
    .Usage = TextureUsage::Storage | TextureUsage::Sampled,
    .DebugName = "GradientOut",
});

// Per frame:
Cmd->TransitionForStorageWrite(Img);
Cmd->Bind(Pipe);
struct PC { uint32_t OutputSlot; } pc{ Img.StorageSlot };
Cmd->Push(pc);
Cmd->Dispatch2D(1920, 1080, 8, 8);
Cmd->TransitionForSampling(Img);
// ... bind FullscreenBlit + push Img.SampledSlot to display
```

### 2. Read + write storage buffers — GPU-driven simulation

GPU-driven culling, particle systems, cloth, fluid sims, bitonic sort. The data lives only on the GPU; CPU never sees it after upload.

```cpp
struct Particle { float3 Pos; float3 Vel; };
std::vector<Particle> Init(MaxParticles);
auto ParticleBuf = RHI.CreateBuffer({
    .Size  = sizeof(Particle) * MaxParticles,
    .Usage = BufferUsage::Storage,
    .Memory = MemoryUsage::DeviceLocal,
    .InitialData = Init.data(),
    .InitialDataSize = sizeof(Particle) * MaxParticles,
});

// Per frame:
Cmd->Bind(UpdatePipe);
struct PC { uint32_t Slot; float Dt; } pc{ ParticleBuf.BindlessSlot, dt };
Cmd->Push(pc);
Cmd->Dispatch((MaxParticles + 63) / 64, 1, 1);
```

Slang side:

```hlsl
import Bindless;
[[vk::push_constant]] struct PC { uint Slot; float Dt; } pc;

struct Particle { float3 Pos; float Pad0; float3 Vel; float Pad1; };

[shader("compute")]
[numthreads(64, 1, 1)]
void CSMain(uint3 TID : SV_DispatchThreadID) {
    ByteAddressBuffer Buf = GetStorageBuffer(pc.Slot);
    // For RW use RWByteAddressBuffer — but Slang doesn't bind RW via GetStorageBuffer.
    // Pattern: store the buffer at TWO slots (read at Slot, write at Slot+1)
    // OR push the underlying VkBuffer's deviceAddress and use `vk::RawBufferLoad/Store`
    // OR (simplest in V1) use the same buffer for both and accept that compute
    // can read-then-write within one dispatch via `RWByteAddressBuffer`:
    //   [[vk::binding(2,0)]] RWByteAddressBuffer g_RW[];
    // Phase 13 polish will add a dedicated `GetRWStorageBuffer(slot)` helper.

    uint Idx = TID.x;
    Particle P = Buf.Load<Particle>(Idx * 32);
    P.Pos += P.Vel * pc.Dt;
    // ... see future Particle.slang example
}
```

> **Note**: the read-only `ByteAddressBuffer` binding wraps the same slot, but for cross-pass write-then-read it's clearer to keep two slots (one as the producer's `Storage` write target, one as the consumer's `Sampled`/read target — they alias the same VkBuffer in V1). A first-class `RWStorageBuffer` helper is on the Phase 13 backlog.

### 3. Multi-input compute — image-space passes (SSAO, blur, tonemap)

Read one or more sampled textures + ancillary params, write a new one. The canonical "image-in, image-out" shape. See [`Compute/BoxBlur.slang`](../Shaders/Compute/BoxBlur.slang).

Pattern: depth + normal in → AO out (real SSAO needs hemisphere sampling + a noise texture for randomized sample sets; the box blur shows the input/output plumbing without the geometry math distraction).

```cpp
struct AOPC {
    uint32_t DepthSlot;
    uint32_t NormalSlot;
    uint32_t OutputSlot;
    uint32_t SamplerSlot;
    float Radius;
    // ... project matrix slot, image size, etc.
};
```

## Example shaders that ship

All three live under [`Helio/Shaders/Compute/`](../Shaders/Compute/) and auto-compile into `Shaders/Compute/*.spv` next to `Game.exe`:

### `Gradient.slang` — trivial reference

Writes UV → (R,G,0.5,1). Smallest possible compute shader. Useful as a "did the pipeline + slot + dispatch chain work at all" check.

```cpp
auto Pipe = RHI.CreateComputePipeline({ .ShaderPath = "Shaders/Compute/Gradient.spv" });
auto Out  = RHI.CreateTexture({ .Width=1920, .Height=1080, .Fmt=Format::RGBA8_UNORM,
                                .Usage=TextureUsage::Storage|TextureUsage::Sampled });
// Per frame:
Cmd->TransitionForStorageWrite(Out);
Cmd->Bind(Pipe);
struct { uint32_t OutputSlot; } pc{ Out.StorageSlot };
Cmd->Push(pc);
Cmd->Dispatch2D(1920, 1080, 8, 8);
Cmd->TransitionForSampling(Out);
```

### `RayMarch.slang` — fully self-contained animated 3D demo

SDF ray marcher: sphere + ground plane + sun + sky. No scene data, no buffers, no textures — just math. Hits per pixel are ~10-30 iterations of `length(p) - r` style functions. Great for showing the raw flexibility of compute without any asset pipeline involvement.

```cpp
auto Pipe = RHI.CreateComputePipeline({ .ShaderPath = "Shaders/Compute/RayMarch.spv" });
auto Out  = RHI.CreateTexture({ .Width=1920, .Height=1080, .Fmt=Format::RGBA8_UNORM,
                                .Usage=TextureUsage::Storage|TextureUsage::Sampled,
                                .DebugName="RayMarchOut" });

// Per frame:
struct PC {
    uint32_t OutputSlot;
    float Time, Width, Height;
} pc{ Out.StorageSlot, float(StartupClock.SecondsSinceStart()), 1920.0f, 1080.0f };

Cmd->TransitionForStorageWrite(Out);
Cmd->Bind(Pipe);
Cmd->Push(pc);
Cmd->Dispatch2D(1920, 1080, 8, 8);
Cmd->TransitionForSampling(Out);
// blit Out.SampledSlot to swapchain via FullscreenBlit
```

What you'll see: a blue sphere bouncing left/right above a checkered ground, lit by a sun, against a soft gradient sky. All from compute. Adjust the `Scene()` function in the shader to add primitives; try `smin` instead of `min` to get blobby blended SDFs.

### `BoxBlur.slang` — multi-input post-process pattern

Reads a sampled texture, averages a square neighborhood, writes the result. Practical for screen-space blur passes. Demonstrates: multi-input compute (texture + sampler + params), bounds-aware sampling, separating thread layout from image layout.

```cpp
auto BlurPipe = RHI.CreateComputePipeline({ .ShaderPath = "Shaders/Compute/BoxBlur.spv" });
auto Blurred  = RHI.CreateTexture({ .Width=1920, .Height=1080, .Fmt=Format::RGBA8_UNORM,
                                    .Usage=TextureUsage::Storage|TextureUsage::Sampled,
                                    .DebugName="Blurred" });

// Per frame: blur YourSourceTex into Blurred
Cmd->TransitionForSampling(YourSourceTex);     // ensure input is sampleable
Cmd->TransitionForStorageWrite(Blurred);

Cmd->Bind(BlurPipe);
struct PC {
    uint32_t InputSlot, OutputSlot, SamplerSlot;
    int32_t Radius;
    float Width, Height;
} pc{
    YourSourceTex.SampledSlot,
    Blurred.StorageSlot,
    /* kSamplerLinearClamp */ 0,
    2,   // 5x5 kernel
    1920.0f, 1080.0f,
};
Cmd->Push(pc);
Cmd->Dispatch2D(1920, 1080, 8, 8);

Cmd->TransitionForSampling(Blurred);
```

For a real Gaussian blur, run it twice — once with a horizontal-only kernel into an intermediate texture, once with a vertical-only kernel back into the original. Same shader pattern, just two passes and the kernel iterates a single axis at a time.

## Patterns and gotchas

### Thread group layout

`[numthreads(X, Y, Z)]` defines the **local** thread count per dispatched group. `Dispatch(gx, gy, gz)` defines how many groups. Total threads = `(X*gx, Y*gy, Z*gz)`.

For 2D image work, the common choice is `[numthreads(8, 8, 1)]` with `Dispatch2D(width, height, 8, 8)` — divides cleanly into pixel-sized work units, fits in a single NVIDIA warp (32 threads × 2) or AMD wave (64 threads).

For 1D buffer work, `[numthreads(64, 1, 1)]` with `Dispatch((N + 63) / 64, 1, 1)` is the standard.

### Bounds checks

`Dispatch2D` rounds up — for a 1920×1080 image at group size 8, you get 240×135 groups = 1920×1080 threads exactly. But if your image isn't a multiple of the group size (e.g. 1023×768 at 8×8 = 128×96 groups = 1024×768 threads), the extra threads must early-out:

```hlsl
if (TID.x >= W || TID.y >= H) return;
```

Forgetting this writes to out-of-bounds pixels (undefined behavior — usually silent, sometimes a validation error, occasionally a crash on certain drivers).

### Shared memory (group-shared)

`groupshared T name[N];` declares per-group memory. All threads in a group can read/write it after a barrier. Use for: reduction passes (sum/min/max of group results before the global reduce), pre-fetching neighbor pixels once for the whole group.

Sync with `GroupMemoryBarrierWithGroupSync()` after writes, before reads from other threads in the same group.

### Atomic ops

`InterlockedAdd`, `InterlockedMin`, etc. on RW buffers/images. Common use: counting (light list builders, histogram, particle alive count). Slang exposes the HLSL `Interlocked*` family verbatim.

### Indirect dispatch (Phase 13)

`vkCmdDispatchIndirect` — dispatch group counts come from a GPU buffer instead of CPU push constants. Required for GPU-driven culling, occlusion, mesh-shader scheduling. Helio doesn't expose this yet; lands in Phase 13 polish via `CommandList::DispatchIndirect(BufferHandle, uint64_t Offset)`.

### Layout transitions are MANDATORY between graphics and compute

When you write to a storage image in compute and then sample it in graphics (or vice versa), you MUST insert the transition. `TransitionForStorageWrite` puts the image in `GENERAL`, `TransitionForSampling` puts it back in `SHADER_READ_ONLY_OPTIMAL`. Skipping these gives validation errors like "image layout `GENERAL` is not compatible with `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`".

Phase 9's render graph wraps this automatically — declare `Reads`/`Writes` on a pass, the graph inserts barriers. Until then, transitions are manual.

## What's deferred to Phase 13

- **`GetRWStorageBuffer(slot)`** — dedicated read/write storage-buffer helper in Bindless.slang. Today you can write through the same slot if you declare the binding as `RWByteAddressBuffer` yourself.
- **Indirect dispatch.**
- **Sub-group / wave intrinsics** (`WaveActiveSum`, etc.) — Slang supports them, but the device doesn't currently advertise `subgroup` features. Two-line feature add when needed.
- **Tracy GPU zones around compute passes** — Phase 13 polish item, mentioned in [Profiling.md](Profiling.md).
