# Post-Processing in Helio — a build-it-up tutorial

This is a hands-on course, not a spec. We build Helio's screen-space / post-processing layer **one small, runnable milestone at a time** — each one you can see on screen before moving to the next, so you never debug plumbing and math at the same time. Every step says *where* the file lives, *why* the engine is shaped this way, and *what* convention is in play. Assume nothing; if a term shows up, it gets explained where it's first used.

**This is not "the SSAO tutorial."** SSAO is one tenant that happens to exercise the hardest parts. What we're really building is the reusable foundation that *every* screen effect sits on — so tonemapping, bloom, vignette, FXAA, SSR, and other AO methods (GTAO, HBAO+) all slot into the same machinery later.

There are two families of effect, and the roadmap builds the shared parts before either:

- **Color post-processing** — reads the finished image, transforms it, writes a new image. Tonemap, bloom, vignette, FXAA, color grading. (Milestone 0 builds this backbone.)
- **Geometry-aware screen-space effects** — need to know the *shape* of the scene (depth + normals), not just its color. SSAO, SSR, DOF. These reuse the color-post backbone **plus** a depth/reconstruction/normal substrate.

---

## The roadmap

Each milestone is a rung: runnable, visible, and built on the one before.

### Part A — the post-process backbone (color only, no geometry, no math)
- **M0 — Your first post-process pass.** A fullscreen effect that reads the scene color and produces the image we present, with UI composited on top. Passthrough → grayscale. *(done)*
- **M1 — Chaining effects.** A second effect (vignette) after the first, introducing ping-pong between two targets — the pattern for stacking N effects. *(optional — skipped for now; ping-pong reappears at SSAO→blur)*

### Part B — the geometry substrate (what SSAO/SSR need)
- **M2 — See your depth (reverse-Z).** Camera depth is already produced; make it *visible* with a reusable debug-view pass and learn the reverse-Z linearization M3 is built on. *You are here.*
- **M3 — Reconstruction library.** `Shaders/Common/ScreenSpace.slang`: turn a depth sample back into a 3D view-space position. Visualize it. This is where reverse-Z and column-vector math get taught properly.
- **M4 — Geometry normal buffer.** A shared normal target that AO *and* SSR *and* future deferred work read. Visualize it.

### Part C — SSAO (first geometry-aware effect)
- **M5 — AO buffer, no-op fill.** Wire an "AO texture" into lighting with a constant `AO = 1`. Proves the whole pipe end-to-end before any AO math exists.
- **M6 — The SSAO algorithm.** Hemisphere kernel + noise + occlusion. Visualize raw AO.
- **M7 — Blur.** Remove the noise pattern.
- **M8 — Consume + tune.** Fold AO into ambient light; dial it in.

### Beyond (built on the same foundation, when you want them)
Bloom and tonemap reuse Part A. SSR reuses Part B. GTAO/HBAO+ swap in at M5's seam — lighting reads *"the AO buffer,"* never *"SSAO,"* so the technique behind it is replaceable.

> We deliberately do **not** build a formal "effect framework" up front. We build the pieces every effect provably needs and leave technique-swapping as an obvious seam. The abstraction gets added the day a *second* effect needs it — not before.

---

## Mini-glossary (skim now, refer back later)

You don't need to absorb these yet — each is taught in the milestone where it first matters.

- **Bindless** — textures/buffers aren't bound one-by-one; they live in giant global arrays and shaders index them by an integer **slot**. You pass slots around, not handles.
- **Reverse-Z** — Helio stores depth backwards: near = `1.0`, far = `0.0`, cleared to `0.0`. It gives far better precision. (Matters from M2 on.)
- **Column-vector math** — matrices multiply as `mul(M, v)` with the matrix on the **left**. Writing it the other way transposes your transform. (Matters from M3 on.)
- **Fullscreen triangle** — instead of 3D geometry, a post-process pass draws one oversized triangle covering the screen, so its fragment shader runs once per pixel. `Draw(3)`, no vertex buffer.
- **Render graph** — you *declare* passes (`Rg.Graphics("Name").Read(...).Color(...).Execute(...)`) and the graph inserts the GPU memory barriers for you. Passes run in the order you declare them. See [RenderGraph.md](RenderGraph.md).

---

# Milestone 0 — Your first post-process pass

