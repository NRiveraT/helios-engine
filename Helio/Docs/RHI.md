# RHI

The Render Hardware Interface — Helio's public abstraction over Vulkan. Game code includes only `<RHI/Public/*.h>`; no Vulkan symbols leak.

## Modules at a glance

| Header | Owns |
|---|---|
| `RHI/Public/Device.h` | Top-level entry. Lifecycle, frame begin/end, resource factories. Also: `FramesInFlight()`, `CurrentFrameIndex()`, `LastFrameGpuMs()`. |
| `RHI/Public/CommandList.h` | Frame-scoped recording surface. `BeginRendering`, `Bind`, `Push`, `Draw`, `Dispatch`, `BlitToSwapchain`, `TransitionForSampling`, `TransitionForStorageWrite`. |
| `RHI/Public/Buffer.h` | `BufferHandle`, `BufferDesc`, `BufferUsage` flags. |
| `RHI/Public/Texture.h` | `TextureHandle`, `TextureDesc`, `TextureUsage` flags. |
| `RHI/Public/Pipeline.h` | `PipelineHandle`, `GraphicsPipelineDesc`, `ComputePipelineDesc`, `CullMode`, `FrontFace`, `CompareOp`. |
| `RHI/Public/Formats.h` | `Format` enum (subset of common GPU formats). |
| `RHI/Public/AccelStructure.h` | `BLASHandle`, `TLASHandle`, build descriptors. |
| `RHI/Public/RingUploadBuffer.h` | Per-frame host-upload buffer (N slots, rotates with `CurrentFrameIndex`). Use for any data the CPU writes every frame while the GPU may still be reading the previous frame's contents. |

## Typical frame shape

```cpp
helio::rhi::Device RHI({ .NativeWindow = Win.Native(), ... });
auto MyPipe = RHI.CreateGraphicsPipeline({ ... });

while (Win.PumpEvents()) {
    if (auto* Cmd = RHI.BeginFrame()) {
        Cmd->BeginRenderingToSwapchain(0.05f, 0.05f, 0.1f, 1.0f);
        Cmd->Bind(MyPipe);
        Cmd->Push(myConstants);     // push-constants, ≤128 bytes
        Cmd->Draw(3);
        Cmd->EndRendering();
        RHI.EndFrame();
    }
}
RHI.WaitIdle();
```

`BeginFrame()` returns `nullptr` if the swapchain needed recreation (e.g. resize); skip that frame.

## What's wired right now

- **Vulkan 1.3** instance + device with dynamic rendering, sync2, descriptor indexing, buffer-device-address, timeline semaphore, `shaderDrawParameters` (`shaderDrawParameters` is required by Slang's default SPIR-V output).
- **Raytracing** extensions (acceleration-structure / ray-tracing-pipeline / ray-query / deferred-host-ops) enabled when the device supports them. Query at runtime via `Device::HasRayTracing()` and `Device::GetRayTracingProperties()`.
- **VMA allocator** with `BUFFER_DEVICE_ADDRESS` enabled (RT needs it).
- **Swapchain** at the window's initial size, MAILBOX present mode preferred (FIFO fallback), 2 frames in flight.
- **Per-frame command pool + buffer + binary acquire semaphore + InFlight fence**, plus one binary render-finished semaphore *per swapchain image* (Vulkan spec UID-vkQueueSubmit2-semaphore-03868).
- **Single global bindless descriptor set** at `set 0` — see [`Bindless.md`](Bindless.md).
- **Tracy GPU context** created in `VulkanContext` init; `TracyVkCollect` runs per frame so the GPU timeline appears in Tracy.exe alongside the CPU zones.
- **Whole-frame GPU timing** via `Device::LastFrameGpuMs()` — `vkCmdWriteTimestamp2` at top-of-pipe + bottom-of-pipe per frame, read back ~2 frames later when the slot's fence next signals.
- **`RingUploadBuffer`** — per-frame host-upload buffer that hands you a different slot on each frame-in-flight rotation. Eliminates the cross-frame race that single shared host-upload buffers have. The overlay, debug-draw, and mesh-instance paths all use it.
- **`FrontFace` pipeline state** — per-pipeline winding direction (`CounterClockwise` / `Clockwise`). 3D pipelines that use `PerspectiveReverseZLH` need `Clockwise` because the projection's Y negation flips signed-area sign.
- **Deletion queue** keyed to frame-in-flight slot; `Destroy*` calls defer the real `vkDestroy*` until the GPU has retired the slot.
- **PSO disk cache** at `PipelineCache.bin` next to `Game.exe`. First run is cold; subsequent runs reuse compiled PSOs.

## Where to go next

- **Sampling textures from shaders** → [`Bindless.md`](Bindless.md)
- **Render targets, MRT, GBuffer-style passes** → [`RenderTargets.md`](RenderTargets.md)
- **Writing shaders + recompile workflow** → [`Shaders.md`](Shaders.md)
- **Raytracing** → [`RT.md`](RT.md)
- **Render graph (declarative pass DAG)** → [`RenderGraph.md`](RenderGraph.md)
- **Drawing meshes (instanced static meshes, `Transform`, `InstanceBatch`)** → [`Meshes.md`](Meshes.md)
- **On-screen frame-stats overlay** → [`Overlay.md`](Overlay.md)
- **Debug primitives (Line / Box / Sphere / Text)** → [`DebugDraw.md`](DebugDraw.md)

## What's deliberately deferred

- **Render graph** — Phase 9. Pass declarations, automatic barrier insertion, transient image aliasing.
- **Per-pass GPU profiling macros** (`HELIO_GPU_ZONE(cmd, "Name")`) — Phase 13 polish. Frame-level GPU time is already visible in Tracy.
- **Shader hot reload** — Phase 13. The `FileWatcher` from `Platform/Windows/` is already built; the runtime hookup to `libslang` lands during polish.
- **DX12 backend** — namespace reserved at `RHI/D3D12/`, not implemented in V1.
