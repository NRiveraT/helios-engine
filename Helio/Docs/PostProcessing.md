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
- **M2 — See your depth (reverse-Z).** Camera depth is already produced; make it *visible* with a reusable debug-view pass and learn the reverse-Z linearization M3 is built on. *(done)*
- **M3 — Reconstruct view-space position from depth.** Un-project depth back into the surface's 3D point (inline in the debug pass for now; it lifts into a shared `Shaders/Common/ScreenSpace.slang` when SSAO becomes its second consumer). Where reverse-Z + column-vector + the perspective divide get taught. *You are here.*
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

---

# Milestone 3 — Reconstruct a view-space position from depth

**Goal.** In M2 you recovered how *far* a surface is (`z_view = NearZ / depth`). Now recover *where* it is — the full 3D point **(x, y, z) in view space**, the surface's position relative to the camera. You'll add it as a new debug mode and paint it as a grid to confirm it's correct.

**Why this is the milestone that unlocks SSAO.** Ambient occlusion asks, for each pixel, "how much nearby geometry surrounds this point?" To answer that you scatter sample points in a little sphere *around the surface point* and check whether each is buried. You cannot do that with distance alone — you need the actual point. Reconstruction is the operation that turns a flat depth buffer back into 3D positions, and every geometry-aware effect (SSAO, SSR, DOF) begins with it.

---

## 3.1 — The idea: undo the projection

Remember how a point gets to the screen (the mesh vertex shader does this):

```
view-space point  --(Proj)-->  clip space  --(÷w)-->  NDC  -->  the pixel + its depth
```

Reconstruction runs that backwards:

```
the pixel's UV + depth  -->  NDC  --(InvProj)-->  clip space  --(÷w)-->  view-space point
```

`InvProj` is just the inverse of the projection matrix — you already compute it on the CPU (`hlslpp::inverse(Proj)`) and upload it in `FrameConstants`. Two subtleties make or break this, and both are Helio conventions you've met:

1. **Screen UV → NDC.** The fullscreen pass gives you `In.UV` in `[0,1]`. NDC is `[-1,1]`, so `ndc.xy = In.UV * 2 - 1`. Because Helio bakes the Y-flip into the projection, this maps straight through on *both* axes — no manual flip. `ndc.z` is the stored reverse-Z depth as-is.
2. **The perspective divide.** `mul(InvProj, float4(ndc, 1))` gives a *homogeneous* result — a `float4` whose `w` is not 1. You must **divide xyz by w** to get the real 3D point. Skipping the divide is the single most common reconstruction bug; the result looks almost right up close and wildly wrong at distance.

---

## 3.2 — Give the debug shader access to `FrameConstants`

Mode 1 only needed two loose numbers (`NearZ`, `DebugFar`) passed in the push constant. Mode 3 needs the whole `InvProj` matrix — and matrices don't go in push constants, they live in `FrameConstants`. So the debug pass needs the **frame slot**, the same way every 3D pass gets it, to call `LoadFrameConstants`.

Add one field to the push constant. In [`MeshPipeline.h`](../Source/Resource/MeshPipeline.h):

```cpp
struct DebugViewPushConst
{
    uint32_t Mode;
    uint32_t SourceSlot;
    float    NearZ;
    float    DebugFar;
    uint32_t FrameSlot;   // NEW — bindless slot of this frame's FrameConstants
};
static_assert(sizeof(DebugViewPushConst) == 20, "must match the PC block in Shaders/Debug/DebugViewMode.slang");
```

Mirror it in the shader's `DebugPushConst`, and `import Frame;` at the top so you can call `LoadFrameConstants`:

```hlsl
import Fullscreen;
import Bindless;
import Frame;        // NEW — FrameConstants + LoadFrameConstants

struct DebugPushConst {
  uint  Mode;
  uint  SourceSlot;
  float NearZ;
  float DebugFar;
  uint  FrameSlot;   // NEW
};
```

---

## 3.3 — The new shader mode

Add mode 3 to `PSMain` in [`DebugViewMode.slang`](../Shaders/Debug/DebugViewMode.slang):