**Goal.** Insert a fullscreen pass between the 3D scene and the UI that reads the finished scene color and writes the image we hand to the screen. You'll build it as an exact **passthrough** first (screen looks identical → proves the wiring), then flip it to **grayscale** (scene goes black-and-white but the UI stays colored → proves the shader runs and sits in the right place).

**Why start here.** This is the backbone. Tonemapping is this exact shape with different fragment math. Bloom is this plus some downsamples. Vignette, FXAA, color grading — all live here. Get this one right and half the future work is just "new fragment shader, same plumbing."

---

## 0.1 — The mental model (no code yet)

Three facts about how a frame currently ends, so the rest makes sense:

1. **Your scene is not drawn to the window.** It's drawn into an offscreen texture called `m_ColorTexture` (debug name `"SceneColor"`). Only at the very end does [`SceneRenderer.cpp`](../Source/Scene/SceneRenderer.cpp) line ~442 do `Rg.Present(m_ColorTexture)`, which copies that texture onto the window.

2. **A post-process pass is a per-pixel program.** It reads a source texture, does some math on each pixel's color, and writes a destination texture. Because it must touch every pixel, we draw it as a single screen-covering triangle — the *fullscreen triangle* — so the GPU runs our fragment shader once per pixel. No 3D geometry, no depth.

3. **A pass cannot read and write the same texture at once.** So post-processing reads `m_ColorTexture` and writes a **new** texture, then we present the new one.

And the ordering that matters:

```
3D scene  ──►  m_ColorTexture
                    │
                    ▼   (Milestone 0 adds this)
             PostProcess pass  ──►  m_PostColor
                    │
                    ▼
             Debug lines / stats / editor UI   (drawn ON TOP of m_PostColor)
                    │
                    ▼
                 Present(m_PostColor)
```

Post-processing goes **after the scene but before the UI** — you don't want your stats text or editor panels tonemapped, blurred, or turned grayscale. That single ordering decision is most of what "post-processing architecture" means.

---

## 0.2 — The files you'll touch

A quick tour of *where things live and why* — this is the part the codebase never spelled out for you:

| Folder | What lives there | Your M0 change |
|---|---|---|
| `Shaders/Common/` | Shared shader code other shaders `import` (e.g. `Fullscreen.slang`, `Bindless.slang`) | *(nothing — you reuse them)* |
| `Shaders/Passes/` | One file per render pass | **add** `PostProcess.slang` |
| `Source/Scene/SceneRenderer.h` | The renderer's members: textures, pipelines | **add** a texture + a pipeline handle |
| `Source/Scene/SceneRenderer.cpp` | Frame orchestration: create resources, declare passes | **create** the resources, **declare** the pass |

**How shaders build (important, and undocumented until now):** the build **auto-discovers** every `.slang` under `Shaders/` — [`Tools/Build/CompileShaders.cmake`](../Tools/Build/CompileShaders.cmake) globs them recursively with `CONFIGURE_DEPENDS` and runs `slangc` on each, producing a matching `.spv`. So you **do not register a new shader anywhere** — you drop `PostProcess.slang` into `Shaders/Passes/`, rebuild, and `Shaders/Passes/PostProcess.spv` appears. That's why pipeline code refers to shaders by their `.spv` path.

---

## 0.3 — Step A: write the shader (passthrough first)

Create `Shaders/Passes/PostProcess.slang`:

```hlsl
// PostProcess.slang — the fullscreen post-process backbone.
// Reads the scene color and (for now) passes it straight through.

import Fullscreen;   // Shaders/Common/Fullscreen.slang — the screen-covering triangle
import Bindless;     // Shaders/Common/Bindless.slang  — GetTexture2D / GetSampler

// Push constants: a tiny blob of per-draw data (max 128 bytes). We only need
// to tell the shader WHICH bindless texture is the scene color.
struct PostPush {
    uint SourceSlot;   // bindless slot of the texture we're reading
};
[[vk::push_constant]] PostPush pc;

[shader("vertex")]
FullscreenVSOut VSMain(uint id : SV_VertexID) {
    return FullscreenVS(id);   // emits the fullscreen triangle; In.UV spans [0,1]
}

[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
    // Look up the scene color at this pixel. GetTexture2D(slot) fetches the
    // texture from the global bindless array; GetSampler(...) picks how it's
    // filtered. kSamplerLinearClamp = smooth, edge-clamped — the safe default.
    float3 color = GetTexture2D(pc.SourceSlot)
                     .Sample(GetSampler(kSamplerLinearClamp), In.UV).rgb;
    return float4(color, 1.0);
}
```

