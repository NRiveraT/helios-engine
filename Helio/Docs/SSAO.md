# SSAO — Screen-Space Ambient Occlusion (Depth Pre-Pass Route)

A step-by-step walkthrough for adding SSAO to Helio's hybrid-forward renderer. This is a **guide you implement yourself** — every phase names the exact file, the exact hook point, the math you must get right, and the convention traps that will bite. It does not drop a finished feature into the tree; it walks you through building one.

The math here is written for Helio's actual conventions: **LH +Y-up +Z-forward, column-vector `mul(M, v)`, reverse-Z (near = 1, far = 0), Y baked-negative in the projection.** Get the signs wrong and SSAO looks plausible but is subtly inverted — the checklists call out every place that happens.

---

## 0. Why this shape, and the frame order

SSAO needs, per screen pixel, a **view-space position** (reconstructed from the depth buffer) and a **normal**, so it can sample a hemisphere of nearby points and count how many are buried behind geometry. Two facts about our renderer decide the whole architecture:

1. **Depth and normal are *outputs* of the one forward pass.** `"Static Meshes"` ([`SceneRenderer.cpp`](../Source/Scene/SceneRenderer.cpp), the pass around line 311) writes color + world-normal + depth together. At the top of the frame there is no camera depth to read — only the sun shadow depth exists.
2. **That pass sums lighting inline.** [`MeshInstanced.slang`](../Shaders/Passes/MeshInstanced.slang) does `o.Color = Direct + Ambient + Emissive` into one RGBA8 target. Once summed, ambient can't be pulled back out.

So the naive *"run SSAO after, multiply scene color by AO"* is **wrong** — it darkens direct sunlight too. AO must attenuate **only the ambient/indirect term**. The clean way to guarantee that is to make AO exist **before** the forward pass, so the forward shader multiplies it into ambient at the one line that already multiplies a per-material AO scalar.

That is the **depth pre-pass** route. Final frame order:

```
Shadow Map        depth-only, sun's POV            (already exists)
Depth Pre-Pass    depth-only, camera POV           (NEW — mirrors Shadow Map)
SSAO              fullscreen, reads depth          (NEW)
SSAO Blur         fullscreen, 4×4 box              (NEW)
Static Meshes     forward; LOADS prepass depth,    (MODIFIED — 1-line AO multiply)
                  samples blurred AO into ambient
DebugDraw / Overlay / Present                       (already exists)
```

**Why the pre-pass and not a post-forward composite:** the depth pre-pass is a foundation, not a tax. It gives early-Z on the heavy forward pass, and a camera depth buffer that SSR, volumetric fog, and TAA will all want later. The forward shader change is a single multiply. The cost — drawing geometry once more for depth — is the same pattern you already run for shadows, and early-Z pays part of it back.

**Which normals SSAO uses (important):** SSAO reconstructs a **geometric normal from depth**, *not* the normal-mapped `m_NormalTexture`. That texture isn't populated until the forward pass, which runs *after* SSAO here — but more importantly, AO *wants* coarse geometric normals. Feeding high-frequency normal-map detail into AO produces noise and haloing. So `m_NormalTexture` stays for deferred/SSR/debug down the line; SSAO does not read it.

---

## 1. Prerequisite — expose `Proj` and `InvProj` in `FrameConstants`

SSAO reconstructs a view-space position from a depth sample (needs `InvProj`) and projects hemisphere sample points back to screen UV (needs forward `Proj`). Today [`FrameConstants`](../Source/Scene/SceneRenderer.h) carries only the *combined* `ViewProj` and `LightViewProj` — neither the split `Proj` nor any inverse. You must add them.

`FrameConstants` is a hand-mirrored, size-locked struct: the CPU layout in `SceneRenderer.h`, the GPU mirror + byte offsets in [`Frame.slang`](../Shaders/Common/Frame.slang), and a `static_assert(sizeof == …)` on both sides. **Edit all three in lockstep or you get silent garbage.**