```hlsl
if (pc.Mode == 3) {
    FrameConstants F = LoadFrameConstants(pc.FrameSlot);

    float d = GetTexture2D(pc.SourceSlot).Sample(GetSampler(kSamplerPointClamp), In.UV).r;
    if (d <= 0.0) return float4(0, 0, 0, 1);     // background: no surface here

    // Screen UV [0,1] -> clip-space NDC [-1,1]. Y-flip is baked into the
    // projection, so v maps straight through: uv*2-1 on BOTH axes. ndc.z is
    // the reverse-Z depth as stored.
    float3 ndc = float3(In.UV * 2.0 - 1.0, d);

    // Undo the projection (clip -> view), then the perspective divide by w
    // turns the homogeneous result back into a real 3D point.
    float4 vh = mul(F.InvProj, float4(ndc, 1.0));   // column-vector: matrix on the LEFT
    float3 viewPos = vh.xyz / vh.w;                  // <-- the divide you must not forget

    // Visualize as a 1-unit grid via frac(): correct reconstruction shows a
    // grid that's continuous across each surface and breaks cleanly at edges.
    return float4(frac(viewPos), 1.0);
}
```

The `SourceSlot` for this mode is the **depth** texture (you reconstruct *from* depth) — same source as mode 1.

---

## 3.4 — Wire it up (C++)

**Pass the frame slot.** The debug pass lambda in `Render()` currently captures `[this, DebugTexture]`; add `FrameSlot` (the local you already computed for the other passes) and set it:

```cpp
Rg.Graphics("Debug View Mode")
  .Read(DebugTexture)
  .Color(m_PostProcessColor)
  .Execute([this, DebugTexture, FrameSlot](rhi::CommandList& C) mutable
  {
      C.Bind(m_DebugViewModePipeline);
      C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));

      resource::DebugViewPushConst pc{};
      pc.Mode      = m_DebugViewMode;
      pc.NearZ     = m_Camera ? m_Camera->GetNearZ() : 0.01f;
      pc.DebugFar  = 100.f;
      pc.SourceSlot = DebugTexture.SampledSlot;
      pc.FrameSlot = FrameSlot;                 // NEW
      C.Push(pc);
      C.Draw(3);
  });
```

**Route mode 3 to the depth texture.** In the `switch (m_DebugViewMode)` that picks `DebugTexture`:

```cpp
case 1: DebugTexture = m_DepthTexture;  break;   // linear depth
case 2: DebugTexture = m_NormalTexture; break;   // world normal
case 3: DebugTexture = m_DepthTexture;  break;   // NEW — view position (reconstructed from depth)
```

**Extend the ImGui combo** so the label array covers the new mode (otherwise `DebugViewMode[3]` reads out of bounds). In [`EditorOverlay.cpp`](../Source/Editor/EditorOverlay.cpp):

```cpp
const char* DebugViewMode[4] = { "Lit", "Depth", "WorldNormal", "ViewPosition" };
```

`IM_ARRAYSIZE` picks up the fourth entry automatically — no other combo change needed.

---

## 3.5 — Run and verify

Pick **ViewPosition** in the debug combo. You should see a **coloured grid painted onto the scene** — a repeating 1-unit lattice that flows smoothly across each surface and snaps at silhouette edges. That coherence is the proof: every pixel independently reconstructed a position, and neighbours agree, so the grid lines up.