Line by line, because this is the vocabulary you'll reuse forever:

- **`import Fullscreen;`** pulls in `FullscreenVS` / `FullscreenVSOut`. `FullscreenVS(id)` turns the vertex index (0,1,2) into one big triangle covering the screen, and hands the fragment stage `In.UV` in `[0,1]` — pixel (0,0) top-left is UV (0,0). You never write vertex math for post-process; you reuse this.
- **`import Bindless;`** gives you `GetTexture2D(slot)` and `GetSampler(slot)`. In a bindless engine the shader doesn't "bind" a texture — it indexes the global array by an integer slot you pass in.
- **The push constant** carries that slot. Push constants are the fastest way to hand a shader a few numbers per draw; here it's just `SourceSlot`.
- **`PSMain`** samples the source at `In.UV` and returns it unchanged. `.rgb` drops alpha; we re-add `1.0`.
- **`kSamplerLinearClamp`** is one of five fixed samplers the engine ships (defined in `Bindless.slang`). Linear = smooth filtering; clamp = don't wrap at edges. Right choice for reading a screen texture.

---

## 0.4 — Step B: add the destination texture

The pass needs somewhere to write. In [`SceneRenderer.h`](../Source/Scene/SceneRenderer.h), next to the other targets (~line 144), add a member:

```cpp
rhi::TextureHandle m_PostColor;   // post-process output — what we present
```

In the constructor ([`SceneRenderer.cpp`](../Source/Scene/SceneRenderer.cpp), alongside `m_ColorTexture` ~line 25), create it — same size and format as SceneColor:

```cpp
m_PostColor = m_RHI->CreateTexture({
    .Width  = static_cast<uint32_t>(Width),
    .Height = static_cast<uint32_t>(Height),
    .Fmt    = rhi::Format::RGBA8_SRGB,
    .Usage  = rhi::TextureUsage::ColorAttachment  // we render into it
            | rhi::TextureUsage::Sampled           // (future effects may read it)
            | rhi::TextureUsage::TransferSrc,       // Present blits it → needs this
    .DebugName = "PostColor"
});
```

> **Why those usage flags:** `ColorAttachment` = "a pass can render into it," `Sampled` = "a shader can read it," `TransferSrc` = "it can be the source of a blit" — which `Present` needs, because presenting is a blit onto the window.

In the destructor (~line 123), destroy it:

```cpp
m_RHI->DestroyTexture(m_PostColor);
```

> **While you're in the destructor:** it currently destroys color, depth, and shadow map only. Add the three that are silently leaking — `m_NormalTexture`, `m_AO`, and `m_DepthPrepassPipeline` (via `DestroyPipeline`) — plus your new `m_PostColor`. Leaks won't crash you, but "clean up what you create" is a convention worth building now.

In `Resize()` (~line 470, where color/normal/depth/AO are destroyed and recreated), add `m_PostColor` to both the destroy list and the recreate list, mirroring `m_ColorTexture`. Any window-sized target must be rebuilt on resize or it mismatches the others.

---

## 0.5 — Step C: create the pipeline

A *pipeline* bundles a compiled shader with fixed state (which formats it writes, whether it tests depth, how it culls). In the constructor (near your `m_DepthPrepassPipeline`, ~line 79), add:

```cpp
m_PostPipeline = m_RHI->CreateGraphicsPipeline({
    .ShaderPath = "Shaders/Passes/PostProcess.spv",
    .ColorFormats = { rhi::Format::RGBA8_SRGB },  // one output, matches m_PostColor
    .ColorAttachmentCount = 1,
    .DepthFormat = rhi::Format::Undefined,         // post-process has NO depth
    .Cull = rhi::CullMode::None,                   // never cull the fullscreen triangle
    .DepthTest = false,
    .DepthWrite = false,
    .DebugName = "PostProcess"
});
```

And the member in `SceneRenderer.h` (near `m_DepthPrepassPipeline`):

```cpp
rhi::PipelineHandle m_PostPipeline;
```

