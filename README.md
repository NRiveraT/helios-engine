# Helio

Modern Vulkan 1.3 rendering engine with bindless descriptors, render-graph pass declarations, and raytracing — paired with a sibling game project at `game/`.

V1 ships **rendering plumbing only**. Scenes, materials, characters, abilities, gameplay logic — those live in `game/Source/` and are your responsibility.

## Layout

```
E/
├── Helio/         engine code (RHI + render graph + overlay + debug draw + input + platform)
├── game/          the game project that consumes Helio
└── vcpkg.json     dependency manifest
```

## Prerequisites

- Windows 10/11
- Visual Studio 2022 (Community or higher) with **Desktop development with C++** workload
- CMake 3.27+
- Vulkan SDK 1.4+ — <https://vulkan.lunarg.com/> — provides headers, validation layer, `slangc`
- vcpkg, cloned and bootstrapped — see [`Helio/Docs/Build.md`](Helio/Docs/Build.md)

## Quick start

```powershell
$env:VCPKG_ROOT = "C:/Users/nrive/vcpkg"
cmake --preset windows-msvc-debug
cmake --build build/windows-msvc-debug --target Game --config Debug
./build/windows-msvc-debug/bin/Debug/Game.exe
```

## Documentation

See [`Helio/Docs/`](Helio/Docs/):

**Getting set up**
- [GettingStarted.md](Helio/Docs/GettingStarted.md) — clone, build, your first triangle
- [Build.md](Helio/Docs/Build.md) — vcpkg, CMake presets, Rider + VSCode setup
- [Architecture.md](Helio/Docs/Architecture.md) — module layering, data flow, key conventions

**Core engine APIs**
- [RHI.md](Helio/Docs/RHI.md) — back-end-agnostic device, command list, resources
- [Bindless.md](Helio/Docs/Bindless.md) — single global descriptor set, slot allocation
- [RenderGraph.md](Helio/Docs/RenderGraph.md) — pass DAG, auto-barriers, transient resources
- [Shaders.md](Helio/Docs/Shaders.md) — Slang + bindless helpers, build pipeline
- [RT.md](Helio/Docs/RT.md) — BLAS/TLAS workflow, ray queries

**Rendering features**
- [Meshes.md](Helio/Docs/Meshes.md) — `Mesh`, `Transform`, `InstanceBatch`, instanced drawing
- [Compute.md](Helio/Docs/Compute.md) — compute pipelines, dispatch, storage images
- [RenderTargets.md](Helio/Docs/RenderTargets.md) — MRT, GBuffer-style passes, depth attachments
- [Overlay.md](Helio/Docs/Overlay.md) — on-screen frame-stats HUD, bitmap text, frametime graph
- [DebugDraw.md](Helio/Docs/DebugDraw.md) — `Line` / `Box` / `Sphere` / `Text2D` / `Text3D`

**System / dev**
- [Input.md](Helio/Docs/Input.md) — keyboard / mouse, `ActionMap`, `Dispatcher`
- [Profiling.md](Helio/Docs/Profiling.md) — Tracy, frame stats, GPU timestamps
- [Logging.md](Helio/Docs/Logging.md) — spdlog wrapper, log categories