**Append, don't reorder** — keep every existing offset (0…207) intact so no existing shader load shifts. Add at the end:

| Field | Offset | Bytes | Purpose |
|---|---|---|---|
| `Proj` | 208 | 64 | view → clip (project samples back to UV) |
| `InvProj` | 272 | 64 | clip → view (reconstruct position from depth) |
| `ViewportAO` | 336 | 16 | `.xy` = screen w,h · `.z` = `asuint(AOSlot)` · `.w` = AO strength |

New `sizeof` = **352**.

### CPU side — `SceneRenderer.h`

Add after `ShadowParams` (keep `ShadowMapSlot` + pads where they are, or move them past the new fields — just keep CPU and GPU identical). Illustrative:

```cpp
struct FrameConstants {
    Mat4Packed ViewProj;          // 0
    Mat4Packed LightViewProj;     // 64
    Vec4Packed CameraPosWS;       // 128
    Vec4Packed LightDirWS;        // 144
    Vec4Packed LightColorAmbient; // 160
    Vec4Packed ShadowParams;      // 176
    uint32_t   ShadowMapSlot;     // 192
    uint32_t   Pad0, Pad1, Pad2;  // 196
    Mat4Packed Proj;              // 208  (NEW)
    Mat4Packed InvProj;           // 272  (NEW)
    Vec4Packed ViewportAO;        // 336  (NEW) xy = screen size, z = asuint(AO slot), w = strength
};
static_assert(sizeof(FrameConstants) == 352, "must match Shaders/Common/Frame.slang");
```

### Fill it in `SceneRenderer::Render()`

Where the frame constants are built (around line 246, alongside `FC.ViewProj = m_Camera->GetViewProjection()`), add:

```cpp
const float4x4 Proj    = m_Camera->GetProjection();     // Camera.h:23
const float4x4 InvProj = hlslpp::inverse(Proj);
FC.Proj    = Proj;                                       // Mat4Packed stores transposed (column-stored)
FC.InvProj = InvProj;
FC.ViewportAO = float4((float)m_Width, (float)m_Height,
                       asfloat(m_AOBlurTexture.SampledSlot), // AO slot (see Phase 6/8)
                       /*strength*/ 1.0f);
```

> `Mat4Packed` uses `hlslpp` `store_transposed` (column-stored) and the shader rebuilds each matrix with `LoadMat4ColumnStored` (`transpose(float4x4(C0..C3))`). Your new matrices go through the **same** path — don't hand-pack them differently or they'll transpose.

### GPU side — `Frame.slang`

Add the three fields to the `FrameConstants` struct **and** load them at the matching offsets in `LoadFrameConstants`:

```hlsl
struct FrameConstants {
    float4x4 ViewProj;
    float4x4 LightViewProj;
    float4   CameraPosWS;
    float4   LightDirWS;
    float4   LightColorAmbient;
    float4   ShadowParams;
    uint     ShadowMapSlot;
    float4x4 Proj;         // NEW
    float4x4 InvProj;      // NEW
    float4   ViewportAO;   // NEW
};

// inside LoadFrameConstants(uint Slot):
F.Proj       = LoadMat4ColumnStored(B, 208);
F.InvProj    = LoadMat4ColumnStored(B, 272);
F.ViewportAO = asfloat(B.Load4(336));
```

**Checklist:** ☐ CPU struct ☐ CPU `static_assert(… == 352)` ☐ fill in `Render()` ☐ Slang struct ☐ Slang loads at 208/272/336 ☐ (Slang has no `static_assert`, but the header-comment byte map at the top of `Frame.slang` should be updated too).

---

## 2. The depth pre-pass — mirror the shadow pass

You already have a working depth-only pass: `"Shadow Map"` ([`SceneRenderer.cpp`](../Source/Scene/SceneRenderer.cpp), ~277–299). The camera depth pre-pass is the same shape with two swaps: use the **camera** `ViewProj` (not `LightViewProj`) and target **`m_DepthTexture`** (not the shadow map).