The three fields that make this a *post-process* pipeline, versus your 3D mesh pipeline:

- **`DepthFormat = Undefined`, `DepthTest/DepthWrite = false`** — post-process doesn't care about 3D depth. It just paints pixels. (Contrast your depth-prepass pipeline, which is all about depth.)
- **`Cull = None`** — culling throws away triangles facing the "wrong" way. The fullscreen triangle has no meaningful facing, so culling it would risk a blank screen. Always `None` for fullscreen passes.

> If your `GraphicsPipelineDesc` sets color formats by index (like the Overlay pipeline does: `Pd.ColorFormats[0] = ...`), the equivalent is `.ColorFormats[0] = rhi::Format::RGBA8_SRGB` — same thing, one output slot.

---

## 0.6 — Step D: declare the pass and rewire the ending

Now the payoff. In `Render()`, find the end of the `"Static Meshes"` pass block (the closing `}` around line 419) — **before** the "Debug lines + stats overlay" block (~line 421). Insert:

```cpp
// ---- Post-process: scene color -> presented image --------------------
Rg.Graphics("PostProcess")
  .Read(m_ColorTexture)      // SOURCE — graph transitions it to shader-readable
  .Color(m_PostColor)        // DESTINATION — cleared, then we overwrite every pixel
  .Execute([this](rhi::CommandList& C) {
      C.Bind(m_PostPipeline);
      C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
      PostPush pc{ m_ColorTexture.SampledSlot };  // hand the shader the source slot
      C.Push(pc);
      C.Draw(3);             // the fullscreen triangle — no vertex/index buffers
  });
```

`.Read(m_ColorTexture)` is how the render graph knows to insert the barrier that makes SceneColor readable by a shader — you saw the same `.Read(m_ShadowMapTexture)` on the mesh pass. `.Color(m_PostColor)` declares the render target. `PC{ m_ColorTexture.SampledSlot }` passes the *bindless slot* of the source into the shader.

Then **retarget everything after it** from `m_ColorTexture` to `m_PostColor`, so the UI composites onto the post-processed image and we present that. Change these four spots (lines ~424–442):

```cpp
m_DebugDraw.Render(Rg, m_PostColor, m_Camera->GetViewProjection(), m_Width, m_Height);
// ...
m_Overlay.Render(Rg, m_PostColor, m_Width, m_Height);
// ...
m_OverlayHook(Rg, m_PostColor, /*w*/, /*h*/);
// ...
Rg.Present(m_PostColor);
```

> The overlay and debug-draw pipelines were created for the `RGBA8_SRGB` format, and `m_PostColor` is `RGBA8_SRGB` — so they compose onto it with no format mismatch. That's not luck; keeping your post target the same format as the scene target is what makes the swap a one-liner.

---

## 0.7 — Build, run, verify (passthrough)

Rebuild (the shader auto-compiles — no registration). Run. **The screen should look exactly as it did before.** That's the whole point of step one: if a passthrough looks identical, your post-process stage is correctly inserted, correctly sampling the scene, and correctly presented.

If it's black or wrong, walk this list:

- ☐ Is the `"PostProcess"` pass declared **after** Static Meshes and **before** Present? (Declaration order = execution order.)
- ☐ Does `Rg.Present(...)` name `m_PostColor` (not `m_ColorTexture`)?
- ☐ Is `PostPush.SourceSlot` set to `m_ColorTexture.SampledSlot`?
- ☐ Does the pipeline's color format (`RGBA8_SRGB`) match `m_PostColor`'s format?
- ☐ Did `Shaders/Passes/PostProcess.spv` actually get produced by the build?

---

## 0.8 — Step E: make it do something (grayscale)

Now prove the fragment shader is really running. Change only `PSMain`:

```hlsl
[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
    float3 color = GetTexture2D(pc.SourceSlot)
                     .Sample(GetSampler(kSamplerLinearClamp), In.UV).rgb;
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));  // Rec.709 luminance
    return float4(luma, luma, luma, 1.0);
}
```

`dot(color, weights)` collapses RGB to a single brightness. The weights aren't equal because your eye is far more sensitive to green than blue — this is the standard Rec.709 luminance, the same math tonemapping and many effects lean on.

