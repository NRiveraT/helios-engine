# Architecture

How the modules layer, how a frame flows from `main()` to GPU, and the conventions every layer agrees on. Read this once when you start; refer back when something surprises you.

## Module layout

```
HelioObject (conceptual)        ← every reflectable / lifetime-managed type (future)
        │
Game project   ───────────►   game/Source/main.cpp
        │                       (the only consumer of Helio's public API)
        ▼
┌──────────────────────────────────────────────────────────┐
│  Helio.Resource    (Mesh, MeshSystem, InstanceBatch,     │
│                     procedural primitives, MeshPipeline) │
└─────────────────────────┬────────────────────────────────┘
                          │
        ┌─────────────────┴────────────┐
        ▼                              ▼
┌───────────────────┐         ┌───────────────────────────┐
│  Helio.Renderer   │         │  Helio.RHI                │
│  RenderGraph,     │ ──────► │  back-end-agnostic        │
│  Overlay,         │         │  Device / CommandList /   │
│  DebugDraw        │         │  Buffer / Texture /       │
│                   │         │  Pipeline / Accel struct  │
└─────────┬─────────┘         │  + RingUploadBuffer       │
          │                   └──────────┬────────────────┘
          │                              │
          ▼                              ▼
┌───────────────────┐         ┌───────────────────────────┐
│  Helio.Input      │         │  Helio.Platform.Windows   │
│  ActionMap,       │ ───────►│  SDL3 window, file watch  │
│  Dispatcher,      │         │  (Linux / Console: stub)  │
│  Key/MouseButton  │         └───────────────────────────┘
└─────────┬─────────┘
          │
          ▼
┌────────────────────────────────────────────────────────┐
│  Helio.Core   (Logging, Math, Transform, Time, Profile,│
│                Handles, Assert)                        │
└────────────────────────────────────────────────────────┘
```

Each `Source/<Module>/` builds an independent static lib (`Helio.Core.lib`, `Helio.RHI.lib`, etc.). Top-level `Helio` is an `INTERFACE` target aggregating them — game code only does `target_link_libraries(Game PRIVATE Helio)`.