### 2a. A depth-prepass shader

Create `Shaders/Passes/DepthPrepass.slang`: a vertex stage that transforms the instanced vertex to clip space with `Frame.ViewProj` (copy the transform from `MeshInstanced.slang`'s VS / the shadow VS), and **no fragment stage** (depth-only — the rasterizer writes depth for you). This is exactly what `ShadowMap.slang` does, minus the light matrix.

### 2b. A depth-prepass pipeline

Mirror the shadow-map pipeline (`SceneRenderer.cpp` ~74–82):

```cpp
// ColorAttachmentCount = 0  (depth-only, like the shadow pipeline)
// DepthFormat  = Format::D32_SFLOAT
// DepthTest    = true
// DepthWrite   = true
// DepthCompare = CompareOp::Greater      // reverse-Z: nearer = larger
// Cull         = Back, FrontFace = Clockwise   // match the mesh pipeline
```

### 2c. Declare the pass (before SSAO)

```cpp
Rg.Graphics("Depth Pre-Pass")
  .Depth(m_DepthTexture, 0.0f)             // reverse-Z clear to far
  .Execute([this, FrameSlot](rhi::CommandList& C) {
      if (m_Draws.empty()) return;
      C.Bind(m_DepthPrepassPipeline);
      C.SetViewport((uint32_t)m_Width, (uint32_t)m_Height);
      // same per-draw loop as the shadow pass: push FrameSlot + instance data, DrawIndexed
  });
```

### 2d. Let the forward pass LOAD the prepass depth (early-Z)

Right now the forward pass **clears** `m_DepthTexture` and re-renders depth (`.Depth(m_DepthTexture, 0.0f)`). If it keeps clearing, the pre-pass is wasted — you draw depth twice for nothing. Make the forward pass **load** the prepass depth and test against it. Two small edits:

**Add a `DepthLoad` to the render graph.** [`RenderGraph.h`](../Source/Renderer/RenderGraph.h) has `ColorLoad` (LoadOp::Load) but only a clearing `Depth`. Add a sibling that pushes a `Depth` use with `ClearOnLoad = false` — `ExecutePass` already maps `ClearOnLoad ? Clear : Load`, so no executor change is needed:

```cpp
PassBuilder& DepthLoad(TextureHandle h);   // Access::Depth, ClearOnLoad = false
```

**Switch the forward pipeline's depth to `GreaterEqual` and stop writing depth.** With the prepass owning depth, the forward pass must accept fragments at *equal* reverse-Z depth (`Greater` alone rejects them → nothing draws). In the mesh pipeline desc (`MeshPipeline.h` defaults / where `m_MeshPipeline` is created):

```cpp
DepthCompare = CompareOp::GreaterEqual;   // was Greater — MUST change with a prepass
DepthWrite   = false;                     // prepass already wrote depth
```

Then change the forward pass declaration from `.Depth(m_DepthTexture, 0.0f)` to `.DepthLoad(m_DepthTexture)`.

> ⚠️ **`Greater` + prepass = black screen.** Reverse-Z depth-equal fragments fail a strict `Greater` test. `GreaterEqual` is the robust choice (it also tolerates any tiny position-invariance differences between the two pipelines). If you skip the `DepthLoad`/`GreaterEqual` change, everything still *works* but you get zero early-Z benefit and draw depth twice.

**Checklist:** ☐ `DepthPrepass.slang` ☐ `m_DepthPrepassPipeline` ☐ `"Depth Pre-Pass"` node before SSAO ☐ `RenderGraph::DepthLoad` ☐ forward pipeline → `GreaterEqual` + `DepthWrite = false` ☐ forward `.Depth(...)` → `.DepthLoad(...)`.

---

## 3. SSAO resources — kernel + noise

Two CPU-side inputs, created once (in the `SceneRenderer` constructor).

### 3a. Hemisphere kernel

A fixed set of N sample offsets in the **+Z tangent hemisphere** (Z is the surface normal direction), clustered toward the origin so nearby geometry dominates. N = 32 is a good default (16 = faster/noisier, 64 = smoother/heavier).

Generation recipe (bake once, at init, with any RNG):

```
for i in 0..N-1:
    s = normalize( float3( rand(-1,1), rand(-1,1), rand(0,1) ) )   // +Z hemisphere
    s *= rand(0,1)                                                  // inside the hemisphere, not just the shell
    scale = lerp(0.1, 1.0, (i / N)^2)                              // accelerating: cluster near origin
    kernel[i] = s * scale
```

**Storage — pick one:**
- **Hardcoded `static const float3 kKernel[32]` in the SSAO shader** — zero new plumbing, deterministic, easiest to start with. Bake the 32 values once and paste them.
- **A small bindless storage buffer** (create with `Device::CreateBuffer`, pass `.BindlessSlot` via push constant) — tweakable at runtime, scales to any N. Do this once you want to tune N live.

Start hardcoded.

### 3b. Noise texture (4×4, tiled)

A 4×4 texture of random **rotation vectors** in the tangent plane, tiled across the screen to rotate the kernel per-pixel. This trades low-frequency banding for high-frequency noise the blur then removes.

Use **`Format::RG8_UNORM`** (2 bytes/texel — no fp16 packing needed). Each texel stores `rot.xy` remapped to `[0,1]`; the shader decodes `*2-1`:

```cpp
// 16 texels, each = normalize(float2(rand(-1,1), rand(-1,1))) stored as (v*0.5+0.5)*255
uint8_t noise[4*4*2];
// ... fill ...
rhi::TextureDesc nd{};
nd.Width = 4; nd.Height = 4;
nd.Fmt = rhi::Format::RG8_UNORM;
nd.Usage = rhi::TextureUsage::Sampled;      // TransferDst is added automatically for InitialData
nd.InitialData = noise;
nd.InitialDataSize = sizeof(noise);          // 32 bytes
nd.DebugName = "SSAO Noise";
m_NoiseTexture = m_RHI->CreateTexture(nd);   // .SampledSlot -> push to the SSAO shader
```

> Tile it with **`kSamplerPointWrap` (slot 3)** — `NEAREST + REPEAT`. Never filter the noise. Sample it at `SV_Position.xy / 4.0` so it repeats every 4 pixels.

### 3c. AO render targets

Two full-res single-channel targets (add to ctor / dtor / `Resize` — see Phase 8):

```cpp
// m_AOTexture and m_AOBlurTexture
.Fmt   = rhi::Format::R8_UNORM,
.Usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
```

> `R8_UNORM` is safe as a **fragment (ColorAttachment)** target. If you later move the blur to a compute shader writing a *storage* image, prefer `R16F` — `R8_UNORM` storage writes aren't universally supported. Half-resolution AO targets are a common perf win; start full-res and correct.

---

## 4. The SSAO pass — the math that must be exact

Create `Shaders/Passes/SSAO.slang`. Reuse the fullscreen-triangle VS from [`Fullscreen.slang`](../Shaders/Common/Fullscreen.slang) (`FullscreenVS`, host issues `Draw(3)`). The fragment stage does all the work.

### Push constants

```hlsl
struct PushConsts {
    uint FrameSlot;    // FrameConstants (Proj, InvProj, ViewportAO)
    uint DepthSlot;    // m_DepthTexture.SampledSlot
    uint NoiseSlot;    // m_NoiseTexture.SampledSlot
    uint _pad;
    float Radius;      // world units, e.g. 0.5
    float Bias;        // view-space, e.g. 0.025
    float Power;       // contrast, e.g. 1.5..2.0
    float _pad2;
};
[[vk::push_constant]] PushConsts pc;
```

### 4a. Reconstruct view-space position from depth

Screen UV → NDC is direct in Helio (the Y-flip is already baked into the projection, so `v = ndc.y*0.5+0.5` — see the shadow code note in `Frame.slang` ~73). Depth is `ndc.z` under reverse-Z. Use `InvProj`:

```hlsl
float3 ReconstructViewPos(float2 uv, float depth, float4x4 invProj) {
    float3 ndc = float3(uv * 2.0 - 1.0, depth);       // both x and y: uv*2-1
    float4 v   = mul(invProj, float4(ndc, 1.0));      // column-vector: matrix on the LEFT
    return v.xyz / v.w;
}
```

> Analytic cross-check for our infinite reverse-Z projection ([`Math.cpp`](../Source/Core/Math/Math.cpp) ~72): `ndc.z = NearZ / z_view`, so `z_view = NearZ / depth`. If your reconstructed `.z` doesn't match `NearZ/depth`, your `InvProj` upload is transposed.

**Early-out on the background:** where `depth == 0` there is no geometry (reverse-Z far). Return AO = 1 immediately — don't reconstruct (`z_view` would be infinite).

### 4b. Geometric normal from depth

Use neighboring depth taps (more robust at silhouettes than raw `ddx/ddy`):

```hlsl
float2 texel = 1.0 / Frame.ViewportAO.xy;
float3 P  = ReconstructViewPos(uv, depth, Frame.InvProj);
float3 Px = ReconstructViewPos(uv + float2(texel.x, 0), SampleDepth(uv + float2(texel.x,0)), Frame.InvProj);
float3 Py = ReconstructViewPos(uv + float2(0, texel.y), SampleDepth(uv + float2(0,texel.y)), Frame.InvProj);
float3 N  = normalize(cross(Px - P, Py - P));
if (N.z > 0.0) N = -N;   // face the camera: LH view space, camera looks +Z, so a visible surface's normal has N.z < 0
```

> The `if (N.z > 0) N = -N` both fixes the cross-product order ambiguity and guarantees the hemisphere opens toward the camera. Keep it. (Refinement for later: pick the better of the left/right and up/down neighbor by smallest depth delta to avoid 1-pixel edge bleed.)

### 4c. Rotate the kernel and accumulate occlusion

```hlsl
float3 randomVec = float3(SampleNoise(In.Position.xy / 4.0) * 2.0 - 1.0, 0.0); // decode RG8, z=0
float3 T = normalize(randomVec - N * dot(randomVec, N));   // Gram-Schmidt
float3 B = cross(N, T);

float occlusion = 0.0;
for (int i = 0; i < KERNEL_SIZE; ++i) {
    // tangent -> view, written explicitly (avoids float3x3 row/column ambiguity):
    float3 k = kKernel[i];
    float3 samplePos = P + (k.x * T + k.y * B + k.z * N) * pc.Radius;

    // project the sample point back to screen UV via forward Proj
    float4 clip = mul(Frame.Proj, float4(samplePos, 1.0));
    float2 sUV  = (clip.xy / clip.w) * 0.5 + 0.5;
    if (any(sUV < 0.0) || any(sUV > 1.0)) continue;   // off-screen sample contributes nothing

    // the actual scene surface at that screen location, in view space
    float sceneDepth = SampleDepth(sUV);              // ndc.z (reverse-Z)
    float sceneZ     = ReconstructViewPos(sUV, sceneDepth, Frame.InvProj).z;

    // occluded when the visible surface is CLOSER to camera than the sample point
    // (LH +Z-forward: smaller z_view = closer). Bias avoids self-occlusion acne.
    float occluded = (sceneZ < samplePos.z - pc.Bias) ? 1.0 : 0.0;

    // range check: ignore occluders farther than Radius from P (no haloing across depth gaps)
    float rangeCheck = smoothstep(0.0, 1.0, pc.Radius / max(abs(P.z - sceneZ), 1e-4));
    occlusion += occluded * rangeCheck;
}

float ao = 1.0 - occlusion / float(KERNEL_SIZE);
ao = pow(saturate(ao), pc.Power);
return float4(ao, 0, 0, 1);   // R8_UNORM target
```

> ⚠️ **The occlusion sign is the #1 SSAO bug.** In Helio's LH +Z-forward *view* space, `z_view` grows with distance, so a surface is an occluder when its `sceneZ` is **less** than the sample's `samplePos.z`. If your AO comes out inverted (cavities bright, faces dark), flip this comparison — do not "fix" it by inverting `ao` at the end, which breaks the range check.

### 4d. Pipeline + pass

Pipeline: copy the **Overlay** pipeline desc ([`Overlay.cpp`](../Source/Editor/Overlay.cpp) ~59–72) — `ColorAttachmentCount = 1`, `ColorFormats[0] = Format::R8_UNORM`, `DepthFormat = Undefined`, `DepthTest/Write = false`, `Cull = None`, `PushConstantBytes = sizeof(PushConsts)`.

Pass (after the depth pre-pass):

```cpp
Rg.Graphics("SSAO")
  .Read(m_DepthTexture)          // -> SHADER_READ_ONLY (auto-barrier)
  .Color(m_AOTexture)            // clears; you overwrite every pixel anyway
  .Execute([this, FrameSlot](rhi::CommandList& C) {
      C.Bind(m_SSAOPipeline);
      C.SetViewport((uint32_t)m_Width, (uint32_t)m_Height);
      SSAOPush pc{ FrameSlot, m_DepthTexture.SampledSlot, m_NoiseTexture.SampledSlot,
                   0, /*Radius*/0.5f, /*Bias*/0.025f, /*Power*/1.75f, 0 };
      C.Push(pc);
      C.Draw(3);
  });
```

> Sample depth/normal reads with **`kSamplerPointClamp` (slot 2)** via `GetSampler(2)` — never linear-filter the depth buffer for reconstruction.

---

## 5. The blur pass — kill the noise tiling

The 4×4 noise makes raw AO look like it's under a screen door. Because the noise period is exactly 4 texels, a **4×4 box blur** averages one full period and removes it cleanly.

Create `Shaders/Passes/SSAOBlur.slang` — fullscreen, reads `m_AOTexture`, averages the 4×4 neighborhood, writes `m_AOBlurTexture`:

```hlsl
float result = 0.0;
float2 texel = 1.0 / Frame.ViewportAO.xy;    // or pass resolution via push constant
for (int x = -2; x < 2; ++x)
for (int y = -2; y < 2; ++y)
    result += GetTexture2D(pc.AOSlot).Sample(GetSampler(kSamplerPointClamp),
                                             In.UV + float2(x, y) * texel).r;
return float4(result / 16.0, 0, 0, 1);
```

Pipeline: same shape as the SSAO pipeline (`R8_UNORM`, no depth). Pass:

```cpp
Rg.Graphics("SSAO Blur")
  .Read(m_AOTexture)
  .Color(m_AOBlurTexture)
  .Execute([this](rhi::CommandList& C) {
      C.Bind(m_SSAOBlurPipeline);
      C.SetViewport((uint32_t)m_Width, (uint32_t)m_Height);
      BlurPush pc{ m_AOTexture.SampledSlot /*, resolution if not in Frame */ };
      C.Push(pc);
      C.Draw(3);
  });
```

> A separable Gaussian is overkill for a 4-tap-period noise; the 4×4 box is the standard SSAO blur and it's cheaper. (You can move this to compute later using [`BoxBlur.slang`](../Shaders/Compute/BoxBlur.slang) as the template — `numthreads(8,8,1)`, `GetStorageImage`.)

---

## 6. Apply AO in the forward pass — one line

The forward pass now samples the blurred AO and folds it into ambient. Two edits.

### 6a. Declare the read

On the `"Static Meshes"` pass builder (`SceneRenderer.cpp` ~311), add:

```cpp
Pass.Read(m_AOBlurTexture);   // same mechanism as the existing .Read(m_ShadowMapTexture)
```

### 6b. Multiply into ambient only

In [`MeshInstanced.slang`](../Shaders/Passes/MeshInstanced.slang), the ambient term is at ~199:

```hlsl
float3 Ambient = Frame.LightColorAmbient.w * Albedo * Frame.LightColorAmbient.rgb * AO;
```

That `AO` scalar (~163) is the per-material occlusion texture. Multiply the **screen-space** AO into the same spot. The fragment's screen UV comes from `SV_Position.xy / screenSize`:

```hlsl
float2 screenUV = In.Position.xy / Frame.ViewportAO.xy;
float  ssao     = GetTexture2D(asuint(Frame.ViewportAO.z))   // AO slot packed in Phase 1
                    .Sample(GetSampler(kSamplerPointClamp), screenUV).r;
float3 Ambient  = Frame.LightColorAmbient.w * Albedo * Frame.LightColorAmbient.rgb * AO * ssao;
```

> ⚠️ **Never multiply `ssao` into `Direct`.** Direct sunlight is already occluded by the shadow map (`Direct = (Diffuse+Specular) * Radiance * NdotL * Shadow`, ~198). SSAO attenuates ambient/indirect *only*. The final line stays `o.Color = Direct + Ambient + Emissive`.

`In.Position` is the pixel-shader `SV_Position` (pixel-center coords). If `VSOut` doesn't already carry `SV_Position` into the fragment stage under a readable name, it does implicitly — read it as the `SV_Position`-tagged input.

---

## 7. Wire the passes in order

The graph runs in **declaration order — no reordering** ([`RenderGraph.md`](RenderGraph.md)). The old `// AO` placeholder comment sits at `SceneRenderer.cpp:301`, *before* the mesh pass — but SSAO needs the prepass depth, so the real insertion is:

```cpp
// (Shadow Map pass — unchanged, if Shadow.Enabled)

// ---- Depth pre-pass -------------------------------------------------
Rg.Graphics("Depth Pre-Pass").Depth(m_DepthTexture, 0.0f).Execute(/* Phase 2c */);

// ---- SSAO + blur ----------------------------------------------------
Rg.Graphics("SSAO").Read(m_DepthTexture).Color(m_AOTexture).Execute(/* Phase 4d */);
Rg.Graphics("SSAO Blur").Read(m_AOTexture).Color(m_AOBlurTexture).Execute(/* Phase 5 */);

// ---- Static Meshes (forward) ---------------------------------------
auto Pass = Rg.Graphics("Static Meshes")
              .Color(m_ColorTexture, 0.1274, 0.3005, 0.8469, 1.0)
              .Color(m_NormalTexture)
              .DepthLoad(m_DepthTexture);        // was .Depth(..., 0.0f)
Pass.Read(m_AOBlurTexture);                       // NEW
if (Shadow.Enabled && HasScene) Pass.Read(m_ShadowMapTexture);
Pass.Execute(/* unchanged mesh loop */);

// (DebugDraw, Overlay, OverlayHook, Present — unchanged)
```

The graph inserts every layout transition from the declared `.Read()` / `.Color()` / `.DepthLoad()` accesses automatically — you don't write barriers.

---

## 8. Lifecycle — and fix two pre-existing bugs

Your new textures (`m_AOTexture`, `m_AOBlurTexture`, `m_NoiseTexture`) must be created in the ctor, destroyed in the dtor, and — for the two full-res AO targets — recreated on window resize. While you're there, fix two bugs the scan found:

> ⚠️ **Existing bug 1:** the `SceneRenderer` destructor (~92–103) destroys color, depth, and shadow but **not** `m_NormalTexture` — it leaks. Add it.
> ⚠️ **Existing bug 2:** `SceneRenderer::Resize()` (~392–425) recreates only color and depth, **not** `m_NormalTexture` — after a resize the normal G-buffer keeps its old size and mismatches the others. Add it.

So `Resize()` must destroy + recreate: **color, depth, normal (bug fix), `m_AOTexture`, `m_AOBlurTexture`** (all screen-sized). The 4×4 noise and the kernel are size-independent — create once, never resize. The dtor destroys all of them.

**Checklist:** ☐ ctor creates AO + noise ☐ dtor destroys normal (bug) + AO + noise ☐ `Resize` recreates normal (bug) + both AO targets ☐ `FC.ViewportAO.z` re-reads `m_AOBlurTexture.SampledSlot` after any recreate.

---

## 9. Verify and tune

**See the AO buffer directly first.** Before touching the ambient term, temporarily `Rg.Present(m_AOBlurTexture)` (it's `Sampled`; to present it you'd blit — simplest is to route it through a debug fullscreen copy, or just eyeball it in RenderDoc). You want: white on open surfaces, darkening in creases, contact shadows where objects meet the floor. Then wire Phase 6.

**Tuning parameters:**

| Param | Start | Effect |
|---|---|---|
| `Radius` | 0.5 (world m) | Sampling reach. Too large = soft, global darkening + haloing; too small = only razor-thin contact AO. Scale to your scene units. |
| `Bias` | 0.025 | Self-occlusion rejection. Raise until flat surfaces stop shimmering (acne); too high erases fine contact AO. |
| `Power` | 1.5–2.0 | Contrast. Higher = punchier, darker cavities. |
| `KERNEL_SIZE` | 32 | Quality vs cost. 16 needs a stronger blur; 64 is smoother. |
| Ambient strength | `LightColorAmbient.w` | AO only shows where there *is* ambient. With ambient at 0.03 the effect is subtle — that's correct. |

**Sign-convention checklist (run this if it looks wrong):**

- ☐ Reconstructed `.z` equals `NearZ / depth`? If not → `InvProj` transposed (Phase 1 packing).
- ☐ AO inverted (cavities bright)? → flip the occlusion comparison in 4c, **not** the final `ao`.
- ☐ Whole screen uniformly dark? → `Radius` too large, or you multiplied `ssao` into `Direct` (Phase 6 warning).
- ☐ Black screen after the prepass? → forward pipeline still on `Greater`; must be `GreaterEqual` (Phase 2d).
- ☐ Noise never resolves? → blur reading the wrong slot, or noise sampled with a *clamp* sampler instead of `kSamplerPointWrap`.
- ☐ AO wrong after resizing the window? → a target missing from `Resize()` (Phase 8), or `ViewportAO.z` not refreshed.

---

## Appendix — Helio conventions that shaped this

- **Reverse-Z:** depth cleared to `0.0` (far), `CompareOp::Greater(Equal)`, `ndc.z = NearZ / z_view`. Linearize with `z_view = NearZ / depth`.
- **LH +Y-up +Z-forward, view space:** camera looks down **+Z**; larger `z_view` = farther. Visible surface normals have `N.z < 0`.
- **Column-vector:** always `mul(Matrix, vector)`, matrix on the left. `mul(v, M)` transposes your transform.
- **Y-flip baked in the projection** (`row1 = -YScale`): screen textures are stored Y-down; fullscreen passes sample `In.UV` directly, and `ndc.xy = uv*2-1` needs no manual flip.
- **Column-stored matrices** (`Mat4Packed` / `store_transposed`): rebuilt in-shader with `LoadMat4ColumnStored`. New matrices must go through the same path.
- **Bindless everything:** textures via `GetTexture2D(slot)`, samplers via `GetSampler(kSampler*)`, frame data via `LoadFrameConstants(FrameSlot)`. Slots travel in the 128-byte push constant.
- **5 static samplers only** (no `CreateSampler`): `LinearClamp=0`, `LinearWrap=1`, `PointClamp=2`, `PointWrap=3`, `ShadowLinear=4`. SSAO uses `PointClamp` (depth) and `PointWrap` (noise).

---

*Route chosen: depth pre-pass (hybrid-forward). The alternative — forward writes ambient to its own `SV_Target2` and a post-forward composite applies AO — avoids the second geometry pass but is more invasive to the core lighting shader and adds an ambient render target + composite. If you ever go fully deferred, SSAO moves to read the real G-buffer normal and this prepass folds into the G-buffer fill.*
