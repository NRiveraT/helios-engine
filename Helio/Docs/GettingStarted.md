# Getting Started

A 5-minute walkthrough. For environment setup (CMake, vcpkg, Vulkan SDK), see [Build.md](Build.md) first.

## V1 status

V1 ships the rendering plumbing. From `game/Source/main.cpp` you can today:

- Open a window via SDL3 + receive input events
- Bring up a Vulkan 1.3 device (bindless, RT-capable, validation-clean)
- Create buffers + textures with bindless slots
- Create + bind graphics / compute pipelines
- Open render passes (single target, MRT, fullscreen) and emit draws
- Sample textures across passes via bindless
- Build BLAS / TLAS and ray-query against them from any shader
- Log to console + file (`Saved/Logs/Game.log`)
- Profile with Tracy (CPU zones + GPU timeline)
- Compile shaders automatically (drop `.slang` anywhere under `Helio/Shaders/`)

You will NOT have (these are your job, post-V1):

- Render graph (auto-barriers + transient image aliasing) — **Phase 9**
- Input layer (action mapping + dispatch) — **Phase 10**
- On-screen overlay — **Phase 11**
- Debug-draw primitives (lines/spheres/boxes) — **Phase 12**
- Scene system / ECS — your design
- Material system — your design
- Mesh / texture file loaders beyond `meshoptimizer` link — your job
- Gameplay framework (`GameMode`, `Character`, abilities) — your design

## Quick "I want to see it" recipe — minimum working app

The smallest possible `main.cpp` that opens a Vulkan window and runs a frame loop:

```cpp
#include <Core/Logging/Log.h>
#include <Platform/Windows/Window.h>
#include <RHI/Public/Device.h>

int main() {
    helio::log::Init();

    helio::platform::windows::Window Win({
        .Title = "Helio", .Width = 1280, .Height = 720,
    });

    helio::rhi::Device RHI({
        .NativeWindow  = Win.Native(),
        .InitialWidth  = Win.Width(),
        .InitialHeight = Win.Height(),
        .EnableValidation = true,
        .EnableRayTracing = true,   // graceful fallback if GPU lacks RT
    });

    HELIO_LOG_INFO("Game", "RT supported: {}", RHI.HasRayTracing());

    while (Win.PumpEvents()) {
        if (auto* Cmd = RHI.BeginFrame()) {
            Cmd->BeginRenderingToSwapchain(0.05f, 0.1f, 0.2f, 1.0f);   // dark blue clear
            Cmd->EndRendering();
            RHI.EndFrame();
        }
    }

    RHI.WaitIdle();
    helio::log::Shutdown();
}
```

Build + run:

```powershell
cmake --build build/windows-msvc-debug --target Game --config Debug
./build/windows-msvc-debug/bin/Debug/Game.exe
```

You'll see a 1280×720 dark-blue window. Close button or ESC quits. Console + `Saved/Logs/Game.log` print device init + per-event lines.

That's the minimum proof the entire stack — SDL → Vulkan → swapchain → sync2 → present — is live. Everything else builds on this.

## Next steps — pick whichever interests you

| Want to | Read |
|---|---|
| **Draw a triangle from a shader you wrote** | [Shaders.md](Shaders.md) |
| **Sample a texture from a shader** | [Bindless.md](Bindless.md) |
| **Render to a GBuffer / MRT / your own texture** | [RenderTargets.md](RenderTargets.md) |
| **Run a compute pass (SSAO-style or ray marcher)** | [Compute.md](Compute.md) |
| **Trace rays against a scene** | [RT.md](RT.md) |
| **See per-frame CPU + GPU timings** | [Profiling.md](Profiling.md) |
| **Add a log category for your subsystem** | [Logging.md](Logging.md) |
| **Understand the whole RHI surface** | [RHI.md](RHI.md) |
| **Understand the module layout** | [Architecture.md](Architecture.md) *(Phase 13)* |

Each of those docs has its own "Quick 'I want to see it' recipe" section — runnable C++ + Slang you can copy into `main.cpp` and see the result on screen.

## What ships at `Helio/Shaders/` you can study

| Path | What it does |
|---|---|
| `Common/Bindless.slang` | The `GetTexture2D` / `GetStorageBuffer` / `GetTLAS` etc. helper module — everyone imports this |
| `Common/Fullscreen.slang` | Fullscreen-triangle VS helper — `import Fullscreen;` for any fragment-only pass |
| `Passes/Triangle.slang` | Vanilla colored triangle, no descriptors |
| `Passes/FullscreenBlit.slang` | Fullscreen sample of a bindless texture (CheckerTex demo) |
| `Compute/Gradient.slang` | Minimum compute — writes UV gradient to storage image |
| `Compute/RayMarch.slang` | Animated SDF ray marcher (sphere + checkered plane + sun + sky) |
| `Compute/BoxBlur.slang` | NxN box blur — image-in, image-out compute |
| `RT/RayQueryTest.slang` | Compute shader that ray-queries the bound TLAS, white-on-hit |

Reference any of these as a template when writing your own.