**No upward dependencies.** Renderer can use RHI; RHI cannot use Renderer. Resource depends on RHI but not on Renderer (it doesn't know about RenderGraph — game code wires them together).

## Module responsibilities (one paragraph each)

### `Helio.Core`
The foundation. No Vulkan, no SDL, no rendering. Provides logging (spdlog wrapper with categories), math (`float3` / `float4` / `float4x4` aliases for hlslpp, plus packed GPU-layout types `Mat4Packed` / `Vec3Packed` / `Vec4Packed`), `Transform` (position + quaternion + scale, the canonical mutable transform), quaternion helpers, time (`Clock` with `Tick()` for delta time), tracy macros (`HELIO_PROFILE_ZONE`), handle types, assertion macros. Every other module depends on this.

### `Helio.Platform.Windows`
SDL3-backed window + file watcher. Owns the `SDL_Window*` and the `Dispatcher` it pumps events into. The only platform-specific module currently implemented; sibling `Platform/Linux/` and `Platform/Console/` are stubs.

### `Helio.RHI`
The Render Hardware Interface. Public headers under `RHI/Public/` are back-end-agnostic (`Device`, `CommandList`, `Buffer`, `Texture`, `Pipeline`, `AccelStructure`, `RingUploadBuffer`). Vulkan implementation under `RHI/Vulkan/` (`VulkanContext`, `VulkanCommandList`, etc.). Game code includes only `<RHI/Public/...>` — no Vulkan symbols leak. Owns the single bindless descriptor set, the per-frame command pools + sync, the swapchain, the VMA allocator, the deletion queue, and the PSO disk cache.

### `Helio.Input`
Pure-data input layer. `Key` / `MouseButton` / `KeyState` enums, `InputEvent` tagged union, `ActionMap` (string-keyed bindings), `Dispatcher` (raw + action-based handler subscription). Platform layers translate native input to `InputEvent` and call `Dispatcher::Dispatch`.

### `Helio.Renderer`
Built on top of `Helio.RHI`. Provides the declarative pass DAG (`RenderGraph`, `PassBuilder`), the on-screen overlay (`Overlay` with bitmap font + frame stats + frametime graph), and immediate-mode debug primitives (`DebugDraw` with `Line` / `Box` / `Sphere` / `Text2D` / `Text3D`). Three subsystems sharing the `RingUploadBuffer` pattern for race-free per-frame uploads.

### `Helio.Resource`
GPU-resident mesh storage + per-frame instance batches + the canonical static-mesh pipeline. `MeshSystem` owns vertex/index buffers and runs meshoptimizer over uploaded geometry. `Transform` (from Core) is the game-facing state; `InstanceBatch::Add(const Transform&)` is the bridge to GPU. Future home for glTF loading, materials, LODs, SDFs, meshlets.

## A single frame, end to end

Following the path of one frame in your game loop:

```cpp
core::Clock GameClock;
Transform CubePose;
CubePose.Position = float3(0, 0, -3);

while (Win.PumpEvents()) {                              // (1)
    const float Dt = float(GameClock.Tick());           // (2)

    CubePose.RotateAxis(float3(0, 1, 0), 1.5f * Dt);    // (3)

    Batch.Begin();                                       // (4)
    Batch.Add(CubePose);
    uint32_t Count = Batch.End();

    if (auto* Cmd = RHI.BeginFrame()) {                 // (5)
        RenderGraph rg(RHI, *Cmd);                       // (6)

        rg.Graphics("Meshes")
          .Color(Color, 0,0,0,1)
          .Depth(Depth, 0.0f)
          .Execute([&](CommandList& C) {                 // (7)
              C.Bind(MeshPipe);
              C.Push(PC);
              C.BindIndexBuffer(Cube.IndexBuffer, IndexTypeFor(Cube));
              C.DrawIndexed(Cube.IndexCount, Count);
          });

        Hud.DrawStats(CpuMs, RHI.LastFrameGpuMs(), rg.Passes());
        Hud.Render(rg, Color, W, H);
        rg.Present(Color);

        rg.Execute();                                    // (8)
        RHI.EndFrame();                                  // (9)
    }
}
```

1. **`Win.PumpEvents()`** — SDL pumps OS events. For each one, the platform layer constructs an `InputEvent` and calls `Win.Dispatcher().Dispatch(event)`. Game-registered handlers fire synchronously. Returns false when window close was requested.
2. **`GameClock.Tick()`** — seconds elapsed since the last frame's call. Drives any time-based update.
3. **`CubePose.RotateAxis(...)`** — pure CPU state mutation. `Transform` knows nothing about the GPU.
4. **`Batch.Begin/Add/End`** — `InstanceBatch::Add(Transform)` calls `Transform::ToMatrix()`, packs into a 64-byte `MeshInstance`, stages in a `std::vector`. `End()` uploads the staging array into the current frame's slot of a `RingUploadBuffer` (one buffer per frame-in-flight; rotation handles cross-frame race).
5. **`RHI.BeginFrame()`** — waits on this frame slot's `InFlight` fence, reads the previous occupant's GPU timestamps for `LastFrameGpuMs()`, acquires the next swapchain image, resets + begins the command buffer. Returns `nullptr` if the swapchain needed recreation (skip the frame).
6. **`RenderGraph rg(...)`** — empty DAG, ready to accept pass declarations.
7. **Inside `.Execute(...)`** — the render graph has automatically transitioned attachments into the right layouts, opened a dynamic-rendering scope with the declared color + depth attachments. Your callback records draw commands. The graph closes the scope after the callback returns.
8. **`rg.Execute()`** — walks pass declarations in order, inserts barriers between them, blits `Present`'s source into the swapchain image, transitions swapchain to `PRESENT_SRC`.
9. **`RHI.EndFrame()`** — writes the frame-end timestamp, ends + submits the command buffer, presents, advances `m_frameIndex`.

## Cross-cutting conventions

Things every module agrees on so they compose without surprises:

| Convention | Owner | Rationale |
|---|---|---|
| **Bindless descriptors** in one global set | RHI | Game code passes slot indices via push constants; no per-draw descriptor binding |
| **Column-vector math** (`mul(M, v_col)`) | Core | Standard textbook convention; matches HLSL's `mul()` |
| **Y-negated projection** + **`FrontFace::Clockwise`** on 3D pipelines | Core (math) + Resource (mesh pipeline) | World-up = screen-up, math-CCW geometry renders correctly with `Cull::Back` |
| **Reverse-Z depth** (`CompareOp::Greater`, clear to `0.0`) | Resource (mesh pipeline default) | Better depth precision at distance |
| **48-byte interleaved `Vertex`** | Resource + Shaders/Common | One bindless slot per mesh, glTF-compatible |
| **64-byte `MeshInstance`** (transform only in V1) | Resource | Aligns nicely, reserves space for future per-instance material data |
| **Frames-in-flight = 2** | RHI | Modest GPU/CPU pipelining; simplifies sync |
| **`RingUploadBuffer` for any per-frame host-upload** | RHI | One buffer per slot, no cross-frame race when CPU writes happen during GPU read |
| **`PascalCase` identifiers, `m_camelCase` members, acronyms stay UPPERCASE** | All | `SDLInitGuard`, `RHIDevice`, `TLASBuilder` |

## What lives where (rough lookup)

- Want to draw a mesh? → [`Meshes.md`](Meshes.md)
- Want to write a shader? → [`Shaders.md`](Shaders.md) (Slang language pointers) and [`Bindless.md`](Bindless.md) (how slots reach shaders)
- Want to add a new pass to the render graph? → [`RenderGraph.md`](RenderGraph.md)
- Want to do raytracing? → [`RT.md`](RT.md)
- Want to debug a frame? → [`Profiling.md`](Profiling.md) (Tracy + frame-stats overlay) and [`DebugDraw.md`](DebugDraw.md) (in-world primitives)
- Want to wire keys / mouse? → [`Input.md`](Input.md)
- Want to log? → [`Logging.md`](Logging.md)

## Deliberate non-features (V1)

The plan is "rendering plumbing", not "Unreal-in-a-box". The following layers don't exist yet and won't be added until you (the game developer) need them:

- **Scene graph / world / actor system** — discussion in [`Meshes.md`](Meshes.md) about how the planned OOP layer would map onto current primitives. No code today.
- **Material system** — `MeshInstanced.slang` is a verification path. Real materials are your job, built on the bindless texture path.
- **Asset pipeline** — no `.helmesh` cache format, no async streaming, no reference counting. Procedural primitives + (planned) glTF load are it.
- **Physics, animation, audio, networking** — folders reserved as stubs; no engine code.
- **Gameplay framework** — no `IGameMode`, no character base classes. `game/Source/main.cpp` calls Helio directly.
- **Editor** — V1's architecture supports a future editor (Helio is a static lib, the render graph can target arbitrary textures for viewport embedding, shaders hot-reload), but no editor code is written.

When you start filling these in, the engine's job is to stay out of the way: stable handles, lifetime-safe registries, no forced inheritance — so your game code shapes the abstraction, not the other way around.