Rebuild, run. **The 3D scene is now grayscale — but the stats overlay and any editor UI stay in color.** That visible split *is* the lesson: the post-process transformed the scene, and the UI, drawn afterward onto `m_PostColor`, was untouched. You've proven the ordering.

---

## 0.9 — What you built, and why it's reusable

You now have the backbone every color effect reuses:

- a **source** texture (the scene),
- a **fullscreen shader** that transforms it,
- a **destination** texture,
- a **slot in the frame** after the scene and before the UI.

Tonemapping is this with a tone curve in `PSMain`. Vignette multiplies by a radial falloff. FXAA reads neighboring pixels. To **chain** two effects you *ping-pong*: effect 1 reads A → writes B, effect 2 reads B → writes A, present the last one written. That's Milestone 1, and it's a small step from here.

**Conventions you now own:**

- ☐ Fullscreen pass = `import Fullscreen` + `Draw(3)`, no vertex buffer.
- ☐ A pass **reads** with `.Read(tex)` and **writes** with `.Color(tex)`; it can't do both to one texture.
- ☐ Bindless sampling: `GetTexture2D(slot).Sample(GetSampler(kSampler...), uv)`; slots travel in push constants.
- ☐ Post-process pipelines: no depth (`DepthTest/Write = false`, `DepthFormat = Undefined`), `Cull = None`.
- ☐ New shaders auto-build from `Shaders/**.slang` — drop and rebuild.
- ☐ Post-processing sits **after the scene, before the UI**.

---

## Where your existing code fits

You've already jumped ahead into **Part B**: the depth pre-pass pipeline and pass, the `FrameConstants` additions (`Proj` / `InvProj` / `ViewportAO`), and the `m_AO` target are all Part B/C scaffolding. That's fine — we'll formalize and *verify* those as we go (M2 visualizes the depth you're already producing; M3 reconstructs position from it). Your mesh pipeline already defaults `DepthCompare` to `GreaterEq` with `DepthWrite=false`, so the prepass + `DepthLoad` integration is already correct.

> We're **skipping the optional M1** (a second color effect / vignette) — the ping-pong "chain" idea it would have taught shows up naturally later when SSAO feeds a blur. If you ever want bloom/tonemap/vignette, that milestone is a short detour back to Part A.

---

# Milestone 2 — See your depth (and the reverse-Z trick)

**Goal.** You have a working depth pre-pass — but depth is *invisible*, a texture full of numbers you can't look at. This milestone makes it visible, and along the way teaches **reverse-Z**, the single most important convention for everything geometry-aware. M3's position reconstruction is built *directly* on the one formula you'll write here, so this isn't a detour — it's the first half of reconstruction.

It reuses your M0 backbone exactly: same fullscreen pass, new **source** (the depth texture instead of the color texture) and new **math** (linearize instead of passthrough). And it gives you a **reusable debug-view pass** you'll extend for normals (M3/M4) and AO (M6) — your permanent inspection tool.

---

## 2.1 — What "depth" actually is, and why Helio stores it backwards

When the GPU rasterizes a triangle, each pixel records one number: how far that surface is from the camera, in a squashed form the hardware can compare cheaply. That's the depth buffer — `m_DepthTexture`, format `D32_SFLOAT`.

A *conventional* depth buffer stores `0.0` at the near plane and `1.0` at the far plane. Helio does the opposite — **reverse-Z**: `1.0` at near, `0.0` at far, cleared to `0.0`. That's why every depth clear in the codebase is `.Depth(tex, 0.0f)` and every 3D pipeline compares with `Greater`/`GreaterEq`.

**Why backwards?** Floating-point numbers pack most of their precision near zero. A normal depth buffer wastes that precision at the far plane where you least need it, causing *z-fighting* (flickering) on distant surfaces. Flipping it so far = `0.0` lines the float precision up with where distant geometry lands, and z-fighting at range essentially disappears. It's a free quality win, which is why it's standard in modern engines. You inherited it; now you understand it.

The exact relationship for Helio's projection (an infinite-far reverse-Z perspective) is:

```
stored_depth  =  NearZ / z_view
```

where `z_view` is the true distance in front of the camera. Sanity-check it: at the near plane `z_view = NearZ`, so `depth = 1.0`. As `z_view → ∞`, `depth → 0.0`. A cleared pixel (`0.0`) means "nothing here / infinitely far" — that's your sky.

---

## 2.2 — Why you can't just display it, and what "linearize" means