Two things that are *expected*, not bugs:
- The grid **swims as you move the camera.** View space is measured *from the camera*, so the positions shift with it. (M4+ effects that need world-locked behaviour are why some engines also keep a world-position path — we don't need it for SSAO.)
- Near the camera the grid cells look large; far away they pack tight. That's correct perspective.

**The built-in correctness test:** the `z` component of your reconstructed `viewPos` must equal mode 1's `NearZ / d`. If you temporarily output `float4(frac(viewPos.zzz), 1.0)`, it should match the Depth view's structure. If it doesn't, your `InvProj` is transposed or being read at the wrong offset.

If it's wrong:
- ☐ **Everything's a flat colour / garbage** → you skipped the `÷ vh.w` divide, or `FrameSlot` isn't reaching the shader.
- ☐ **Looks mirrored or sheared** → `mul()` argument order (must be `mul(F.InvProj, v)`, matrix left), or `InvProj` read at the wrong byte offset in `Frame.slang`.
- ☐ **Grid is right up close but explodes at distance** → the classic missing perspective divide.

---

## 3.6 — What you learned, and why it matters

- **Reconstruction = un-projection + perspective divide.** `viewPos = (InvProj · [ndc, 1]) / w`. This is the exact code SSAO will run per pixel, and per hemisphere sample.
- **Fullscreen passes read `FrameConstants` too** — via a `FrameSlot` in the push constant, same as the 3D passes. You'll lean on this constantly.
- You now have depth (mode 1), normals (mode 2), and **position (mode 3)** — the screen fully "understands its own geometry."

**Next — Milestone 4:** you already produce a world-normal buffer, but for occlusion math SSAO wants a *view-space* normal that's consistent with these view-space positions. M4 formalizes the normal you'll feed SSAO (and you can finally verify it against a normal reconstructed *from* the positions you just built).

---

# Milestone 4 & 5 — how we actually got here (the short version)

We took a shortcut through M4/M5 that's worth recording so the jump to M6 makes sense.

- **M4 (the normal buffer) — done via the depth pre-pass.** Instead of a dedicated normal pass, the depth pre-pass now writes the **geometric world normal** in the same draw that lays down depth ("in one swing"). The prepass VS transforms the interpolated vertex normal by the inverse-transpose (the cofactor matrix, identical to `MeshInstanced.slang`) and the PS outputs it to `m_NormalTexture` (`RGBA16F`, so raw signed `[-1,1]` — no `*0.5+0.5` encoding). Crucially it writes the **geometric** normal, *not* the normal-mapped one: SSAO wants the surface's macro facing, and micro normal-map detail the depth buffer can't confirm would only add noise. This is the normal SSAO consumes.
- **M5 (no-op AO wire) — deferred on purpose.** The `m_AO` target and the "Ambient Occlusion" pass exist and run in the right slot (after the prepass, before the main pass). Lighting does **not** consume `m_AO` yet — we're wiring the consumption *after* the AO math is real, so we never debug an empty pipe and wrong math at the same time.

So the pipeline order today is: **depth+normal prepass → AO pass → main opaque pass**. M6 fills in the AO pass.

---

# Milestone 6 — The SSAO algorithm

**Goal.** Replace the placeholder AO shader with real screen-space ambient occlusion: for each pixel, ask *"how much nearby geometry surrounds this surface point?"* and write the answer (1 = open, 0 = fully buried) to `m_AO`.

**The one-sentence idea.** Reconstruct the surface point in view space, scatter a handful of sample points in a hemisphere oriented along its normal, and count how many of those samples fall *behind* real geometry (as seen in the depth buffer). Many buried samples → a crevice → low AO.

---

## 6.0 — Prerequisites (three loose ends, fix these first)

The math below is view-space and reads a view-space normal, so three things must be true before it works:

1. **`InvProj` must be `inverse(Proj)`.** In `SceneRenderer.cpp` the frame-constants fill currently computes `hlslpp::inverse(m_Camera->GetViewProjection())` — that's `inverse(ViewProj)` (clip→**world**), mislabeled as `InvProj`. Reconstruction here needs clip→**view**. Change it to `hlslpp::inverse(Proj)`. (Without this, every reconstructed position is world-space and the camera-relative math silently breaks.)
2. **Add `View` to `FrameConstants`.** The normal buffer is *world*-space; SSAO needs it in *view* space. Add a `float4x4 View` field (CPU struct + `Mat4Packed` upload of `m_Camera->GetView()` + the `LoadMat4ColumnStored` mirror in `Frame.slang`, and bump the `sizeof` static_assert by 64 bytes) — same pattern `Proj`/`InvProj` already follow. *(Alternative if you want zero new matrix: derive the view normal in-shader as `normalize(cross(ddx(P), ddy(P)))` from the reconstructed positions — faceted and edge-noisy, but no `View`. We use the buffer you built.)*
3. **The AO pass must read depth.** It currently only `.Read(m_NormalTexture)`. Add `.Read(m_DepthTexture)` and pass its slot — you can't reconstruct position without depth bound.

---

## 6.1 — The inputs, per pixel

Two reconstructions, both things you already know how to do:

```
depth  ──(InvProj, ÷w)──►  P   = view-space position   (Milestone 3)
worldN ──(View 3×3)─────►  N   = view-space normal      (rotate the buffer normal)
```

```hlsl
float3 P  = ReconstructViewPos(In.UV, d, F.InvProj);          // view-space point
float3 Nw = normalize(SampleNormal(In.UV).xyz);               // world normal (raw, RGBA16F)
float3 N  = normalize(mul((float3x3)F.View, Nw));             // → view space
```

`(float3x3)F.View` is the view matrix's upper-left 3×3 — a pure rotation (the view transform is rigid), so it rotates the normal correctly with no inverse-transpose needed. Point-sample the normal (never linear — averaging normals across an edge invents a facing that belongs to no surface).

---

## 6.2 — The sample kernel (hemisphere, clustered near the surface)

We test occlusion by placing sample points in the **hemisphere above the surface** — the half-space the normal points into, where an occluder would have to sit to darken this pixel. Two properties matter:

- **Oriented along `N`.** A sphere kernel would put half its samples *inside* the surface (always "occluded"), washing everything grey. The hemisphere spends every sample on the open side.
- **Clustered near the origin.** Nearby geometry occludes far more than distant geometry, so we pack more samples close to `P`. We scale each sample's length by an accelerating curve, `lerp(0.1, 1.0, (i/K)²)`.

We generate the `K` directions in-shader from the **Hammersley** low-discrepancy sequence — deterministic, evenly spread, and needs no uploaded kernel buffer:

```hlsl
float2 xi = Hammersley(i, K);                   // evenly-distributed 2D in [0,1]
float  z  = xi.x;                               // height up the hemisphere [0,1]
float  r  = sqrt(1.0 - z * z);
float  ph = 2.0 * PI * xi.y;
float3 k  = float3(r*cos(ph), r*sin(ph), z);    // tangent space: +z is the normal
k *= lerp(0.1, 1.0, float(i*i) / float(K*K));   // pack near the surface
```

---

## 6.3 — Rotating the kernel per pixel (kill the banding)

The same `K` directions on every pixel produce visible repeating bands. We rotate the whole kernel by a **per-pixel random angle** so the pattern becomes high-frequency noise instead — noise a cheap blur (M7) then dissolves. We use **interleaved gradient noise** (Jimenez) rather than a noise *texture*, so there's nothing extra to allocate:

```hlsl
float  ang = IGN(In.UV * F.ViewportAO.xy) * 2.0 * PI;   // ViewportAO.xy = (W,H)
float3 rnd = float3(cos(ang), sin(ang), 0.0);           // random vector in the tangent plane
float3 T   = normalize(rnd - N * dot(rnd, N));          // Gram-Schmidt against N
float3 B   = cross(N, T);                                // T,B,N is now an orthonormal basis
```

`T,B,N` maps a tangent-space kernel direction into view space. We apply it **explicitly** — `T*k.x + B*k.y + N*k.z` — to sidestep the classic HLSL `float3x3(T,B,N)` row-vs-column ambiguity.

---

## 6.4 — The occlusion test (mind Helio's reverse-Z + +Z-forward)

For each sample, move to the view-space sample point, project it back to the screen, read what surface *actually* lives at that pixel, and compare depths:

```hlsl
float3 samplePos = P + (T*k.x + B*k.y + N*k.z) * RADIUS;   // view-space sample

float4 sc = mul(F.Proj, float4(samplePos, 1.0));           // view → clip (Proj on the LEFT)
float2 su = (sc.xy / sc.w) * 0.5 + 0.5;                     // → screen UV (Y-flip is baked in)

float  sd      = DepthAt(su);                              // stored reverse-Z at that pixel
float3 stored  = ReconstructViewPos(su, sd, F.InvProj);    // its view-space position
float  zStored = stored.z;                                 // ← the surface's view-space z
```

Now the comparison — **this is the one place Helio's conventions bite.** View space is **+Z-forward**, so a *larger* z means *farther* from the camera and a *smaller* z means *closer*. A sample is occluded when the real surface at its screen location is **closer to the camera than the sample point** — i.e. solid geometry sits between the camera and where our sample floats:

```
occluded  ⟺  zStored < samplePos.z − BIAS
```

- `BIAS` (a small view-space distance, ~0.025) subtracts a margin so a sample sitting *on* its own surface doesn't self-occlude from precision wobble.
- A **range check** stops a distant background wall (glimpsed past a thin object) from over-darkening: only occluders within ~`RADIUS` of `P` count.

```hlsl
float rangeCheck = smoothstep(0.0, 1.0, RADIUS / abs(P.z - zStored));
occlusion += (zStored < samplePos.z - BIAS ? 1.0 : 0.0) * rangeCheck;
```

> **If your AO comes out inverted** (crevices bright, flats dark), this comparison is the first suspect — flip it to `zStored > samplePos.z + BIAS` only if you've *confirmed* your view space is −Z-forward. Helio's is +Z-forward, so the form above is correct.

---

## 6.5 — From occlusion to AO

Average over the kernel, invert (occlusion is the *dark* term), and raise to a power for contrast:

```
AO = ( 1 − occlusion / K ) ^ POWER
```

`POWER` (~1.5) deepens contact shadows without crushing mid-tones. Write `AO` to all three channels (so the debug view shows it as grey) — the lighting consumer only reads `.r`.

---

## 6.6 — The full shader

`Shaders/Passes/AmbientOcclusion.slang` — replacing the red placeholder:

```hlsl
import Fullscreen;
import Bindless;
import Frame;

static const uint  K      = 24;      // samples per pixel
static const float RADIUS = 0.5;     // view-space units — tune to scene scale
static const float BIAS   = 0.025;   // view-space units — self-occlusion margin
static const float POWER  = 1.5;     // contrast
static const float PI     = 3.14159265;

struct AOPush { uint FrameSlot; uint DepthSlot; uint NormalSlot; };
[[vk::push_constant]] AOPush pc;

[shader("vertex")]
FullscreenVSOut VSMain(uint id : SV_VertexID) { return FullscreenVS(id); }

// van der Corput radical inverse → Hammersley low-discrepancy set (no uploaded kernel)
float RadicalInverse(uint b) {
    b = (b << 16) | (b >> 16);
    b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
    b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
    b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
    b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
    return float(b) * 2.3283064365386963e-10;              // ÷ 2^32
}
float2 Hammersley(uint i, uint n) { return float2(float(i) / n, RadicalInverse(i)); }

// interleaved gradient noise → per-pixel rotation angle (no noise texture)
float IGN(float2 p) { return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715)))); }

float DepthAt(uint slot, float2 uv) {
    return GetTexture2D(slot).SampleLevel(GetSampler(kSamplerPointClamp), uv, 0.0).r;
}
float3 ReconstructViewPos(float2 uv, float d, float4x4 invProj) {
    float3 ndc = float3(uv * 2.0 - 1.0, d);
    float4 vh  = mul(invProj, float4(ndc, 1.0));
    return vh.xyz / vh.w;
}

[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
    FrameConstants F = LoadFrameConstants(pc.FrameSlot);

    float d = DepthAt(pc.DepthSlot, In.UV);
    if (d <= 0.0) return float4(1, 1, 1, 1);                        // sky: unoccluded

    float3 P  = ReconstructViewPos(In.UV, d, F.InvProj);
    float3 Nw = normalize(GetTexture2D(pc.NormalSlot)
                          .SampleLevel(GetSampler(kSamplerPointClamp), In.UV, 0.0).xyz);
    float3 N  = normalize(mul((float3x3)F.View, Nw));              // world → view

    float  ang = IGN(In.UV * F.ViewportAO.xy) * 2.0 * PI;
    float3 rnd = float3(cos(ang), sin(ang), 0.0);
    float3 T   = normalize(rnd - N * dot(rnd, N));
    float3 B   = cross(N, T);

    float occlusion = 0.0;
    for (uint i = 0; i < K; ++i) {
        float2 xi = Hammersley(i, K);
        float  z  = xi.x;
        float  r  = sqrt(1.0 - z * z);
        float  ph = 2.0 * PI * xi.y;
        float3 k  = float3(r * cos(ph), r * sin(ph), z);
        k *= lerp(0.1, 1.0, float(i * i) / float(K * K));

        float3 samplePos = P + (T * k.x + B * k.y + N * k.z) * RADIUS;

        float4 sc = mul(F.Proj, float4(samplePos, 1.0));
        float2 su = (sc.xy / sc.w) * 0.5 + 0.5;
        if (any(su < 0.0) || any(su > 1.0)) continue;              // off-screen: skip

        float sd = DepthAt(pc.DepthSlot, su);
        if (sd <= 0.0) continue;                                    // sampled the sky
        float zStored = ReconstructViewPos(su, sd, F.InvProj).z;

        float rangeCheck = smoothstep(0.0, 1.0, RADIUS / abs(P.z - zStored));
        occlusion += (zStored < samplePos.z - BIAS ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = pow(saturate(1.0 - occlusion / float(K)), POWER);
    return float4(ao, ao, ao, 1.0);
}
```

Note `SampleLevel(..., 0.0)`, not `Sample`: the depth taps land at a *computed* `su` inside a loop, where implicit derivatives are undefined — forcing LOD 0 is correct and avoids driver warnings.

---

## 6.7 — Wire it up (C++)

The AO pass already exists; it needs depth bound and the fuller push constant:

```cpp
Rg.Graphics("Ambient Occlusion")
  .Read(m_NormalTexture)
  .Read(m_DepthTexture)                 // NEW — reconstruction needs depth
  .Color(m_AO, 0.f, 0.f, 0.f, 1.f)
  .Execute([this, FrameSlot](rhi::CommandList& C) {
      HELIO_PROFILE_ZONE("AO")
      struct AOPush { uint32_t FrameSlot; uint32_t DepthSlot; uint32_t NormalSlot; } pc;
      pc.FrameSlot  = FrameSlot;
      pc.DepthSlot  = m_DepthTexture.SampledSlot;   // NEW
      pc.NormalSlot = m_NormalTexture.SampledSlot;
      C.Bind(m_AmbientOcclusionPipeline);
      C.SetViewport(static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
      C.Push(pc);
      C.Draw(3);
  });
```

Plus the two prerequisites from 6.0: the `InvProj = inverse(Proj)` fix and the `View` matrix addition to `FrameConstants`.

---

## 6.8 — See it before you consume it

You haven't wired AO into lighting yet — so **route `m_AO` through the debug view** to inspect it in isolation. Add an `AmbientOcclusion` debug mode whose `DebugTexture = m_AO` and display `.r` as greyscale (it's already single-value, so no decode). Fly around: seams, contact points, and concave corners should darken; open floors and walls stay white. That's your ground truth before it ever touches ambient.

