# Render Targets & MRT

How to create your own textures, render to them (one or many at once), and sample them in a later pass — the foundation for GBuffer / deferred rendering, post-process chains, shadow maps, etc.

## 1. Creating a render-target texture

A texture intended as a render target needs two things in its `Usage`:

- `ColorAttachment` (or `DepthStencilAttachment`) — lets the GPU write to it during `BeginRendering`
- `Sampled` — lets you read it in a later pass via its bindless slot (omit if you only need it as an intermediate write target)

```cpp
using namespace helio::rhi;

auto Albedo = RHI.CreateTexture({
    .Width  = 1920,
    .Height = 1080,
    .Fmt    = Format::RGBA8_UNORM,
    .Usage  = TextureUsage::ColorAttachment | TextureUsage::Sampled,
    .DebugName = "GBuffer.Albedo",
});

auto Depth = RHI.CreateTexture({
    .Width  = 1920,
    .Height = 1080,
    .Fmt    = Format::D32_SFLOAT,
    .Usage  = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled,
    .DebugName = "GBuffer.Depth",
});
```

On the returned `TextureHandle`:
- `SampledSlot` is the bindless index you push to shaders that need to read it.
- `StorageSlot` is non-`UINT32_MAX` only if you also set `TextureUsage::Storage` (for compute writes).
- A render target's color/depth view is internal — no slot for that; you reference it by passing the whole `TextureHandle` to `BeginRendering`.

### Format suggestions

| GBuffer slot | Format | Why |
|---|---|---|
| Albedo / BaseColor | `RGBA8_SRGB` | sRGB conversion free; matches most asset textures |
| Normal (encoded, e.g. octahedral 2-channel) | `RG16F` | enough precision for unit vectors; half storage |
| Material (metallic, roughness, AO, ID) | `RGBA8_UNORM` | 4 channels of 8-bit packed material params |
| Motion vector | `RG16F` | sub-pixel velocity; half precision is fine |
| Emissive / HDR color | `RGBA16F` | HDR range, no banding |
| Depth | `D32_SFLOAT` | reverse-Z friendly, no stencil needed |
| Depth + stencil | `D24_UNORM_S8_UINT` | when you actually use stencil |

### Sizing

V1 doesn't automatically resize your render targets when the swapchain resizes. Two patterns:

1. **Match swapchain** — recreate on `OnResize` (your future input handler). For now create them at startup at the window size.
2. **Independent (downsample/upsample)** — fixed sizes (e.g. SSAO half-res), do not need rebuild.

## 2. Rendering to your own texture (single target)

```cpp
if (auto* Cmd = RHI.BeginFrame()) {
    ColorAttachment Color{
        .Target = Albedo,
        .Load   = LoadOp::Clear,
        .ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    Cmd->BeginRendering(&Color, 1);
    Cmd->Bind(MyPipe);
    Cmd->Draw(3);
    Cmd->EndRendering();

    // ... later: blit/sample Albedo into the swapchain
    RHI.EndFrame();
}
```

The pipeline `MyPipe` must declare `ColorAttachmentCount = 1` and `ColorFormats[0] = Format::RGBA8_UNORM` to match the target — otherwise validation will fire.

## 3. MRT: GBuffer-style rendering

The deferred-rendering classic — one pass writes albedo, normal, material, and depth simultaneously.

### Pipeline setup

```cpp
auto GBufferPipe = RHI.CreateGraphicsPipeline({
    .ShaderPath = "Shaders/Passes/GBuffer.spv",
    .ColorFormats = {
        Format::RGBA8_SRGB,   // 0: albedo
        Format::RG16F,        // 1: normal
        Format::RGBA8_UNORM,  // 2: material
    },
    .ColorAttachmentCount = 3,
    .DepthFormat          = Format::D32_SFLOAT,
    .Cull                 = CullMode::Back,
    .DepthTest            = true,
    .DepthWrite           = true,
    .DepthCompare         = CompareOp::Greater,   // reverse-Z
    .DebugName            = "GBuffer",
});
```

### Recording the pass

```cpp
ColorAttachment GBufferColors[] = {
    { .Target = Albedo,   .ClearColor = {0,0,0,1} },
    { .Target = Normal,   .ClearColor = {0,0,0,0} },
    { .Target = Material, .ClearColor = {0,0,0,0} },
};
DepthAttachment GBufferDepth{
    .Target     = Depth,
    .ClearDepth = 0.0f,    // reverse-Z: clear to far
};

Cmd->BeginRendering(GBufferColors, 3, &GBufferDepth);
Cmd->Bind(GBufferPipe);
// per-draw: push your bindless material/transform indices, then Draw
Cmd->Push(MyDrawConstants);
Cmd->Draw(IndexCount);
Cmd->EndRendering();
```

There's also an `initializer_list` shorthand for inline lists:

```cpp
Cmd->BeginRendering({
    { Albedo,   LoadOp::Clear, {0,0,0,1} },
    { Normal,   LoadOp::Clear, {0,0,0,0} },
    { Material, LoadOp::Clear, {0,0,0,0} },
}, &GBufferDepth);
```

Rules:
- Up to 8 color attachments.
- All color attachments must share the same width and height. (Depth doesn't have to match if you really want, but please don't.)
- Each target must have been created with the matching `*Attachment` usage flag.
- Targets are auto-transitioned into the right layout — no manual barrier needed.

### Shader side

`GBuffer.slang` writes to all three color outputs:

```hlsl
struct PSOut {
    float4 Albedo   : SV_Target0;
    float2 NormalXY : SV_Target1;   // RG16F target
    float4 Material : SV_Target2;
};

[shader("fragment")]
PSOut PSMain(VSOut In) {
    PSOut Out;
    Out.Albedo   = float4(SampleAlbedo(In.UV), 1);
    Out.NormalXY = EncodeNormal(In.WorldNormal);
    Out.Material = float4(In.Metallic, In.Roughness, In.AO, 0);
    return Out;
}
```

## 4. Sampling a render target in a later pass

After `EndRendering()`, each target is sitting in `COLOR_ATTACHMENT_OPTIMAL` (or `DEPTH_ATTACHMENT_OPTIMAL`). To sample it from a shader you need a layout + memory barrier to `SHADER_READ_ONLY_OPTIMAL`. Helio gives you one call per texture:

```cpp
// GBuffer pass already ended.
Cmd->TransitionForSampling(Albedo);
Cmd->TransitionForSampling(Normal);
Cmd->TransitionForSampling(Material);
Cmd->TransitionForSampling(Depth);

// Lighting pass reads them via bindless slots.
Cmd->BeginRenderingToSwapchain(0, 0, 0, 1);
Cmd->Bind(LightingPipe);

struct LightingPC {
    uint AlbedoSlot;
    uint NormalSlot;
    uint MaterialSlot;
    uint DepthSlot;
    uint SamplerSlot;
};
LightingPC PC{
    Albedo.SampledSlot, Normal.SampledSlot,
    Material.SampledSlot, Depth.SampledSlot,
    /* kSamplerPointClamp */ 2,
};
Cmd->Push(PC);
Cmd->Draw(3);   // fullscreen triangle
Cmd->EndRendering();
```

The lighting shader reads with the usual bindless helpers:

```hlsl
import Bindless;

struct PC { uint AlbedoSlot, NormalSlot, MaterialSlot, DepthSlot, SamplerSlot; };
[[vk::push_constant]] PC pc;

float4 Lighting(float2 UV) {
    float4 Albedo   = GetTexture2D(pc.AlbedoSlot  ).Sample(GetSampler(pc.SamplerSlot), UV);
    float2 NormalXY = GetTexture2D(pc.NormalSlot  ).Sample(GetSampler(pc.SamplerSlot), UV).rg;
    float4 Material = GetTexture2D(pc.MaterialSlot).Sample(GetSampler(pc.SamplerSlot), UV);
    float  Depth    = GetTexture2D(pc.DepthSlot   ).Sample(GetSampler(pc.SamplerSlot), UV).r;
    // ... lighting math
}
```

> **Round-tripping back to attachment** — if you want to write to the GBuffer again on the next frame, `BeginRendering` will auto-transition it back from `SHADER_READ_ONLY_OPTIMAL` to `COLOR_ATTACHMENT_OPTIMAL`. No manual work needed.

## 5. Common pitfalls

- **Pipeline format mismatch.** `GraphicsPipelineDesc::ColorFormats[]` must exactly match the formats of the textures passed to `BeginRendering`. Same count, same per-slot format. Validation will tell you precisely which slot mismatched.
- **Sampling without `TransitionForSampling`.** You'll see a validation error like *"layout COLOR_ATTACHMENT_OPTIMAL is not compatible with VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE"*. Insert the transition call between the writer and the reader.
- **Forgetting `EndRendering` before another `BeginRendering`.** `BeginRendering` asserts `!InRendering`. Always close the previous scope first.
- **Mismatched attachment sizes.** All color attachments in one `BeginRendering` must share dimensions. If you need a different size for a subsequent pass, end this pass and open another with the new targets.
- **Depth without `DepthFormat` on the pipeline.** If you pass a depth attachment to `BeginRendering` but the pipeline's `DepthFormat = Format::Undefined`, the depth test won't fire. Set both.
- **Not destroying render targets on resize.** `RHI.DestroyTexture(handle)` queues the texture for deferred destruction. Recreate at the new size, save the new handles, push them through your pipelines.
- **`Cull::Back` on a fullscreen-triangle pipeline silently eats your draw.** The `Common/Fullscreen.slang` helper emits a CW triangle in Vulkan's Y-down framebuffer space, which Helio's default `VK_FRONT_FACE_COUNTER_CLOCKWISE` treats as back-facing. Use `Cull::None` on any pipeline driven by `FullscreenVS` — same goes for blits, lighting passes, post-process, and tonemapping. Real 3D mesh pipelines are where `Cull::Back` actually pulls its weight. See [Shaders.md → Gotchas](Shaders.md#gotchas) for the full explanation + the Y-flip projection workaround that lets `Cull::Back` apply uniformly across both fullscreen and mesh passes.
- **Sequential `BeginRenderingToSwapchain` calls clear each other.** Every `BeginRenderingToSwapchain(R,G,B,A)` opens its rendering scope with `LoadOp::Clear`, wiping whatever the previous pass wrote. If you want a later pass to compose over an earlier one's output, you need `LoadOp::Load` — for V1 the only way to express that on the swapchain is to do all your work inside one `BeginRenderingToSwapchain → EndRendering` scope. The render graph (Phase 9) handles this automatically.

## 6. What V1 doesn't have yet

- **Render graph (Phase 9)** — automatic transitions + transient image aliasing + topological pass scheduling. The primitives above are the foundation it'll wrap.
- **Mip generation** — V1's `CreateTexture` allocates the mip count you ask for but doesn't auto-generate from mip 0. Manual `vkCmdBlitImage` chains land when Phase 9 needs them.
- **MSAA color attachments** — single-sample only. The plumbing for `SAMPLE_COUNT_*` and resolve attachments comes with the render graph.
- **Image arrays / cubemaps as render targets** — single 2D slice only via `BeginRendering`. (You can create them; just can't bind one face/slice today.)
