# Shaders

Helio uses **Slang** as its shader language and `slangc` (bundled with the Vulkan SDK) as the compiler. Every `.slang` source under `Helio/Shaders/` is compiled to a single `.spv` SPIR-V module that embeds every entry point tagged with `[shader("...")]`.

## Authoring a shader

Slang syntax is HLSL-flavored. Entry points are tagged with `[shader("vertex")]`, `[shader("fragment")]`, `[shader("compute")]`, etc., and the pipeline picks them by name at creation time (`VSMain`, `PSMain`, `CSMain`, …).

A minimal vertex/fragment pair lives in one file:

```hlsl
// Helio/Shaders/Passes/Triangle.slang
struct VSOut {
    float4 Position : SV_Position;
    float3 Color    : COLOR0;
};

[shader("vertex")]
VSOut VSMain(uint VertexID : SV_VertexID) {
    // ...
}

[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {
    return float4(In.Color, 1.0);
}
```

To read bindless resources, `import Bindless;` and use the `Get*()` helpers:

```hlsl
import Bindless;

struct PushConsts { uint TextureSlot; uint SamplerSlot; };
[[vk::push_constant]] PushConsts pc;

[shader("fragment")]
float4 PSMain(float2 UV : TEXCOORD0) : SV_Target {
    return GetTexture2D(pc.TextureSlot).Sample(GetSampler(pc.SamplerSlot), UV);
}
```

See [`Helio/Shaders/Common/Bindless.slang`](../Shaders/Common/Bindless.slang) for all helpers and the static sampler enum.

## Compiling — how to recompile shaders

Just rebuild Game. The CMake setup tracks every `.slang` as a dependency of its `.spv` output, so editing one and running:

```powershell
cmake --build build/windows-msvc-debug --target Game --config Debug
```

…re-runs `slangc` for the changed files and copies the fresh `.spv` next to `Game.exe`. Build chain:

1. `CompileShaders.cmake` declares each `.slang -> .spv` as a `add_custom_command(... DEPENDS .slang)`.
2. CMake target `Helio.Shaders` aggregates every `.spv` output (built as part of `ALL`).
3. `add_dependencies(Game Helio.Shaders)` in [`game/CMakeLists.txt`](../../game/CMakeLists.txt) makes Game depend on the shader target.
4. When a `.spv` updates, Game's `POST_BUILD` step copies `build/.../Helio/Shaders/` into `bin/Debug/Shaders/`.

### Tighter iteration loops

| Goal | Command |
|---|---|
| Recompile + deploy + relink everything | `cmake --build build/windows-msvc-debug --target Game --config Debug` |
| **Only** recompile shaders (no deploy) | `cmake --build build/windows-msvc-debug --target Helio.Shaders --config Debug` |
| Force a single shader to recompile | `touch Helio/Shaders/Passes/Triangle.slang` then build Game |
| Inspect the exact slangc command | Build with `cmake --build ... --verbose` and grep for `slangc` |

### Adding a new shader

The build auto-discovers every `.slang` under `Helio/Shaders/` (except `Common/`, which is import-only).

1. Drop the `.slang` somewhere under `Helio/Shaders/Passes/`, `Helio/Shaders/RT/`, or any new subdir of your choice (e.g. `Passes/MyPass.slang`).
2. Build: `cmake --build build/windows-msvc-debug --target Game --config Debug`. The build picks the new file up automatically (`CONFIGURE_DEPENDS` triggers a re-glob).
3. Reference it from C++ via its `.spv` (or `.slang`, which is silently rewritten):
   ```cpp
   auto Pipe = RHI.CreateGraphicsPipeline({
       .ShaderPath = "Shaders/Passes/MyPass.spv",
       .ColorAttachmentCount = 1,
       .DebugName = "MyPass",
   });
   ```

> **Why `Common/` is excluded.** Files there are intended for `import Foo;` from other shaders, not for standalone compilation — they typically have no `[shader("...")]` entry points, so `slangc` would either error or produce an empty `.spv`. Keep utility / helper modules in `Common/`; everything else compiles automatically.

If you ever need to opt back out of auto-discovery and pin an explicit list, `helio_compile_shaders` still accepts a `SOURCES` argument (see [`CompileShaders.cmake`](../Tools/Build/CompileShaders.cmake) for both modes).

### slangc command summary

The compile invocation (see [`Helio/Tools/Build/CompileShaders.cmake`](../Tools/Build/CompileShaders.cmake)) is:

```
slangc <source>.slang
    -target spirv
    -profile sm_6_6
    -capability spvRayTracingKHR
    -capability spvRayQueryKHR
    -fvk-use-entrypoint-name
    -emit-spirv-directly
    -I Helio/Shaders/Common
    -o <output>.spv
```

- `sm_6_6` enables modern HLSL features (templates, bindless via NonUniformResourceIndex).
- Both RT capabilities are unconditionally declared so any shader can `traceRayEXT`. They're harmless when unused.
- `-fvk-use-entrypoint-name` preserves the source entry-point name in the SPIR-V (so the pipeline can match `pName = "VSMain"`).
- `-emit-spirv-directly` skips Slang's intermediate text dump.
- `-I Helio/Shaders/Common` makes `import Bindless;` resolve.

### Common errors

