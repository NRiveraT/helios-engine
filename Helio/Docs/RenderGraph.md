# Render Graph

A declarative pass DAG that wraps Helio's CommandList primitives. You declare textures + passes; the graph allocates transient images, inserts layout transitions, opens dynamic-rendering scopes for graphics passes, and blits the final output to the swapchain.

V1 ships **declaration-order execution with auto-barriers + transient texture allocation**. That's enough to replace all the manual `TransitionForSampling` / `TransitionForStorageWrite` / `BeginRendering` / `EndRendering` bookkeeping from `main.cpp` with one fluent chain. Topological sorting, dead-pass elimination, and true memory aliasing across non-overlapping lifetimes are Phase 13 polish — declaration order is enough for V1's needs.

## Why

Without the graph, your frame loop looks like:

```cpp
auto Albedo  = RHI.CreateTexture({ /* ColorAttachment | Sampled, persistent */ });
auto Normal  = RHI.CreateTexture({ /* ... */ });

while (loop) {
    if (auto* Cmd = RHI.BeginFrame()) {
        // GBuffer pass — manual attachment array setup
        ColorAttachment GBufferColors[] = {
            { .Target = Albedo, .Load = LoadOp::Clear, .ClearColor = {0,0,0,1} },
            { .Target = Normal, .Load = LoadOp::Clear, .ClearColor = {0,0,0,0} },
        };
        Cmd->BeginRendering(GBufferColors, 2);
        Cmd->Bind(GBufferPipe);
        Cmd->Draw(3);
        Cmd->EndRendering();

        // Manual transitions
        Cmd->TransitionForSampling(Albedo);
        Cmd->TransitionForSampling(Normal);

        // Lighting pass — open another scope
        Cmd->BeginRenderingToSwapchain(0, 0, 0, 1);
        Cmd->Bind(LightingPipe);
        Cmd->Push(LightingPC{Albedo.SampledSlot, Normal.SampledSlot});
        Cmd->Draw(3);
        Cmd->EndRendering();

        RHI.EndFrame();
    }
}
```

With the graph:

```cpp
while (loop) {
    if (auto* Cmd = RHI.BeginFrame()) {
        helio::render::RenderGraph Rg(RHI, *Cmd);

        auto Albedo = Rg.Image("Albedo", 1920, 1080, Format::RGBA8_SRGB);
        auto Normal = Rg.Image("Normal", 1920, 1080, Format::RGBA16F);
        auto Lit    = Rg.Image("Lit",    1920, 1080, Format::BGRA8_SRGB);

        Rg.Graphics("GBuffer")
          .Color(Albedo).Color(Normal)
          .Execute([&](CommandList& C) {
              C.Bind(GBufferPipe);
              C.Draw(3);
          });

        Rg.Graphics("Lighting")
          .Color(Lit)
          .Read(Albedo).Read(Normal)
          .Execute([&](CommandList& C) {
              C.Bind(LightingPipe);
              C.Push(LightingPC{Albedo.SampledSlot, Normal.SampledSlot});
              C.Draw(3);
          });

        Rg.Present(Lit);
        // Rg destructor calls Execute() — runs the passes, deletes transients.

        RHI.EndFrame();
    }
}
```