If it's wrong:
- ☐ **Uniform grey / no variation** → `RADIUS` far too small or too large for your scene; try 0.2–1.0. Or `View`/`InvProj` not reaching the shader (normals/positions garbage).
- ☐ **Inverted (crevices bright)** → the occlusion comparison sign (6.4).
- ☐ **Dark halos around every silhouette** → expected pre-blur to a degree; the range check tames it. If severe, raise `BIAS` slightly.
- ☐ **Swimming/marching noise** → that's the per-pixel rotation doing its job; it's what the M7 blur removes.

---

## 6.9 — What you built, and what's next

- **SSAO is view-space geometry sampling:** reconstruct `P` and `N`, orient a hemisphere kernel, and count samples that fall behind the depth buffer. Every input was something earlier milestones already built.
- **Zero new resources:** the kernel is Hammersley-in-shader, the rotation is IGN-in-shader — no kernel buffer, no noise texture.
- **The output is deliberately noisy.** The per-pixel rotation trades banding for high-frequency grain.

**Next — Milestone 7 (blur):** a small edge-aware (depth-aware) blur over `m_AO` removes the grain without bleeding occlusion across silhouettes. **Then Milestone 8:** wire `m_AO` into the ambient term (sample it at the pixel's *screen* UV, multiply `Ambient` only — never direct light) and tune `RADIUS`/`POWER` to taste.