If you sampled the depth texture and drew it straight to the screen, you'd see **almost entirely black** with a thin bright rim right at the camera. That's because `NearZ / z_view` collapses fast: with `NearZ = 0.01`, a surface just 1 meter away already reads `0.00001` — indistinguishable from black. The stored value is deliberately non-linear.

To *see* structure, you undo the squash — recover the real distance:

```
z_view  =  NearZ / stored_depth        // this is the reverse-Z inverse
```

That single line is the heart of this milestone. And here's why it matters beyond looking pretty: **reconstructing a full 3D position from depth (M3) starts with this exact step.** Linearizing depth *is* step one of reconstruction. Do it here, in the easy case where you only care about distance, and M3 becomes "now recover x and y too."

---

## 2.3 — A reusable debug-view pass

Rather than a one-off "show depth" shader, we build a small **debug visualizer**: a post-process pass (same shape as M0) with a `Mode` switch. Today it has one mode — linear depth. As we add normals and AO, we add modes. It becomes the tool you reach for whenever a buffer looks wrong.

Create `Shaders/Passes/DebugView.slang`:

```hlsl
// DebugView.slang — a mode-switched inspector for offscreen buffers.
// Reuses the M0 post-process backbone: fullscreen triangle, reads one texture,
// writes m_PostColor. Grows a new `Mode` per milestone.

import Fullscreen;
import Bindless;

struct DebugPush {
    uint  Mode;        // 1 = linear depth (more modes later)
    uint  SourceSlot;  // bindless slot of the texture to inspect (depth for now)
    float NearZ;       // camera near plane — needed to undo reverse-Z
    float DebugFar;    // display range: surfaces past this read as fully "far"
};
[[vk::push_constant]] DebugPush pc;

[shader("vertex")]
FullscreenVSOut VSMain(uint id : SV_VertexID) { return FullscreenVS(id); }

[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
    if (pc.Mode == 1) {
        // Depth is reverse-Z: stored = NearZ / z_view (1 = near, 0 = far).
        // POINT-sample it — never linear-filter depth (see the note below).
        float d = GetTexture2D(pc.SourceSlot)
                    .Sample(GetSampler(kSamplerPointClamp), In.UV).r;

        if (d <= 0.0) return float4(0, 0, 0, 1);       // cleared/far → sky, show black

        float zView = pc.NearZ / d;                     // <-- the reverse-Z inverse (reused in M3)
        float g = 1.0 - saturate(zView / pc.DebugFar);  // near = white, far = black
        return float4(g, g, g, 1);
    }

    return float4(1, 0, 1, 1); // magenta = "unhandled mode" — loud on purpose
}
```

The two conventions this teaches:

- **Point-sample depth, never linear.** `kSamplerPointClamp` (slot 2 = NEAREST + clamp) reads the exact texel. If you used a *linear* sampler across a depth edge, the GPU would average a near value and a far value into a distance that belongs to *no real surface* — inventing geometry that isn't there. This rule holds for every "data" texture (depth, normals, AO); it's the opposite of M0, where linear filtering of *color* was fine.
- **Make unhandled cases loud.** The magenta fallback means "I bound a mode I didn't implement" screams at you instead of silently drawing black (which you'd mistake for a working-but-empty effect). A debugging habit worth keeping.

---

## 2.4 — Wire it in (C++)

**The push-constant mirror.** Next to your `PostProcessPushConstants` in [`MeshPipeline.h`](../Source/Resource/MeshPipeline.h):

```cpp
struct DebugViewPushConstants {
    uint32_t Mode;
    uint32_t SourceSlot;
    float    NearZ;
    float    DebugFar;
};
```

**Renderer members** in [`SceneRenderer.h`](../Source/Scene/SceneRenderer.h):

```cpp
rhi::PipelineHandle m_DebugViewPipeline;
int m_DebugView = 0;                                   // 0 = normal render, 1 = linear depth
public:
    void SetDebugView(int Mode) noexcept { m_DebugView = Mode; }   // later: bind to a key / editor
```

**Pipeline** in the constructor — identical shape to your `m_PostPipeline` (one color output, no depth, no cull):

```cpp
m_DebugViewPipeline = m_RHI->CreateGraphicsPipeline({
    .ShaderPath = "Shaders/Passes/DebugView.spv",
    .ColorFormats = { rhi::Format::RGBA8_SRGB },
    .ColorAttachmentCount = 1,
    .DepthFormat = rhi::Format::Undefined,
    .Cull = rhi::CullMode::None,
    .DepthTest = false,
    .DepthWrite = false,
    .DebugName = "DebugView"
});
```

**The pass.** In `Render()`, wrap your M0 post-process declaration in a branch — when a debug mode is active, run the inspector *instead*; both write `m_PostColor`, so everything after (UI, present) is untouched:

```cpp
if (m_DebugView == 0)
{
    // ... your normal PostProcess pass from M0 (reads m_ColorTexture) ...
}
else
{
    Rg.Graphics("DebugView")
      .Read(m_DepthTexture)          // depth → SHADER_READ_ONLY (graph inserts the barrier)
      .Color(m_PostColor)
      .Execute([this](rhi::CommandList& C) {
          C.Bind(m_DebugViewPipeline);
          C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
          resource::DebugViewPushConstants pc{};
          pc.Mode       = static_cast<uint32_t>(m_DebugView);
          pc.SourceSlot = m_DepthTexture.SampledSlot;   // valid: depth was created Sampled
          pc.NearZ      = m_Camera ? m_Camera->GetNearZ() : 0.01f;
          pc.DebugFar   = 50.0f;                          // tune to your scene's size
          C.Push(pc);
          C.Draw(3);
      });
}
```

> The `.Read(m_DepthTexture)` is what makes this safe. By the time this pass runs, depth has been written (prepass) and used as a depth attachment (main pass); `.Read` transitions it to shader-readable. You're sampling the *same* depth texture your meshes tested against.

---

## 2.5 — Run and verify

Call `SetDebugView(1)` somewhere (from your app/editor setup, or just default `m_DebugView = 1` temporarily). Rebuild — the shader auto-compiles — and run.

You should see a **grayscale depth map**: near surfaces bright, far surfaces dark, sky black. Fly the camera toward a wall and it brightens; back away and it darkens. Adjust `DebugFar` until the gradient spans your scene nicely (too small → everything clamps to black; too large → everything's white and flat). Set `SetDebugView(0)` to return to normal rendering.

If it's wrong:

- ☐ **All black** → `DebugFar` too small, or `NearZ` wrong, or the prepass isn't writing depth. Bump `DebugFar` to ~200 first; confirm the "Depth Prepass" pass actually runs.
- ☐ **All white** → `DebugFar` far too large; drop it.
- ☐ **Flat magenta** → `Mode` isn't reaching the shader as `1`, or the pipeline bound the wrong `.spv`.
- ☐ **Blocky/aliased edges on the gradient** → you sampled depth with a *linear* sampler; switch to `kSamplerPointClamp`.

---

## 2.6 — Housekeeping (now that we're in Part B)

One real fix worth doing here: your destructor still leaks. `~SceneRenderer()` frees only color/depth/shadow plus two pipelines. Add everything you now own:

```cpp
m_RHI->DestroyTexture(m_NormalTexture);
m_RHI->DestroyTexture(m_AO);
m_RHI->DestroyTexture(m_PostColor);
m_RHI->DestroyPipeline(m_DepthPrepassPipeline);
m_RHI->DestroyPipeline(m_PostPipeline);
m_RHI->DestroyPipeline(m_DebugViewPipeline);
```

Leaks won't crash you, but "free what you create" is the habit — and the day you start creating/destroying targets on resize, a missed teardown becomes a real bug.

---

## 2.7 — What you learned, and why it matters

- **Reverse-Z:** `depth = NearZ / z_view`, near = 1, far = 0. You *must* linearize (`z_view = NearZ / depth`) to interpret depth — and that one line is the seed of position reconstruction.
- **Data textures point-sample, never linear.** Depth, normals, AO — read the exact texel; averaging them invents wrong values.
- **A debug view is just a post-process pass with a mode.** You now have a reusable inspector, and adding a buffer to inspect is one more `if (pc.Mode == N)`.

**Next — Milestone 3:** turn that linearized depth into a full 3D **view-space position** (not just distance — the actual x, y, z of the surface under each pixel), and add it as debug mode 2. That's the final piece of "the screen understands its own geometry" before we can start measuring occlusion for SSAO.