- **`'XXX' file not found`** — the `import XXX;` doesn't match any file under the include dirs. Add an `-I` or check the spelling. Headers live under `Helio/Shaders/Common/`.
- **`SPIR-V Capability ... was declared, but ...`** — the GPU's Vulkan device wasn't created with the required feature. Add the feature to the chain in `VulkanContext::CreateLogicalDevice`. `shaderDrawParameters` and `descriptorIndexing` are already on; RT capabilities are conditional on `HasRayTracing()`.
- **`vkCreateShaderModule` validation error after editing** — your `.spv` is out of date. `cmake --build --target Game` to force the deploy step.
- **`"VSMain" entry point not found`** at pipeline creation — graphics pipelines REQUIRE a vertex stage. There's no "fragment-only" graphics pipeline in Vulkan / D3D / Metal. If your shader is fundamentally a fragment-only pass (fullscreen blit, lighting, post-process), pair PSMain with the helper VS from `Common/Fullscreen.slang`:

  ```hlsl
  import Fullscreen;

  [shader("vertex")]
  FullscreenVSOut VSMain(uint id : SV_VertexID) { return FullscreenVS(id); }
  ```

  Then call `Cmd->Draw(3)` (three vertices, no index buffer needed). If you genuinely have no rasterization need, use a compute pipeline + `Cmd->Dispatch(...)` instead.

## Gotchas

### Nothing renders but no validation errors fire — check your cull mode

If you wired a pipeline correctly (formats match, both stages compile, draw call submits, RenderDoc shows the draw) but the target stays at its clear color, the most likely culprit is **backface culling discarding your triangle** before rasterization.

Helio sets `VK_FRONT_FACE_COUNTER_CLOCKWISE` on every pipeline. Vulkan's framebuffer space has Y pointing **down** (`y = -1` is the top of the screen, `y = +1` is the bottom). This combination means:

| Triangle | Cull::None | Cull::Back | Cull::Front |
|---|---|---|---|
| `Common/Fullscreen.slang`'s fullscreen triangle | ✓ renders | ✗ culled (back-facing in Vulkan framebuffer) | ✓ renders |
| glTF / OBJ mesh imported as-is (CCW in math Y-up) | ✓ renders | ✗ culled | ✓ renders |
| Same mesh after `proj[1][1] *= -1` Y-flip | ✓ renders | ✓ renders | ✗ culled |

**Practical rules:**

- **Fullscreen passes** (post-process, blits, deferred lighting): use `CullMode::None`. There's no "wrong side" to a screen-filling triangle anyway.
- **Real 3D meshes**: pick ONE convention and apply it consistently. The most common Vulkan setup is `proj[1][1] *= -1` in the projection matrix + `CullMode::Back` everywhere. That makes Vulkan's Y-down match the math Y-up that most asset pipelines (glTF, OBJ, Blender) export from.
- **Don't mix conventions** — if some meshes use Y-flip projection and some don't, you'll have draws facing opposite directions and Cull::Back will appear to "randomly" eat geometry.

#### Why this happens (math)

The standard fullscreen-triangle trick from `SV_VertexID`:

```hlsl
float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
float4 Pos = float4(UV * 2.0 - 1.0, 0, 1);
```

…emits vertices at `(-1,-1)`, `(3,-1)`, `(-1,3)`. Trace v0→v1→v2 on a Vulkan framebuffer (Y down): top-left → top-right → bottom-left. That's a **clockwise** loop in framebuffer space. Vulkan's `VK_FRONT_FACE_COUNTER_CLOCKWISE` says CW = back-facing → `Cull::Back` drops it.

Real meshes exported from CCW-convention tools (most of them) hit the same issue after the standard model→view→proj chain without a Y-flip, because Vulkan's Y-down inversion mirrors the winding.

## Quick "I want to see it" recipe

End-to-end: write a new shader, build, see it on screen.

1. **Create the shader.** Save as `Helio/Shaders/Passes/MyTri.slang`:

   ```hlsl
   struct VSOut {
       float4 Position : SV_Position;
       float3 Color    : COLOR0;
   };

   [shader("vertex")]
   VSOut VSMain(uint VertexID : SV_VertexID) {
       const float2 Pos[3] = { float2(0, -0.6), float2(-0.6, 0.6), float2(0.6, 0.6) };
       const float3 Col[3] = { float3(1,1,0), float3(0,1,1), float3(1,0,1) };
       VSOut Out;
       Out.Position = float4(Pos[VertexID], 0, 1);
       Out.Color    = Col[VertexID];
       return Out;
   }

   [shader("fragment")]
   float4 PSMain(VSOut In) : SV_Target {
       return float4(In.Color, 1);
   }
   ```

2. **Build.** Auto-discovery picks it up:

   ```powershell
   cmake --build build/windows-msvc-debug --target Game --config Debug
   ```

   You'll see in the build output:

   ```
   slangc Shaders/Passes/MyTri.slang -> Shaders/Passes/MyTri.spv
   ```

3. **Reference from C++.** In your `main.cpp`:

   ```cpp
   auto MyTriPipe = RHI.CreateGraphicsPipeline({
       .ShaderPath = "Shaders/Passes/MyTri.spv",
       .ColorFormats = { helio::rhi::Format::BGRA8_SRGB },
       .ColorAttachmentCount = 1,
       .DebugName = "MyTri",
   });

   // In your frame loop:
   Cmd->BeginRenderingToSwapchain(0, 0, 0, 1);
   Cmd->Bind(MyTriPipe);
   Cmd->Draw(3);
   Cmd->EndRendering();
   ```

4. **Run.** You'll see a yellow/cyan/magenta triangle on a black background.

To iterate on the shader, edit the `.slang`, save, then `cmake --build ... --target Game` again — slangc reruns, the new `.spv` deploys next to `Game.exe`, and your next launch picks up the change. (Phase 13 polish will add live in-process hot reload via `libslang`.)

## Hot reload

Not in V1. Phase 13 wires `libslang` + the `FileWatcher` from `Platform/Windows/` so saving a `.slang` rebuilds the affected pipelines in-process on the next frame.