The graph:
- Allocates `Albedo` / `Normal` / `Lit` as transient textures (auto-destroyed when `Rg` goes out of scope)
- Inserts `TransitionForSampling(Albedo)` + `TransitionForSampling(Normal)` between GBuffer and Lighting (you didn't have to)
- Opens / closes `BeginRendering` / `EndRendering` for each graphics pass
- Blits `Lit` to the swapchain at the end via `Cmd->BlitToSwapchain`

## Texture lifetimes — persistent vs transient

You have two ways to get a `TextureHandle` into the graph. Pick deliberately:

### Persistent (recommended for production)

Create the texture once via `RHI.CreateTexture(...)` outside the frame loop. Pass the handle to `.Color(...)` / `.Read(...)` / `.Write(...)` directly — the graph doesn't allocate or destroy it, just tracks the access for barrier insertion.

```cpp
// Once at startup:
auto Albedo = RHI.CreateTexture({
    .Width = 1920, .Height = 1080,
    .Fmt   = Format::RGBA8_SRGB,
    .Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled,
    .DebugName = "Albedo",
});

// Every frame:
while (running) {
    if (auto* Cmd = RHI.BeginFrame()) {
        RenderGraph rg(RHI, *Cmd);
        rg.Graphics("GBuffer").Color(Albedo).Execute([&](CommandList& C){ /* draw */ });
        rg.Present(Albedo);
        rg.Execute();
        RHI.EndFrame();
    }
}
```

This is the right pattern when the texture's descriptor doesn't change frame-to-frame. No per-frame allocation, no churn in the deletion queue, the bindless slot stays stable so shaders can keep pushing the same slot index. **G-buffer attachments, persistent post-process intermediates, swapchain-sized work targets — all persistent.**

> When the swapchain resizes, you'll want to destroy + recreate persistent textures sized to the window. Today that's manual — handle it in your resize callback once Phase 10 input lands. Phase 13 polish adds an `OnResize` hook.

### Transient (per-frame scratch)

`rg.Image(...)` creates the texture and queues its destruction for graph teardown. Use when:

- The descriptor genuinely changes frame-to-frame (variable size based on visible geometry, etc.)
- The work is one-shot and you don't want the texture to live longer than the graph
- You're prototyping and don't care about allocation overhead

```cpp
RenderGraph rg(RHI, *Cmd);
auto Scratch = rg.Image("Scratch", W, H, Format::RGBA16F);  // CreateTexture + queued destroy
rg.Compute(...).Write(Scratch).Execute(...);
rg.Present(Scratch);
rg.Execute();
```

Today each `rg.Image()` is a real `RHI.CreateTexture` + `RHI.DestroyTexture` per frame. The Phase 13 transient pool will recycle same-desc textures across frames, but the pattern stays identical from the user side.

## API

```cpp
#include <Renderer/RenderGraph.h>
using namespace helio::render;

RenderGraph Rg(Device, CommandList);

// Per-frame scratch — graph allocates + destroys.
TextureHandle T = Rg.Image("Name", W, H, Format::RGBA8_SRGB,
                           /*Usage =*/ TextureUsage::Sampled | TextureUsage::ColorAttachment);

// (For persistent textures, skip Rg.Image() entirely — just pass your
//  pre-created TextureHandle directly to the pass methods below.)

// Declare a pass — fluent chain.
Rg.Graphics("PassName")
  .Color(T)                        // attachment, clears to (0,0,0,1)
  .Color(T2, 1, 0, 0, 1)            // attachment with custom clear color
  .ColorLoad(T3)                    // attachment with LoadOp::Load (preserves prior content)
  .Depth(DepthT, 0.0f)              // depth attachment with reverse-Z clear
  .Read(SomeTex)                    // shader read → transitions to SHADER_READ_ONLY
  .Execute([&](CommandList& C) {
      C.Bind(pipe);
      C.Draw(3);
  });

// Inspect the graph (for stats overlays etc.)
uint32_t Count = Rg.Passes();       // declared-pass count so far

Rg.Compute("ComputeName")
  .Read(InputTex)
  .Write(OutputTex)                 // compute storage write → transitions to GENERAL
  .Execute([&](CommandList& C) {
      C.Bind(computePipe);
      C.Dispatch2D(W, H, 8, 8);
  });

Rg.Present(SomeTex);                // blit to swapchain at Execute()
Rg.Execute();                       // REQUIRED — call before RHI.EndFrame()
```

## ⚠️ `Execute()` is required before `RHI.EndFrame()`

The graph records commands when `Execute()` runs. `RHI.EndFrame()` calls `vkEndCommandBuffer()` and submits — once that fires, recording is closed. So:

```cpp
if (auto* Cmd = RHI.BeginFrame()) {
    RenderGraph rg(RHI, *Cmd);
    // ... declarations ...
    rg.Present(...);
    rg.Execute();           // ← MUST happen before EndFrame
    RHI.EndFrame();
}
```

If you forget `Execute()`, the destructor catches it: `HELIO_CHECK` fires with a clear message naming how many passes you declared but never recorded. Alternative phrasing — scope `rg` so its destructor fires before `EndFrame`:

```cpp
if (auto* Cmd = RHI.BeginFrame()) {
    {
        RenderGraph rg(RHI, *Cmd);
        // declarations...
        rg.Present(...);
    }                       // ← rg dtor here would also assert if Execute() wasn't called
    RHI.EndFrame();
}
```

The explicit `rg.Execute()` call is clearer than relying on scoping, especially when adding logging between the graph and EndFrame. Prefer the explicit form.

## Pass kinds + access types

| Pass kind | Access | What the graph does |
|---|---|---|
| `Graphics` | `Color(h)` | Adds `h` to the BeginRendering color array; clears with the given color |
| `Graphics` | `Color(h, r, g, b, a)` | Same with custom clear |
| `Graphics` | `ColorLoad(h)` | Same but `LoadOp::Load` — preserves prior contents (used by overlay + debug-draw passes over a scene) |
| `Graphics` | `Depth(h, d)` | Adds `h` as the depth attachment; clears depth to `d` (use `0.0f` for reverse-Z) |
| `Graphics` | `Read(h)` | Calls `TransitionForSampling(h)` before `BeginRendering` |
| `Compute` | `Read(h)` | Calls `TransitionForSampling(h)` before the user callback |
| `Compute` | `Write(h)` | Calls `TransitionForStorageWrite(h)` before the user callback |

`Color`/`ColorLoad`/`Depth` only apply to graphics passes. `Read`/`Write` work in both — `Read` is shader-sample (sampled image) and `Write` is shader-write (storage image).

The `RenderGraph` itself also exposes `Passes()` (declared-pass count, useful for frame-stats overlays).

## Quick "I want to see it" recipe

Minimum graph: persistent texture, compute writes a gradient into it, graph blits to the swapchain. **The texture is created ONCE at startup, not per frame** — this is the production-recommended pattern.

```cpp
#include <Core/Logging/Log.h>
#include <Platform/Windows/Window.h>
#include <RHI/Public/Device.h>
#include <Renderer/RenderGraph.h>

int main() {
    helio::log::Init();

    helio::platform::windows::Window Win({ .Title = "RG Demo", .Width = 1280, .Height = 720 });
    helio::rhi::Device RHI({
        .NativeWindow = Win.Native(),
        .InitialWidth = Win.Width(), .InitialHeight = Win.Height(),
    });

    auto ComputePipe = RHI.CreateComputePipeline({
        .ShaderPath = "Shaders/Compute/Gradient.spv",
    });

    // PERSISTENT output texture — created once, written every frame.
    auto Out = RHI.CreateTexture({
        .Width = Win.Width(), .Height = Win.Height(),
        .Fmt   = helio::rhi::Format::RGBA8_UNORM,
        .Usage = helio::rhi::TextureUsage::Storage | helio::rhi::TextureUsage::Sampled,
        .DebugName = "GradientOut",
    });

    while (Win.PumpEvents()) {
        if (auto* Cmd = RHI.BeginFrame()) {
            helio::render::RenderGraph Rg(RHI, *Cmd);

            Rg.Compute("Gradient")
              .Write(Out)
              .Execute([&](helio::rhi::CommandList& C) {
                  C.Bind(ComputePipe);
                  struct PC { uint32_t Slot; } pc{ Out.StorageSlot };
                  C.Push(pc);
                  C.Dispatch2D(Win.Width(), Win.Height(), 8, 8);
              });

            // Don't even need an explicit blit pass — Rg.Present blits Out to swapchain.
            Rg.Present(Out);
            Rg.Execute();          // record commands before EndFrame closes the buffer
            RHI.EndFrame();
        }
    }

    RHI.WaitIdle();
    helio::log::Shutdown();
}
```

Run it. You'll see a UV gradient on screen. The graph:
1. Created a transient `RGBA8_UNORM` texture sized to the window
2. Inserted `TransitionForStorageWrite(Out)` before the compute callback
3. Ran your compute shader
4. Called `BlitToSwapchain(Out)` at the end (which transitions Out to `TRANSFER_SRC` and the swapchain image to `TRANSFER_DST` internally)
5. On `Rg`'s destruction, queued `DestroyTexture(Out)` to run when the GPU finishes with frame `N-2`

## Common patterns

### Three-target GBuffer + lighting pass

```cpp
auto Albedo = Rg.Image("Albedo", W, H, Format::RGBA8_SRGB);
auto Normal = Rg.Image("Normal", W, H, Format::RGBA16F);
auto MRA    = Rg.Image("MRA",    W, H, Format::RGBA8_UNORM);
auto Depth  = Rg.Image("Depth",  W, H, Format::D32_SFLOAT,
                       TextureUsage::DepthStencilAttachment | TextureUsage::Sampled);
auto Lit    = Rg.Image("Lit",    W, H, Format::RGBA16F);

Rg.Graphics("GBuffer")
  .Color(Albedo).Color(Normal).Color(MRA)
  .Depth(Depth, 0.0f)
  .Execute([&](CommandList& C) {
      C.Bind(GBufferPipe);
      for (auto& Draw : MeshDraws) {
          C.Push(Draw.PC);
          C.BindIndexBuffer(Draw.Idx, IndexType::U32);
          C.DrawIndexed(Draw.IdxCount);
      }
  });

Rg.Graphics("Lighting")
  .Color(Lit)
  .Read(Albedo).Read(Normal).Read(MRA).Read(Depth)
  .Execute([&](CommandList& C) {
      C.Bind(LightingPipe);
      LightingPC PC{ Albedo.SampledSlot, Normal.SampledSlot, MRA.SampledSlot,
                     Depth.SampledSlot, /* SamplerSlot */ 2 };
      C.Push(PC);
      C.Draw(3);
  });

Rg.Present(Lit);
```

### Compute + graphics chain

```cpp
auto Color = Rg.Image("Color", W, H, Format::RGBA16F,
                      TextureUsage::ColorAttachment | TextureUsage::Sampled);
auto SSAO  = Rg.Image("SSAO",  W, H, Format::R8_UNORM,
                      TextureUsage::Storage | TextureUsage::Sampled);

Rg.Graphics("ForwardOpaque").Color(Color).Execute([&](CommandList& C) { /* ... */ });

Rg.Compute("SSAO")
  .Read(SomeDepthInput).Write(SSAO)
  .Execute([&](CommandList& C) {
      C.Bind(SsaoPipe);
      C.Push(SsaoPC{...});
      C.Dispatch2D(W, H, 8, 8);
  });

Rg.Graphics("Compose")
  .Color(Color)             // load existing color
  .Read(SSAO)
  .Execute([&](CommandList& C) { /* modulate color by SSAO */ });

Rg.Present(Color);
```

## Gotchas

- **Declaration order matters in V1.** No topo sort. If pass B reads what pass A writes, declare B AFTER A. (Phase 13 will sort by dependency.)
- **Forgot `Read(handle)` on a pass that samples it?** The graph won't transition; the sample will fire validation about a wrong layout. Always declare your reads explicitly even if it feels redundant — the declaration IS what tells the graph to insert the barrier.
- **`Present` accepts only one source.** It blits that texture to the swapchain. If you want a composite displayed, render it into a single texture first (e.g. into your `Lit` target above) and present that.
- **Transients allocate real textures every frame.** The graph calls `Device::CreateTexture` for each `Image()` and `Device::DestroyTexture` when it destructs. Allocation is reasonably cheap (VMA) and deletion goes through the deferred queue (no GPU stalls), but allocating 30 transient textures per frame is real overhead. Phase 13 adds a transient pool that recycles same-desc textures across frames.
- **No multi-queue.** Graphics + compute submit on the same queue in V1; the graph doesn't schedule across queues. Async compute lands with the Phase 13 polish.

## What's deferred to Phase 13

- **Topological sort + dead-pass elimination** — passes whose outputs aren't consumed by `Present` (directly or transitively) currently still execute. Phase 13 cuts them.
- **Transient memory aliasing** — non-overlapping lifetimes share the same underlying allocation. Today every transient is a separate allocation.
- **Cross-frame texture pool** — keep transient textures alive across frames so repeated graphs with the same image descriptors don't recreate them. Today they're created and destroyed per-frame.
- **Async compute / multi-queue.**
- **Per-pass GPU profiling zones** via `HELIO_GPU_ZONE` — Phase 13 builds this on top of the graph's pass boundaries automatically.
